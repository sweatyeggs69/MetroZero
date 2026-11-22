#pragma once
// Controller rumble (Settings > Controls > Rumble). The GBA has no rumble, so Watcher reads the
// game's RAM after every frame and turns what just happened into a Pattern; the host plays it on
// every connected controller (input::Manager::rumble), scaled by the strength setting.
//
// Events, all read from Metroid: Zero Mission's RAM (addresses worked out from the variable order
// and struct sizes in the community decompilation, which lines up with the known gEquipment,
// gCurrentRoomEntry and gBgPointersAndDimensions addresses):
//   - Samus takes damage: gEquipment (IWRAM 0x03001530) +0x6 u16 energy drops. Stronger
//     for bigger hits; heat, cold and acid that drain a little every few frames give a light buzz.
//   - Samus dies: energy reaches 0.
//   - the screen shakes: gScreenShakeY / gScreenShakeX (IWRAM 0x03000110 / 0x03000114,
//     struct { u8 timer; u8 delay; u8 intensity; u8 direction; }). The game shakes the screen for
//     explosions, heavy landings, bosses and the like, so the controller shakes with it.
//   - a power bomb goes off: gCurrentPowerBomb (IWRAM 0x03000130) +0 u8 state turns non-zero.
//   - a missile is fired: +0x8 u16 missiles drops (a light tick), or a super missile: +0xA u8
//     (a firmer one).
// Nothing fires during the title screen demos (gDemoState non-zero). gDemoState is a u8 just
// before gSamusData: IWRAM 0x030013D2 by the layout, or 0x030013D1 if the unused array before it
// sits one byte lower. Both bytes are 0 outside demos, so both are checked.
//
// Settings (metrozero::Config): input.rumble (on by default), input.rumble_strength (10-100,
// default 75).
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>

#include "game.h"

namespace metrozero::rumble {

inline constexpr bool kDefaultOn = true;
inline constexpr int kDefaultStrength = 75;
inline constexpr int kMinStrength = 10;
// Shown next to the Controller rumble toggle.
inline constexpr const char* kEventsHelp =
    "Shakes your controller when Samus is hurt, when the screen shakes (explosions, bosses, "
    "heavy landings), when a power bomb goes off and, lightly, when she fires a missile. "
    "Works with controllers that have rumble motors.";

// ---- Patterns (the same in every port) ----
//
// A rumble is a Pattern: up to kMaxSteps steps played one after another. Each step holds the two
// motors at a strength from 0 to 1 for some milliseconds (low = the big, slow motor in the left
// grip that gives deep thumps, high = the small, fast one in the right grip that gives sharp
// buzzes); a step with both at 0 is a pause. The strength setting scales every step.
//
// The table, at 100% strength (each game picks the ones it has):
//   light hit     one crisp 80 ms buzz, mostly the small motor
//   heavy hit     a deep 150 ms thump and a 150 ms tail, harder the more health it took
//   death         three falling beats: full, full, then a long fade (about 1.4 s)
//   drain         a faint 50 ms tick on the big motor, at most twice a second (heat, acid)
//   screen shake  a rolling rumble (70 ms on, 50 ms softer) for as long as the screen shakes
//   power bomb    a building swell, a full blast, then a slow fade (about 1.3 s)
//   missile       a tiny 60 ms tick on the small motor
//   super missile a firmer kick and a smaller echo
//   heavy landing one short low thump
// When two overlap, the one with the higher priority plays; a missile tick never cuts a death
// short, but a death interrupts anything.

struct Step {
    float low = 0, high = 0;
    int ms = 0;
};

struct Pattern {
    static constexpr int kMaxSteps = 24;
    std::array<Step, kMaxSteps> steps{};
    int count = 0;
    int priority = 0;

