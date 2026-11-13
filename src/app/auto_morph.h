#pragma once
// Auto Morph Ball (F1 > Cheats/Mods > Mods, off by default). When Samus runs into a wall that has a
// one-block-high Morph Ball tunnel at floor level, she morphs on her own and rolls in.
//
// Nothing in the game's memory is written: the host only changes the buttons it sends, doing what
// a player would (Down to crouch, let go, Down again to morph). It reads:
//   gSamusData (IWRAM 0x030013D4): +0 pose, +0x12 x, +0x14 y (feet), in 1/4-pixel units
//   gEquipment (IWRAM 0x03001530): +0xF active suit flags, bit 6 = Morph Ball
//   gBgPointersAndDimensions (IWRAM 0x0300009C): +0x18 u32 clipdata map pointer, +0x1C u16 width, +0x1E u16 height
//   gTilemapAndClipPointers (IWRAM 0x03005450): +4 u32 pointer to the clip value -> collision type table
// (names and layouts from the community decompilation, metroidret/mzm).
// Collision types: 0 = air, 1 = solid (other values: slopes, doors, enemy-only, etc.).
//
// It fires only when all of these hold:
//   - the Morph Ball is collected
//   - Samus is standing or running on the ground (not jumping, sparking, hanging, ...)
//   - Left or Right is held toward the wall, alone (no Up, Down, L, A or B), for kHoldFrames frames
//   - the two blocks ahead at floor level are plain air with plain solid blocks right above them,
//     and the floor under them is solid. So a one-block notch or a lip she could roll under into
//     open space does not count.
// After she morphs it waits for the direction to be let go before it can fire again.
//
// Setting (metrozero::Config): mods.auto_morph (off by default). Off in RetroAchievements hardcore.
//
// The same class also runs the Morph button (Settings > Controls > Extra Controls): one press
// morphs Samus when she is standing, running or crouching on the ground (with the Morph Ball
// collected), and another press while in the ball stands her back up, by pressing Down twice or
// Up for the player. It works without the mod, since it only presses buttons the player could.
#include <cstddef>
#include <cstdint>
#include <span>

#include "game.h"

namespace metrozero::auto_morph {

// GBA KEYINPUT bit order (1 = pressed in the host's mask).
inline constexpr std::uint16_t kA = 1u << 0, kB = 1u << 1, kRight = 1u << 4, kLeft = 1u << 5,
                               kUp = 1u << 6, kDown = 1u << 7, kR = 1u << 8, kL = 1u << 9;
inline constexpr std::uint16_t kDirs = kRight | kLeft | kUp | kDown;

inline constexpr int kHoldFrames = 6;      // pushing into the wall this long before morphing
inline constexpr int kStepTimeout = 16;    // give up if the game does not crouch / morph in time

inline constexpr unsigned kBlock = 64;     // one 16-pixel block in the game's 1/4-pixel units
inline constexpr unsigned kHalfWidth = 28; // Samus's standing hitbox: x - 28 .. x + 28

inline constexpr unsigned kBackgroundsData = 0x0300009C;
inline constexpr unsigned kTilemapAndClipPointers = 0x03005450;

inline constexpr unsigned kPoseRunning = 0, kPoseStanding = 1, kPoseCrouching = 4, kPoseMorphing = 16,
                          kPoseMorphBall = 17, kPoseRolling = 18;

// Read-only view of game memory, addressed the way the GBA sees it.
struct Memory {
    std::span<const std::uint8_t> iwram;  // 0x03000000
    std::span<const std::uint8_t> ewram;  // 0x02000000
    std::span<const std::uint8_t> rom;    // 0x08000000

