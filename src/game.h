#pragma once
// The one place that describes the game MetroZero runs. Everything game-specific that the rest of
// the code needs (names shown to the user, ROM fingerprint, save layout) lives here, so the
// launcher, packaging and tests do not hard-code it.
//
// Target: Metroid: Zero Mission, USA release (AGB-BMXE), revision 0.
//
// Values marked TODO(verify) must be checked against your own dump before the first release.
// Nothing here is game data: these are only identifiers and a fingerprint of the user's own ROM.

#include <cstddef>
#include <string_view>

namespace metrozero::game {

// Shown in the launcher, dialogs and error messages.
inline constexpr const char* kTitle = "Metroid: Zero Mission";
inline constexpr const char* kRegion = "USA";
inline constexpr const char* kTitleWithRegion = "Metroid: Zero Mission (USA)";
// Used for shortcuts, default export names, etc. (no characters that are invalid in file names).
inline constexpr const char* kFileSafeTitle = "Metroid - Zero Mission";

// GBA cartridge header of the supported dump.
inline constexpr const char* kHeaderTitle = "ZEROMISSIONE";  // 0xA0, 12 bytes
inline constexpr const char* kGameCode = "BMXE";             // 0xAC, 4 bytes
inline constexpr unsigned kRevision = 0;                     // 0xBC
inline constexpr std::size_t kRomSize = 8 * 1024 * 1024;

// SHA-1 of the known-good dump (lowercase hex), checked against the maintainer's own cartridge
// dump on 2026-11-12 (MD5 ebbce58109988b6da61ebb06c7a432d5, CRC32 5C61A844). It is also the
// "us" ROM the community decompilation (metroidret/mzm) builds. If this is ever emptied, a ROM is
// accepted on a header match instead and the launcher labels it "header match (fingerprint not
// pinned yet)".
inline constexpr const char* kExpectedSha1 = "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8";

// Battery save: 32 KiB SRAM (the ROM carries the SRAM_V113 library string).
inline constexpr std::size_t kSaveSize = 32 * 1024;
// Bytes the game writes at SRAM offset kSaveHeaderOffset, used to reject saves from other games.
// Empty disables the check. The game writes its file-select options block at SRAM offset 0 on
// first boot: a checksum and its inverse (8 bytes, they change), a few counters, then this
// version string (SaveFileScreenOptions.ZeroMissionVer_Text in the community decompilation).
// Checked on 2026-11-12 against a fresh first-boot save.
inline constexpr std::size_t kSaveHeaderOffset = 0x1C;
inline constexpr std::string_view kSaveHeader{"ZeroMissionUSAver005"};

// Samus's equipment in IWRAM, used by the cheats menu. Layout from the community decompilation
// (struct Equipment at gEquipment); the address matches the well-known GameShark codes:
//   +0 u16 max energy   +2 u16 max missiles   +4 u8 max super missiles   +5 u8 max power bombs
//   +6 u16 energy       +8 u16 missiles       +A u8 super missiles       +B u8 power bombs
//   +C u8 beam/bomb flags  +D activation  +E u8 suit/misc flags  +F activation
inline constexpr unsigned kEquipmentAddress = 0x03001530;
// +0xE/+0xF u8 suit/misc ability flags (collected / switched on); bit 6 is the Morph Ball.
inline constexpr unsigned kSuitMorphBall = 1u << 6;

// Samus's state (struct SamusData at gSamusData in the community decompilation). +0 u8 pose,
// +0x12 u16 x, +0x14 u16 y (feet), in 1/4-pixel units.
inline constexpr unsigned kSamusDataAddress = 0x030013D4;

}  // namespace metrozero::game
