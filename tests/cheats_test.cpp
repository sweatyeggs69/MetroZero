// Cheats: refills Samus's energy, missiles, super missiles and power bombs in a blank IWRAM image.
// No ROM needed.
#include <array>
#include <cstdint>
#include <cstdio>

#include "app/cheats.h"

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
constexpr std::size_t kBase = metrozero::game::kEquipmentAddress - 0x03000000u;
std::uint16_t get16(const std::array<std::uint8_t, 0x8000>& m, std::size_t at) { return std::uint16_t(m[at] | (m[at + 1] << 8)); }
void set16(std::array<std::uint8_t, 0x8000>& m, std::size_t at, std::uint16_t v) { m[at] = std::uint8_t(v); m[at + 1] = std::uint8_t(v >> 8); }
}  // namespace

int main() {
    using metrozero::cheats::Flags;
    // Qualified: unqualified apply(std::array, ...) also finds std::apply by ADL and fails on libc++.
    namespace cheats = metrozero::cheats;
    std::array<std::uint8_t, 0x8000> ram{};

    // Nothing enabled, or nothing owned yet: no writes.
    CHECK(!cheats::apply(ram, Flags{}));
    CHECK(!cheats::apply(ram, Flags{true, true, true, true}));
    CHECK(get16(ram, kBase + 6) == 0);

    // Energy 12 of 299, missiles 3 of 50, super missiles 2 of 10, power bombs 1 of 6.
    set16(ram, kBase + 0x0, 299);
    set16(ram, kBase + 0x2, 50);
    ram[kBase + 0x4] = 10;
    ram[kBase + 0x5] = 6;
    set16(ram, kBase + 0x6, 12);
    set16(ram, kBase + 0x8, 3);
    ram[kBase + 0xA] = 2;
    ram[kBase + 0xB] = 1;

    CHECK(cheats::apply(ram, Flags{true, false, false, false}));
    CHECK(get16(ram, kBase + 0x6) == 299);
    CHECK(get16(ram, kBase + 0x8) == 3);  // missiles untouched
    CHECK(ram[kBase + 0xA] == 2 && ram[kBase + 0xB] == 1);

    CHECK(cheats::apply(ram, Flags{false, true, false, false}));
    CHECK(get16(ram, kBase + 0x8) == 50);
    CHECK(ram[kBase + 0xA] == 2);

    CHECK(cheats::apply(ram, Flags{false, false, true, false}));
    CHECK(ram[kBase + 0xA] == 10 && ram[kBase + 0xB] == 1);

    CHECK(cheats::apply(ram, Flags{false, false, false, true}));
    CHECK(ram[kBase + 0xB] == 6);

    // Already full: reports no change.
    CHECK(!cheats::apply(ram, Flags{true, true, true, true}));

    // Super missiles not found yet (max 0): energy still refills, supers stay 0.
    set16(ram, kBase + 0x6, 1);
    ram[kBase + 0x4] = 0;
    ram[kBase + 0xA] = 0;
    CHECK(cheats::apply(ram, Flags{true, false, true, false}));
    CHECK(get16(ram, kBase + 0x6) == 299);
    CHECK(ram[kBase + 0xA] == 0);

    // The maxima and the item flags after the counts are never touched.
    CHECK(get16(ram, kBase + 0x0) == 299 && ram[kBase + 0x4] == 0);
    CHECK(ram[kBase - 1] == 0 && ram[kBase + 0xC] == 0 && ram[kBase + 0xE] == 0);

    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::puts("cheats: all checks passed");
    return failures ? 1 : 0;
}