    bool read8(std::uint32_t a, std::uint8_t& out) const {
        std::span<const std::uint8_t> m;
        switch (a >> 24) {
            case 0x02: m = ewram; a &= 0x3FFFF; break;
            case 0x03: m = iwram; a &= 0x7FFF; break;
            case 0x08: case 0x09: m = rom; a &= 0x1FFFFFF; break;
            default: return false;
        }
        if (a >= m.size()) return false;
        out = m[a];
        return true;
    }
    bool read16(std::uint32_t a, std::uint16_t& out) const {
        std::uint8_t lo, hi;
        if (!read8(a, lo) || !read8(a + 1, hi)) return false;
        out = std::uint16_t(lo | (hi << 8));
        return true;
    }
    bool read32(std::uint32_t a, std::uint32_t& out) const {
        std::uint16_t lo, hi;
        if (!read16(a, lo) || !read16(a + 2, hi)) return false;
        out = std::uint32_t(lo) | (std::uint32_t(hi) << 16);
        return true;
    }
};

enum class Block { Air, Solid, Other };

// Collision type of the block at (bx, by), or Other when it can't be read or isn't plain air/solid.
inline Block block_at(const Memory& m, int bx, int by) {
    std::uint32_t map, table;
    std::uint16_t w, h, clip;
    if (!m.read32(kBackgroundsData + 0x18, map) || !m.read16(kBackgroundsData + 0x1C, w) ||
        !m.read16(kBackgroundsData + 0x1E, h) || !m.read32(kTilemapAndClipPointers + 4, table))
        return Block::Other;
    if (bx < 0 || by < 0 || bx >= w || by >= h) return Block::Other;
    if (!m.read16(map + 2u * (std::uint32_t(by) * w + std::uint32_t(bx)), clip)) return Block::Other;
    std::uint8_t type;
    if (!m.read8(table + clip, type)) return Block::Other;
    return type == 0 ? Block::Air : type == 1 ? Block::Solid : Block::Other;
}

struct Samus {
    unsigned pose = 0xFF;
    unsigned x = 0, y = 0;
    bool morph_ball = false;
};

inline Samus read_samus(const Memory& m) {
    Samus s;
    std::uint8_t pose, suit;
    std::uint16_t x, y;
    if (!m.read8(game::kSamusDataAddress + 0, pose) || !m.read16(game::kSamusDataAddress + 0x12, x) ||
        !m.read16(game::kSamusDataAddress + 0x14, y) || !m.read8(game::kEquipmentAddress + 0xF, suit))
        return s;
    s.pose = pose;
    s.x = x;
    s.y = y;
    s.morph_ball = (suit & game::kSuitMorphBall) != 0;
    return s;
}

// True when Samus stands right against a Morph Ball tunnel on her `dir` side (+1 right, -1 left).
inline bool tunnel_ahead(const Memory& m, const Samus& s, int dir) {
    if (s.y == 0) return false;
    const int row = int((s.y - 1) / kBlock);  // the block row Samus's feet are in
    const int front = dir > 0 ? int((s.x + kHalfWidth + 2) / kBlock) : int((int(s.x) - int(kHalfWidth) - 2) / int(kBlock));
    if (row < 1 || front < 0) return false;
    for (int i = 0; i < 2; ++i) {
        const int bx = front + dir * i;
        if (block_at(m, bx, row) != Block::Air) return false;
        if (block_at(m, bx, row - 1) != Block::Solid) return false;
        if (block_at(m, bx, row + 1) != Block::Solid) return false;
    }
    return true;
}

class Assist {
public:
    // Call once per game frame with the buttons about to be sent and memory as the last frame left
    // it. Returns the buttons to send.
    // auto_on: the Auto Morph Ball mod is on (the Morph button works either way).
    std::uint16_t step(std::uint16_t keys, const Memory& mem, bool auto_on = true) {
        const Samus s = read_samus(mem);
        const std::uint16_t dir_key = keys & (kLeft | kRight);
        if (toggle_requested_ && !busy()) {
            toggle_requested_ = false;
            const bool in_ball = s.pose == kPoseMorphing || s.pose == kPoseMorphBall || s.pose == kPoseRolling;
            if (in_ball) start(Phase::Unmorph, 0);
            else if (s.morph_ball && s.pose == kPoseCrouching) start(Phase::Morph, 0);
            else if (s.morph_ball && (s.pose == kPoseStanding || s.pose == kPoseRunning)) start(Phase::Crouch, 0);
        }
        switch (phase_) {
            case Phase::Watch: {
                if (!auto_on) {
                    held_ = 0;
                    return keys;
                }
                const bool lone_dir = (dir_key == kLeft || dir_key == kRight) &&
                                      !(keys & (kUp | kDown | kA | kB | kL));
                const bool grounded = s.pose == kPoseStanding || s.pose == kPoseRunning;
                if (lone_dir && grounded && s.morph_ball &&
                    tunnel_ahead(mem, s, dir_key == kRight ? 1 : -1)) {
                    if (++held_ >= kHoldFrames) start(Phase::Crouch, dir_key);
                } else {
                    held_ = 0;
                }
                return keys;
            }
            case Phase::Crouch:  // tap Down until the game shows her crouching
                if (s.pose == kPoseCrouching) { start(Phase::Morph, dir_); return tap(keys); }
                if (++timer_ > kStepTimeout) { start(Phase::Cooldown, dir_); return keys; }
                return tap(keys);
            case Phase::Morph:  // tap Down again until she morphs
                if (s.pose == kPoseMorphing || s.pose == kPoseMorphBall || s.pose == kPoseRolling) {
                    start(Phase::Cooldown, dir_);
                    return std::uint16_t(keys & ~(kUp | kDown));
                }
                if (++timer_ > kStepTimeout) { start(Phase::Cooldown, dir_); return keys; }
                return tap(keys);
            case Phase::Unmorph:  // tap Up until she is standing again (crouching on the way)
                if (s.pose == kPoseStanding || s.pose == kPoseRunning) { start(Phase::Cooldown, 0); return keys; }
                if (++timer_ > 2 * kStepTimeout) { start(Phase::Cooldown, 0); return keys; }
                down_ = !down_;
                return std::uint16_t((keys & ~kDirs) | (down_ ? kUp : 0));
            case Phase::Cooldown:
                // Hand control back, and wait for the direction to be let go before watching again.
                if (!(keys & dir_)) start(Phase::Watch, 0);
                return keys;
        }
        return keys;
    }
    void reset() {
        start(Phase::Watch, 0);
        toggle_requested_ = false;
    }
    // Morph button pressed: morph, or stand back up if already in the ball. Runs on the next step().
    void request_toggle() { toggle_requested_ = true; }
    bool busy() const { return phase_ == Phase::Crouch || phase_ == Phase::Morph || phase_ == Phase::Unmorph; }

private:
    // The game only reacts to a new Down press, so Down goes up and down on alternate frames, and
    // starts released. The direction stays released meanwhile so she doesn't stand back up.
    std::uint16_t tap(std::uint16_t keys) {
        down_ = !down_;
        return std::uint16_t((keys & ~kDirs) | (down_ ? kDown : 0));
    }
    enum class Phase { Watch, Crouch, Morph, Unmorph, Cooldown };
    void start(Phase p, std::uint16_t dir) {
        phase_ = p;
        dir_ = dir;
        timer_ = 0;
        held_ = 0;
        down_ = true;  // so the first tap() is a release
    }
    Phase phase_ = Phase::Watch;
    std::uint16_t dir_ = 0;  // the direction held into the wall
    int held_ = 0;
    int timer_ = 0;
    bool down_ = true;  // Down (Up when unmorphing) was sent on the last tap() frame
    bool toggle_requested_ = false;
};

}  // namespace metrozero::auto_morph
