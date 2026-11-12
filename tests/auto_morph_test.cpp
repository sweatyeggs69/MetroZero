// Auto Morph Ball: tunnel detection on a fake room, and the crouch / release / morph button sequence.
#include <cstdio>
#include <string>
#include <vector>

#include "app/auto_morph.h"

using namespace metrozero;
using namespace metrozero::auto_morph;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

struct Fake {
    std::vector<std::uint8_t> iwram = std::vector<std::uint8_t>(0x8000, 0);
    std::vector<std::uint8_t> ewram = std::vector<std::uint8_t>(0x40000, 0);
    std::vector<std::uint8_t> rom = std::vector<std::uint8_t>(0x100, 0);

    void put16(std::vector<std::uint8_t>& m, std::size_t at, std::uint16_t v) {
        m[at] = std::uint8_t(v);
        m[at + 1] = std::uint8_t(v >> 8);
    }
    void put32(std::vector<std::uint8_t>& m, std::size_t at, std::uint32_t v) {
        put16(m, at, std::uint16_t(v));
        put16(m, at + 2, std::uint16_t(v >> 16));
    }
    // Rows top to bottom: '#' solid (clip 1), '.' air (clip 0), 's' slope (clip 2).
    void room(const std::vector<std::string>& rows) {
        const std::uint16_t w = std::uint16_t(rows[0].size()), h = std::uint16_t(rows.size());
        put32(iwram, kBackgroundsData + 0x18 - 0x03000000, 0x02026000);
        put16(iwram, kBackgroundsData + 0x1C - 0x03000000, w);
        put16(iwram, kBackgroundsData + 0x1E - 0x03000000, h);
        put32(iwram, kTilemapAndClipPointers + 4 - 0x03000000, 0x08000010);
        rom[0x10] = 0;  // clip 0 -> air
        rom[0x11] = 1;  // clip 1 -> solid
        rom[0x12] = 2;  // clip 2 -> slope
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                put16(ewram, 0x26000 + 2 * (y * w + x), rows[y][x] == '#' ? 1 : rows[y][x] == 's' ? 2 : 0);
    }
    // Samus with her feet on top of block row `floor`, centred in block column `col`, pushed
    // against the right (or left) edge of that column.
    void samus(int col, int floor, int against, unsigned pose = kPoseStanding, bool morph = true) {
        const unsigned x = against > 0 ? unsigned(col * 64 + 63 - (kHalfWidth + 1)) : unsigned(col * 64 + kHalfWidth + 1);
        iwram[game::kSamusDataAddress + 0 - 0x03000000] = std::uint8_t(pose);
        put16(iwram, game::kSamusDataAddress + 0x12 - 0x03000000, std::uint16_t(x));
        put16(iwram, game::kSamusDataAddress + 0x14 - 0x03000000, std::uint16_t(floor * 64 - 1));
        iwram[game::kEquipmentAddress + 0xF - 0x03000000] = morph ? std::uint8_t(game::kSuitMorphBall) : 0;
    }
    void pose(unsigned p) { iwram[game::kSamusDataAddress + 0 - 0x03000000] = std::uint8_t(p); }
    Memory mem() const { return Memory{iwram, ewram, rom}; }
};

// Samus stands in column 2; her feet are in row 3, the floor is row 4.
static const std::vector<std::string> kTunnel = {
    "##########",
    "..........",
    "...#######",
    "..........",
    "##########",
};

int main() {
    {
        Fake f;
        f.room(kTunnel);
        f.samus(2, 4, +1);
        check(tunnel_ahead(f.mem(), read_samus(f.mem()), +1), "tunnel to the right is found");
        check(!tunnel_ahead(f.mem(), read_samus(f.mem()), -1), "open space to the left is not a tunnel");
        f.samus(2, 4, -1);
        check(!tunnel_ahead(f.mem(), read_samus(f.mem()), +1), "not touching the wall: no tunnel");
    }
    {
        Fake f;  // one-block notch: only one block deep
        f.room({"##########", "..........", "...#######", "...#######", "##########"});
        f.samus(2, 4, +1);
        check(!tunnel_ahead(f.mem(), read_samus(f.mem()), +1), "solid wall is not a tunnel");
        f.room({"##########", "..........", "...#######", "....######", "##########"});
        check(!tunnel_ahead(f.mem(), read_samus(f.mem()), +1), "one-block notch is not a tunnel");
        f.room({"##########", "..........", "...#......", "..........", "##########"});
        check(!tunnel_ahead(f.mem(), read_samus(f.mem()), +1), "lip with open space behind is not a tunnel");
        f.room({"##########", "..........", "...#######", "..........", "###...####"});
        check(!tunnel_ahead(f.mem(), read_samus(f.mem()), +1), "gap over a pit is not a tunnel");
        f.room({"##########", "..........", "...#######", "...s......", "##########"});
        check(!tunnel_ahead(f.mem(), read_samus(f.mem()), +1), "slope in the mouth is not a tunnel");
    }
    {
        // Button sequence: hold Right into the tunnel.
        Fake f;
        f.room(kTunnel);
        f.samus(2, 4, +1);
        Assist a;
        int i = 0;
        for (; i < kHoldFrames - 1; ++i) check(a.step(kRight, f.mem()) == kRight, "passes Right through while waiting");
        check(a.step(kRight, f.mem()) == kRight, "last waiting frame still passes Right");
        check(a.step(kRight, f.mem()) == 0, "crouch: starts with everything released");
        check(a.step(kRight, f.mem()) == kDown, "crouch: Down alone");
        check(a.step(kRight, f.mem()) == 0, "crouch: Down released again while she isn't crouching yet");
        check(a.step(kRight, f.mem()) == kDown, "crouch: Down tapped again");
        f.pose(kPoseCrouching);
        check(a.step(kRight, f.mem()) == 0, "morph: Down released first");
        check(a.step(kRight, f.mem()) == kDown, "morph: new Down press");
        f.pose(kPoseMorphing);
        check(a.step(kRight, f.mem()) == kRight, "control handed back once morphing");
        f.pose(kPoseMorphBall);
        check(a.step(kRight, f.mem()) == kRight, "rolls on with Right");
        // No retrigger while Right is still held, even standing back at a tunnel.
        f.pose(kPoseStanding);
        for (int j = 0; j < 30; ++j) check(a.step(kRight, f.mem()) == kRight, "no retrigger until Right is let go");
    }
    {
        // Things that must never fire.
        Fake f;
        f.room(kTunnel);
        auto never = [&](std::uint16_t keys, const char* what) {
            Assist a;
            for (int j = 0; j < 60; ++j) check(a.step(keys, f.mem()) == keys, what);
        };
        f.samus(2, 4, +1, kPoseStanding, false);
        never(kRight, "no Morph Ball yet");
        f.samus(2, 4, +1, 4 /* mid-air */);
        never(kRight, "mid-air");
        f.samus(2, 4, +1);
        never(kRight | kA, "jumping (A held)");
        never(kRight | kB, "shooting (B held)");
        never(kRight | kL, "aiming (L held)");
        never(kRight | kUp, "Up held");
        never(kLeft, "holding away from the tunnel");
        never(0, "nothing held");
    }
    {
        // Gives up if the game never crouches.
        Fake f;
        f.room(kTunnel);
        f.samus(2, 4, +1);
        Assist a;
        int downs = 0;
        for (int j = 0; j < 60; ++j) downs += (a.step(kRight, f.mem()) & kDown) != 0;
        check(downs > 0 && downs <= kStepTimeout, "stops pressing Down when the game doesn't respond");
    }

    if (failures == 0) std::printf("auto_morph: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
