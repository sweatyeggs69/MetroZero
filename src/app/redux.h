#pragma once
// Redux Mode (Settings > Cheats/Mods): optional quality-of-life tweaks inspired by ShadowOne333's
// Metroid Zero Mission Redux and the community hacks it collects. These are MetroZero's own
// versions for the USA ROM, written against the Metroid: Zero Mission decompilation
// (metroidret/mzm). Nothing from the Redux patch files is used.
//
// Two kinds of tweak:
//  - ROM tweaks change a few bytes of the game's code in the emulator's copy of the ROM (the file on
//    disk is never touched). Each patch lists the bytes it expects to replace, so a tweak that does
//    not match the ROM is reported unavailable instead of corrupting it. sync() applies and reverts
//    them between frames, so they can be switched while playing.
//  - Host tweaks adjust IWRAM between frames, like the cheats do.
//
// Settings (metrozero::Config): redux.<id> for every tweak below (all off by default). All tweaks
// are off while RetroAchievements hardcore mode is on.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "app/i18n.h"

namespace metrozero::redux {

struct Patch {
    std::uint32_t address;  // GBA address in the ROM area (0x08000000 + file offset)
    std::uint8_t size;
    std::array<std::uint8_t, 8> original;
    std::array<std::uint8_t, 8> replacement;
};

enum class Kind { Rom, Host };

struct Tweak {
    const char* id;     // config key suffix
    const char* label;  // shown in the menu
    const char* help;   // one line under the checkbox
    Kind kind;
    std::span<const Patch> patches;  // Kind::Rom only
};

namespace detail {
// USA (BMXE, rev 0) addresses. Function names are the decompilation's.

// SamusCheckPlayLowHealthSound: the "low health" test always takes the branch that skips the beep.
inline constexpr Patch kLowHealthAlarm[] = {
    {0x0800B872, 2, {0x0F, 0xD0}, {0x0F, 0xE0}},  // beq 0x0800B894 -> b 0x0800B894
};
// InGameTimerUpdate: a second is 60 frames (the game counts 64).
inline constexpr Patch kAccurateClock[] = {
    {0x080579D0, 2, {0x3F, 0x28}, {0x3B, 0x28}},  // cmp r0,#63 -> cmp r0,#59
};
// SamusBombBounce: drop the "slowed by liquid" early return, so bombs launch the Morph Ball in
// water and lava without the Gravity Suit too.
inline constexpr Patch kBombJumpLiquids[] = {
    {0x08007956, 2, {0x1E, 0xD1}, {0xC0, 0x46}},  // bne (in liquid) -> nop
};
// SamusCopyData, SPOSE_USING_AN_ELEVATOR: 4 pixels per frame instead of 2, both directions.
inline constexpr Patch kFastElevators[] = {
    {0x08007464, 2, {0x08, 0x20}, {0x10, 0x20}},                        // movs r0,#8 -> #16
    {0x08007478, 4, {0xF8, 0xFF, 0x00, 0x00}, {0xF0, 0xFF, 0x00, 0x00}},  // -8 -> -16
};
}  // namespace detail

inline constexpr Tweak kTweaks[] = {
    {"fast_elevators", N_("Faster elevators"), N_("Elevators move twice as fast."), Kind::Rom, detail::kFastElevators},
    {"accurate_clock", N_("Accurate in-game clock"), N_("A second is 60 frames, so the clock matches real time."), Kind::Rom, detail::kAccurateClock},
    {"bomb_jump_liquids", N_("Bomb jumps in liquids"), N_("Bombs launch the Morph Ball in water and lava, even without the Gravity Suit."), Kind::Rom, detail::kBombJumpLiquids},
    {"morph_idle", N_("Morph Ball rests when still"), N_("The Morph Ball only rolls while it is moving."), Kind::Host, {}},
    {"no_low_health_alarm", N_("No low health alarm"), N_("Turns off the beeping when energy is low."), Kind::Rom, detail::kLowHealthAlarm},
};
inline constexpr std::size_t kTweakCount = sizeof(kTweaks) / sizeof(kTweaks[0]);

// Which tweaks are on, indexed like kTweaks.
using Flags = std::array<bool, kTweakCount>;

inline constexpr std::size_t index_of(const char* id) {
    for (std::size_t i = 0; i < kTweakCount; ++i) {
        const char* a = kTweaks[i].id;
        const char* b = id;
        while (*a && *a == *b) ++a, ++b;
        if (*a == *b) return i;
    }
    return kTweakCount;
}

namespace detail {
inline bool matches(std::span<const std::uint8_t> rom, const Patch& p, const std::array<std::uint8_t, 8>& bytes) {
    const std::size_t off = p.address - 0x08000000u;
    if (off + p.size > rom.size()) return false;
    for (std::size_t i = 0; i < p.size; ++i)
        if (rom[off + i] != bytes[i]) return false;
    return true;
}
}  // namespace detail

// True when every patch of a ROM tweak finds the bytes it expects in the unmodified ROM. Host
// tweaks are always available.
inline bool available(std::span<const std::uint8_t> clean_rom, const Tweak& t) {
    for (const Patch& p : t.patches)
        if (!detail::matches(clean_rom, p, p.original)) return false;
    return true;
}

// Applies the ROM tweaks in `want` and reverts the others, in the emulator's ROM. `ok` is
// available() for each tweak, worked out once from the unmodified ROM; unavailable ones are never
// written. Returns true when anything changed. Call between frames.
inline bool sync(std::span<std::uint8_t> rom, const Flags& want, const Flags& ok) {
    bool changed = false;
    for (std::size_t i = 0; i < kTweakCount; ++i) {
        const Tweak& t = kTweaks[i];
        if (t.kind != Kind::Rom || !ok[i]) continue;
        for (const Patch& p : t.patches) {
            const auto& bytes = want[i] ? p.replacement : p.original;
            if (detail::matches(rom, p, bytes)) continue;
            const std::size_t off = p.address - 0x08000000u;
            for (std::size_t k = 0; k < p.size; ++k) rom[off + k] = bytes[k];
            changed = true;
        }
    }
    return changed;
}

// IWRAM addresses (offsets from 0x03000000) used by the host tweaks.
inline constexpr std::size_t kSamusPose = 0x13D4 + 0x00;         // gSamusData.pose
inline constexpr std::size_t kSamusAnimCounter = 0x13D4 + 0x1C;  // gSamusData.animationDurationCounter
inline constexpr std::size_t kSamusAnimFrame = 0x13D4 + 0x1D;    // gSamusData.currentAnimationFrame
inline constexpr std::uint8_t kPoseMorphBall = 17;               // SPOSE_MORPH_BALL (still; rolling is 18)

// Host tweaks. Call before_frame() just before running a frame and after_frame() right after it,
// with the keys held for that frame.
class Host {
public:
    void before_frame(std::span<std::uint8_t> iwram, const Flags& want, std::uint16_t keys) {
        (void)want;
        (void)keys;
        if (iwram.size() < 0x8000) return;
        pose_ = iwram[kSamusPose];
        frame_ = iwram[kSamusAnimFrame];
    }

    void after_frame(std::span<std::uint8_t> iwram, const Flags& want) {
        if (iwram.size() < 0x8000) return;
        // A still Morph Ball keeps its current frame instead of rolling in place. Only while the
        // ball was already still before this frame, so the frame it stops on is kept.
        if (want[index_of("morph_idle")] && pose_ == kPoseMorphBall && iwram[kSamusPose] == kPoseMorphBall) {
            iwram[kSamusAnimFrame] = frame_;
            iwram[kSamusAnimCounter] = 0;
        }
    }

private:
    std::uint8_t pose_ = 0, frame_ = 0;
};

}  // namespace metrozero::redux
