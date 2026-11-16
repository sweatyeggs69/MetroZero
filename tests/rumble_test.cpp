// Rumble: which changes in a blank IWRAM image turn into controller pulses. No ROM needed.
#include <array>
#include <cstdint>
#include <cstdio>

#include "app/rumble.h"

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
using Ram = std::array<std::uint8_t, 0x8000>;
constexpr std::size_t kEq = metrozero::game::kEquipmentAddress - 0x03000000u;
void set16(Ram& m, std::size_t at, std::uint16_t v) { m[at] = std::uint8_t(v); m[at + 1] = std::uint8_t(v >> 8); }
std::size_t iw(unsigned a) { return a - 0x03000000u; }
}  // namespace

int main() {
    namespace rumble = metrozero::rumble;
    Ram ram{};
    const std::array<std::uint8_t, 0x40000> ewram{};
    rumble::Watcher w;

    // A loaded file: 99 of 99 energy, 10 of 10 missiles.
    set16(ram, kEq + 6, 99);
    set16(ram, kEq + 0, 99);
    set16(ram, kEq + 8, 10);
    set16(ram, kEq + 2, 10);
    CHECK(!w.step(ram, ewram));  // first frame only primes
    CHECK(!w.step(ram, ewram));  // nothing changed

    // A hit for 20: a clear pulse on both motors.
    set16(ram, kEq + 6, 79);
    rumble::Pulse p = w.step(ram, ewram);
    CHECK(p && p.low >= 0.6f && p.high > 0);

    // Energy coming back (or a cheat refilling it) never rumbles.
    set16(ram, kEq + 6, 99);
    CHECK(!w.step(ram, ewram));

    // A 1-point drain gives a light buzz.
    set16(ram, kEq + 6, 98);
    p = w.step(ram, ewram);
    CHECK(p && p.strength() < 0.3f);

    // Death: full strength.
    set16(ram, kEq + 6, 0);
    p = w.step(ram, ewram);
    CHECK(p && p.strength() == 1.0f && p.ms >= 500);
    set16(ram, kEq + 6, 99);
    w.step(ram, ewram);

    // A missile: a light tick on the small motor only.
    set16(ram, kEq + 8, 9);
    p = w.step(ram, ewram);
    CHECK(p && p.low == 0 && p.high > 0);

    // A super missile: a firmer tick.
    ram[kEq + 0xA] = 4;
    ram[kEq + 0x4] = 5;
    w.step(ram, ewram);
    ram[kEq + 0xA] = 3;
    p = w.step(ram, ewram);
    CHECK(p && p.low > 0 && p.high >= 0.5f);

    // A bigger energy tank changes the maximum: a different file, so no damage pulse.
    set16(ram, kEq + 6, 50);
    set16(ram, kEq + 0, 199);
    CHECK(!w.step(ram, ewram));

    // The screen starts shaking for 30 frames, then counts down (no new pulse while it does).
    ram[iw(rumble::kScreenShakeY)] = 30;
    p = w.step(ram, ewram);
    CHECK(p && p.low >= 0.5f && p.ms >= 30 * 16);
    ram[iw(rumble::kScreenShakeY)] = 29;
    CHECK(!w.step(ram, ewram));
    ram[iw(rumble::kScreenShakeY)] = 0;
    w.step(ram, ewram);

    // A power bomb going off.
    ram[iw(rumble::kPowerBomb)] = 1;
    p = w.step(ram, ewram);
    CHECK(p && p.low == 1.0f);
    ram[iw(rumble::kPowerBomb)] = 3;
    CHECK(!w.step(ram, ewram));

    // Title screen demo: nothing.
    ram[iw(rumble::kDemoState)] = 1;
    w.step(ram, ewram);
    set16(ram, kEq + 6, 10);
    CHECK(!w.step(ram, ewram));
    ram[iw(rumble::kDemoState)] = 0;
    CHECK(!w.step(ram, ewram));  // the frame the demo ends still compares against demo numbers

    // reset(): a loaded state with less energy is not a hit.
    w.reset();
    set16(ram, kEq + 6, 5);
    CHECK(!w.step(ram, ewram));

    // Mixer: a weaker pulse never cuts a stronger one short; it plays again once that is over.
    rumble::Mixer mix;
    CHECK(mix.take({1.0f, 1.0f, 100}));
    mix.step();
    CHECK(!mix.take({0.0f, 0.35f, 70}));
    for (int i = 0; i < 10; ++i) mix.step();
    CHECK(!mix.playing());
    CHECK(mix.take({0.0f, 0.35f, 70}));
    CHECK(!mix.take({}));

    // Strength scaling.
    CHECK(rumble::motor(1.0f, 100) == 0xFFFF);
    CHECK(rumble::motor(1.0f, 50) == 0x8000);
    CHECK(rumble::motor(0.0f, 100) == 0);
    CHECK(rumble::motor(2.0f, 100) == 0xFFFF);

    if (failures) return 1;
    std::puts("rumble: ok");
    return 0;
}
