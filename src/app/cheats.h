#pragma once
// Cheats menu toggles (F1 > Cheats). After every game frame the host calls apply() on the game's
// IWRAM, which tops the chosen supplies back up to their current maximum. Nothing is written while
// a maximum is still 0 (before a file is loaded or the item is found). Only the counts change,
// never the weapon flags, so a cheat never unlocks missiles or bombs early. Off while
// RetroAchievements hardcore mode is on.
//
// Settings (metrozero::Config): cheats.health, cheats.missiles, cheats.super_missiles,
// cheats.power_bombs (all off by default).
#include <cstddef>
#include <cstdint>
#include <span>

#include "game.h"

namespace metrozero::cheats {

struct Flags {
    bool health = false;
    bool missiles = false;
    bool super_missiles = false;
    bool power_bombs = false;
    bool any() const { return health || missiles || super_missiles || power_bombs; }
};

// Offsets into struct Equipment (see game::kEquipmentAddress).
inline constexpr std::size_t kMaxEnergy = 0x0, kMaxMissiles = 0x2, kMaxSupers = 0x4, kMaxPowerBombs = 0x5;
inline constexpr std::size_t kEnergy = 0x6, kMissiles = 0x8, kSupers = 0xA, kPowerBombs = 0xB;
inline constexpr std::size_t kEquipmentSize = 0xC;  // the counts; the flags after them are never written

namespace detail {
inline std::uint16_t get16(std::span<const std::uint8_t> m, std::size_t at) {
    return std::uint16_t(m[at] | (m[at + 1] << 8));
}
inline void set16(std::span<std::uint8_t> m, std::size_t at, std::uint16_t v) {
    m[at] = std::uint8_t(v);
    m[at + 1] = std::uint8_t(v >> 8);
}
}  // namespace detail

// Refills the enabled supplies in iwram (the 32 KiB IWRAM, starting at 0x03000000). Returns true
// when anything changed.
inline bool apply(std::span<std::uint8_t> iwram, const Flags& f) {
    const std::size_t base = game::kEquipmentAddress - 0x03000000u;
    if (!f.any() || iwram.size() < base + kEquipmentSize) return false;
    bool changed = false;
    auto refill16 = [&](std::size_t cur, std::size_t max) {
        const std::uint16_t m = detail::get16(iwram, base + max);
        if (m != 0 && detail::get16(iwram, base + cur) != m) {
            detail::set16(iwram, base + cur, m);
            changed = true;
        }
    };
    auto refill8 = [&](std::size_t cur, std::size_t max) {
        const std::uint8_t m = iwram[base + max];
        if (m != 0 && iwram[base + cur] != m) {
            iwram[base + cur] = m;
            changed = true;
        }
    };
    if (f.health) refill16(kEnergy, kMaxEnergy);
    if (f.missiles) refill16(kMissiles, kMaxMissiles);
    if (f.super_missiles) refill8(kSupers, kMaxSupers);
    if (f.power_bombs) refill8(kPowerBombs, kMaxPowerBombs);
    return changed;
}

}  // namespace metrozero::cheats