    constexpr Pattern() = default;
    constexpr Pattern(int prio, std::initializer_list<Step> s) : priority(prio) {
        for (const Step& x : s) add(x);
    }
    constexpr bool add(Step s) {
        if (count >= kMaxSteps || s.ms <= 0) return false;
        steps[std::size_t(count++)] = s;
        return true;
    }
    explicit constexpr operator bool() const { return count > 0 && strength() > 0; }
    constexpr int total_ms() const {
        int t = 0;
        for (int i = 0; i < count; ++i) t += steps[std::size_t(i)].ms;
        return t;
    }
    constexpr float peak_low() const {
        float v = 0;
        for (int i = 0; i < count; ++i) v = std::max(v, steps[std::size_t(i)].low);
        return v;
    }
    constexpr float peak_high() const {
        float v = 0;
        for (int i = 0; i < count; ++i) v = std::max(v, steps[std::size_t(i)].high);
        return v;
    }
    constexpr float strength() const { return std::max(peak_low(), peak_high()); }
};

// The same shape, every step's motors times f (capped at 1).
constexpr Pattern scaled(Pattern p, float f) {
    for (int i = 0; i < p.count; ++i) {
        Step& s = p.steps[std::size_t(i)];
        s.low = std::clamp(s.low * f, 0.0f, 1.0f);
        s.high = std::clamp(s.high * f, 0.0f, 1.0f);
    }
    return p;
}

// Whichever of two patterns that start on the same frame wins: the higher priority, then the
// stronger, then the longer.
constexpr Pattern stronger(const Pattern& a, const Pattern& b) {
    if (!a) return b;
    if (!b) return a;
    if (a.priority != b.priority) return a.priority > b.priority ? a : b;
    if (a.strength() != b.strength()) return a.strength() > b.strength() ? a : b;
    return a.total_ms() >= b.total_ms() ? a : b;
}

namespace patterns {
inline constexpr int kDrainPriority = 10, kTickPriority = 20, kShakePriority = 40, kHitPriority = 60,
                     kBombPriority = 80, kDeathPriority = 100;

inline constexpr Pattern kLightHit{kHitPriority, {{0.30f, 0.65f, 80}}};
inline constexpr Pattern kHeavyHit{kHitPriority + 5, {{0.95f, 0.55f, 150}, {0.40f, 0.10f, 150}}};
inline constexpr Pattern kDeath{kDeathPriority,
                                {{1.0f, 1.0f, 250},
                                 {0.0f, 0.0f, 90},
                                 {1.0f, 0.6f, 250},
                                 {0.0f, 0.0f, 90},
                                 {0.75f, 0.25f, 350},
                                 {0.35f, 0.0f, 350}}};
inline constexpr Pattern kDrain{kDrainPriority, {{0.18f, 0.0f, 50}}};
inline constexpr Pattern kMissile{kTickPriority, {{0.0f, 0.45f, 60}}};
inline constexpr Pattern kSuperMissile{kTickPriority + 5, {{0.45f, 0.65f, 80}, {0.0f, 0.0f, 40}, {0.25f, 0.30f, 60}}};
inline constexpr Pattern kPowerBomb{kBombPriority,
                                    {{0.20f, 0.0f, 120},
                                     {0.45f, 0.05f, 120},
                                     {1.0f, 0.9f, 350},
                                     {0.70f, 0.30f, 250},
                                     {0.40f, 0.10f, 250},
                                     {0.20f, 0.0f, 250}}};
inline constexpr Pattern kLanding{kTickPriority + 10, {{0.55f, 0.15f, 90}}};
// Played by the Test rumble button: a light hit, a pause, then a heavy one.
inline constexpr Pattern kTest{kDeathPriority,
                               {{0.30f, 0.65f, 80}, {0.0f, 0.0f, 250}, {0.95f, 0.55f, 150}, {0.40f, 0.10f, 150}}};
}  // namespace patterns

// A hit that took `lost` of `max` health: a light buzz for a scratch (under a tenth of the bar),
// a heavy thump for anything bigger, harder the more it took.
inline Pattern hit(int lost, int max) {
    const float share = max > 0 ? std::clamp(float(lost) / float(max), 0.0f, 1.0f) : 0.0f;
    if (share < 0.10f) return scaled(patterns::kLightHit, 0.75f + share * 2.5f);
    return scaled(patterns::kHeavyHit, std::min(1.0f, 0.55f + share));
}

// The screen shakes for about `ms` milliseconds at `power` (0..1): a rolling rumble as long as
// the shake (120 ms to 1.2 s).
inline Pattern shake(int ms, float power) {
    const float f = std::clamp(power, 0.35f, 1.0f);
    Pattern p;
    p.priority = patterns::kShakePriority;
    for (int left = std::clamp(ms, 120, 1200); left > 0;) {
        if (!p.add({f, f * 0.3f, std::min(70, left)})) break;
        left -= 70;
        if (left <= 0 || !p.add({f * 0.55f, 0.0f, std::min(50, left)})) break;
        left -= 50;
    }
    return p;
}

// Plays one pattern at a time, stepped once per emulated frame. A new pattern replaces the one
// playing only when its priority is at least as high.
class Player {
public:
    // Starts p now if it should play. True when it did.
    bool start(const Pattern& p) {
        if (!p) return false;
        if (active_ && p.priority < cur_.priority) return false;
        cur_ = p;
        index_ = 0;
        elapsed_ = 0;
        active_ = true;
        fresh_ = true;
        return true;
    }
    // Moves on one frame. When the motors need a new command it is returned: the step to play and
    // how long (a little longer than the step when another follows, so there is no gap between
    // frames), or {0, 0, 0} to stop when the pattern is over.
    std::optional<Step> next() {
        if (!active_) return std::nullopt;
        bool changed = fresh_;
        fresh_ = false;
        if (!changed) {
            elapsed_ += kFrameMs;
            while (index_ < cur_.count && elapsed_ >= cur_.steps[std::size_t(index_)].ms) {
                elapsed_ -= cur_.steps[std::size_t(index_)].ms;
                ++index_;
                changed = true;
            }
        }
        if (index_ >= cur_.count) {
            active_ = false;
            return Step{0, 0, 0};
        }
        if (!changed) return std::nullopt;
        Step s = cur_.steps[std::size_t(index_)];
        s.ms -= elapsed_;
        if (index_ + 1 < cur_.count && (s.low > 0 || s.high > 0)) s.ms += kOverlapMs;
        if (s.low <= 0 && s.high <= 0) s.ms = 0;
        return s;
    }
    bool playing() const { return active_; }
    void clear() { active_ = false; }

private:
    static constexpr int kFrameMs = 17;  // one GBA frame (16.74 ms)
    static constexpr int kOverlapMs = 40;
    Pattern cur_;
    int index_ = 0, elapsed_ = 0;
    bool active_ = false, fresh_ = false;
};

// Strength setting (percent) -> motor value for SDL (0..0xFFFF).
inline std::uint16_t motor(float level, int strength_percent) {
    const float v = std::clamp(level, 0.0f, 1.0f) * std::clamp(strength_percent, 0, 100) / 100.0f;
    return std::uint16_t(v * 65535.0f + 0.5f);
}

namespace detail {
inline std::uint16_t get16(std::span<const std::uint8_t> m, std::size_t at) {
    return std::uint16_t(m[at] | (m[at + 1] << 8));
}
}  // namespace detail

// ---- Metroid: Zero Mission ----

inline constexpr unsigned kScreenShakeY = 0x03000110;
inline constexpr unsigned kScreenShakeX = 0x03000114;
inline constexpr unsigned kPowerBomb = 0x03000130;
inline constexpr unsigned kDemoState = 0x030013D1;  // and the byte after it, see above

class Watcher {
public:
    // Forgets the last frame, so a jump in the numbers (a loaded state, rewind, a new game) is
    // not taken for an event.
    void reset() { primed_ = false; }

