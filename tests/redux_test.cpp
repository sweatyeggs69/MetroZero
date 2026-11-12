// Redux Mode: ROM tweaks apply and revert cleanly, refuse a ROM they do not match, and the host
// tweak (still Morph Ball) changes only what it should. No ROM needed; set METROZERO_ROM to a
// Metroid: Zero Mission (USA) dump to also check that every tweak matches it.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "app/redux.h"

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

namespace redux = metrozero::redux;

// A blank 8 MiB "ROM" holding only the original bytes every patch expects.
std::vector<std::uint8_t> fake_rom() {
    std::vector<std::uint8_t> rom(8u << 20, 0xAA);
    for (const auto& t : redux::kTweaks)
        for (const auto& p : t.patches)
            for (std::size_t i = 0; i < p.size; ++i) rom[p.address - 0x08000000u + i] = p.original[i];
    return rom;
}

std::uint16_t get16(const std::vector<std::uint8_t>& m, std::size_t at) { return std::uint16_t(m[at] | (m[at + 1] << 8)); }
void set16(std::vector<std::uint8_t>& m, std::size_t at, std::uint16_t v) { m[at] = std::uint8_t(v); m[at + 1] = std::uint8_t(v >> 8); }
}  // namespace

int main() {
    // Table sanity: unique ids, ROM tweaks have patches, host tweaks have none, patches fit and
    // really change something.
    for (std::size_t i = 0; i < redux::kTweakCount; ++i) {
        const auto& t = redux::kTweaks[i];
        CHECK(redux::index_of(t.id) == i);
        CHECK((t.kind == redux::Kind::Rom) == !t.patches.empty());
        for (const auto& p : t.patches) {
            CHECK(p.size > 0 && p.size <= 8);
            CHECK(p.address >= 0x08000000u && p.address + p.size <= 0x08800000u);
            bool differs = false;
            for (std::size_t k = 0; k < p.size; ++k) differs |= p.original[k] != p.replacement[k];
            CHECK(differs);
        }
    }
    CHECK(redux::index_of("nope") == redux::kTweakCount);

    // Apply everything, then revert everything: back to the original bytes.
    const auto clean = fake_rom();
    auto rom = clean;
    redux::Flags ok{}, all{}, none{};
    for (std::size_t i = 0; i < redux::kTweakCount; ++i) {
        ok[i] = redux::available(clean, redux::kTweaks[i]);
        CHECK(ok[i]);
        all[i] = true;
    }
    CHECK(redux::sync(rom, all, ok));
    CHECK(rom != clean);
    CHECK(!redux::sync(rom, all, ok));  // already applied
    for (const auto& t : redux::kTweaks)
        for (const auto& p : t.patches) CHECK(rom[p.address - 0x08000000u] == p.replacement[0]);
    CHECK(redux::sync(rom, none, ok));
    CHECK(rom == clean);

    // One tweak only touches its own bytes.
    redux::Flags one{};
    one[redux::index_of("accurate_clock")] = true;
    CHECK(redux::sync(rom, one, ok));
    std::size_t diffs = 0;
    for (std::size_t i = 0; i < rom.size(); ++i) diffs += rom[i] != clean[i];
    CHECK(diffs == 1);  // cmp r0,#63 -> #59 is a one-byte change
    redux::sync(rom, none, ok);

    // A ROM that does not match is never written.
    auto other = clean;
    const auto& elev = redux::kTweaks[redux::index_of("fast_elevators")];
    other[elev.patches[0].address - 0x08000000u] ^= 0xFF;
    CHECK(!redux::available(other, elev));
    redux::Flags ok_other = ok;
    ok_other[redux::index_of("fast_elevators")] = redux::available(other, elev);
    const auto before = other;
    redux::Flags elevs{};
    elevs[redux::index_of("fast_elevators")] = true;
    CHECK(!redux::sync(other, elevs, ok_other));
    CHECK(other == before);

    // Morph Ball: a still ball keeps its frame; rolling and other poses are untouched.
    {
        std::vector<std::uint8_t> iwram(0x8000, 0);
        redux::Host host;
        redux::Flags f{};
        f[redux::index_of("morph_idle")] = true;
        iwram[redux::kSamusPose] = redux::kPoseMorphBall;
        iwram[redux::kSamusAnimFrame] = 2;
        host.before_frame(iwram, f, 0);
        iwram[redux::kSamusAnimFrame] = 3;  // the game advanced the animation
        iwram[redux::kSamusAnimCounter] = 4;
        host.after_frame(iwram, f);
        CHECK(iwram[redux::kSamusAnimFrame] == 2);
        CHECK(iwram[redux::kSamusAnimCounter] == 0);
        iwram[redux::kSamusPose] = 18;  // rolling
        host.before_frame(iwram, f, 0);
        iwram[redux::kSamusAnimFrame] = 1;
        host.after_frame(iwram, f);
        CHECK(iwram[redux::kSamusAnimFrame] == 1);
    }

    // Optional: every tweak matches a real dump.
    if (const char* path = std::getenv("METROZERO_ROM")) {
        std::ifstream in(path, std::ios::binary);
        std::vector<std::uint8_t> real((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(real.size() == (8u << 20));
        for (const auto& t : redux::kTweaks) {
            const bool matches = redux::available(real, t);
            if (!matches) std::fprintf(stderr, "tweak %s does not match %s\n", t.id, path);
            CHECK(matches);
        }
    }

    if (failures) return 1;
    std::printf("redux: all checks passed\n");
    return 0;
}
