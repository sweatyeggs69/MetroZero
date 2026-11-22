// Rumble: which changes in a blank IWRAM image turn into controller rumble patterns. No ROM needed.
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
    rumble::Pattern p = w.step(ram, ewram);
    CHECK(p && p.peak_low() >= 0.6f && p.peak_high() > 0);

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
    CHECK(p && p.strength() == 1.0f && p.total_ms() >= 500);
    set16(ram, kEq + 6, 99);
    w.step(ram, ewram);

    // A missile: a light tick on the small motor only.
    set16(ram, kEq + 8, 9);
    p = w.step(ram, ewram);
    CHECK(p && p.peak_low() == 0 && p.peak_high() > 0);

    // A super missile: a firmer tick.
    ram[kEq + 0xA] = 4;
    ram[kEq + 0x4] = 5;
    w.step(ram, ewram);
    ram[kEq + 0xA] = 3;
    p = w.step(ram, ewram);
    CHECK(p && p.peak_low() > 0 && p.peak_high() >= 0.5f);

    // A bigger energy tank changes the maximum: a different file, so no damage pulse.
    set16(ram, kEq + 6, 50);
    set16(ram, kEq + 0, 199);
    CHECK(!w.step(ram, ewram));

    // The screen starts shaking for 30 frames, then counts down (no new pulse while it does).
    ram[iw(rumble::kScreenShakeY)] = 30;
    p = w.step(ram, ewram);
    CHECK(p && p.peak_low() >= 0.5f && p.total_ms() >= 30 * 16);
    ram[iw(rumble::kScreenShakeY)] = 29;
    CHECK(!w.step(ram, ewram));
    ram[iw(rumble::kScreenShakeY)] = 0;
    w.step(ram, ewram);

    // A power bomb going off.
    ram[iw(rumble::kPowerBomb)] = 1;
    p = w.step(ram, ewram);
    CHECK(p && p.peak_low() == 1.0f);
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

    // Player: steps through a pattern a frame at a time, and a lower priority pattern never cuts a
    // higher one short; it plays again once that is over.
    {
        rumble::Player pl;
        CHECK(pl.start(rumble::patterns::kDeath));
        auto s = pl.next();
        CHECK(s && s->low == 1.0f && s->high == 1.0f && s->ms > 250);  // first beat, with overlap
        CHECK(!pl.start(rumble::patterns::kMissile));
        int commands = 1, frames = 0;
        bool paused = false;
        while (pl.playing() && frames < 200) {
            if (auto c = pl.next()) {
                ++commands;
                if (c->low == 0 && c->high == 0 && pl.playing()) paused = true;
            }
            ++frames;
        }
        CHECK(paused);                          // the pauses between beats stop the motors
        CHECK(commands == 7);                   // six steps, then the final stop
        CHECK(frames >= 75 && frames <= 90);    // about 1.4 s of game frames
        CHECK(!pl.playing());
        CHECK(pl.start(rumble::patterns::kMissile));
        CHECK(pl.start(rumble::patterns::kDeath));  // a death interrupts anything
        CHECK(!pl.start({}));
    }

    // The pattern table: different events feel different.
    {
        namespace pt = rumble::patterns;
        CHECK(pt::kMissile.peak_low() == 0 && pt::kMissile.peak_high() > 0);  // small motor only
        CHECK(pt::kDeath.total_ms() > pt::kHeavyHit.total_ms());
        CHECK(pt::kPowerBomb.strength() == 1.0f && pt::kPowerBomb.steps[0].low < 0.5f);  // builds up
        const rumble::Pattern scratch = rumble::hit(1, 100), big = rumble::hit(40, 100);
        CHECK(scratch.count == 1 && big.count == 2 && big.strength() > scratch.strength());
        const rumble::Pattern sh = rumble::shake(600, 0.8f);
        CHECK(sh.count > 4 && sh.total_ms() == 600 && sh.priority == pt::kShakePriority);
        CHECK(rumble::shake(5000, 1.0f).total_ms() == 1200);
    }

    // Strength scaling.
    CHECK(rumble::motor(1.0f, 100) == 0xFFFF);
    CHECK(rumble::motor(1.0f, 50) == 0x8000);
    CHECK(rumble::motor(0.0f, 100) == 0);
    CHECK(rumble::motor(2.0f, 100) == 0xFFFF);

    if (failures) return 1;
    std::puts("rumble: ok");
    return 0;
}