    // Call after each emulated frame with the 32 KiB IWRAM (0x03000000) and 256 KiB EWRAM.
    Pattern step(std::span<const std::uint8_t> iwram, std::span<const std::uint8_t> /*ewram*/) {
        using detail::get16;
        constexpr std::size_t iw = 0x03000000u;
        const std::size_t eq = game::kEquipmentAddress - iw;
        if (iwram.size() < 0x8000) return {};
        Frame now;
        now.energy = get16(iwram, eq + 0x6);
        now.max_energy = get16(iwram, eq + 0x0);
        now.missiles = get16(iwram, eq + 0x8);
        now.max_missiles = get16(iwram, eq + 0x2);
        now.supers = iwram[eq + 0xA];
        now.max_supers = iwram[eq + 0x4];
        now.shake_y = iwram[kScreenShakeY - iw];
        now.shake_x = iwram[kScreenShakeX - iw];
        now.shake_power = std::max(iwram[kScreenShakeY - iw + 2], iwram[kScreenShakeX - iw + 2]);
        now.power_bomb = iwram[kPowerBomb - iw];
        now.demo = (iwram[kDemoState - iw] | iwram[kDemoState - iw + 1]) != 0;

        const Frame before = last_;
        const bool primed = primed_;
        last_ = now;
        primed_ = true;
        if (since_drain_ < 1000) ++since_drain_;
        if (!primed || now.demo || before.demo) return {};

        Pattern out;
        // Same file (the maximum did not change) and energy went down.
        if (now.max_energy == before.max_energy && now.max_energy != 0 && now.energy < before.energy) {
            const int lost = before.energy - now.energy;
            if (now.energy == 0) out = stronger(out, patterns::kDeath);
            else if (lost <= 2) {  // heat / cold / acid drain: a faint tick, at most twice a second
                if (since_drain_ >= 30) {
                    out = stronger(out, patterns::kDrain);
                    since_drain_ = 0;
                }
            }
            else out = stronger(out, hit(lost, now.max_energy));
        }
        // A shake starts (or is extended): its timer goes up.
        const int shake = std::max(now.shake_y > before.shake_y ? now.shake_y : 0,
                                   now.shake_x > before.shake_x ? now.shake_x : 0);
        if (shake > 0) {
            out = stronger(out, rumble::shake(shake * 17, std::min(1.0f, 0.5f + now.shake_power * 0.1f)));
        }
        if (now.power_bomb != 0 && before.power_bomb == 0) out = stronger(out, patterns::kPowerBomb);
        if (now.max_missiles == before.max_missiles && now.missiles < before.missiles &&
            before.missiles - now.missiles <= 2)
            out = stronger(out, patterns::kMissile);
        if (now.max_supers == before.max_supers && now.supers < before.supers && before.supers - now.supers <= 2)
            out = stronger(out, patterns::kSuperMissile);
        return out;
    }

private:
    struct Frame {
        std::uint16_t energy = 0, max_energy = 0, missiles = 0, max_missiles = 0;
        std::uint8_t supers = 0, max_supers = 0;
        std::uint8_t shake_y = 0, shake_x = 0, shake_power = 0, power_bomb = 0;
        bool demo = false;
    };
    Frame last_;
    bool primed_ = false;
    int since_drain_ = 1000;  // frames since the last drain tick
};

}  // namespace metrozero::rumble
