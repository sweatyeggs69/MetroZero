<p align="center">
  <img src="assets/icon/metrozero-icon.png" alt="MetroZero app icon" width="128">
</p>

<p align="center">
  <img src="assets/logo/metrozero-logo.png" alt="MetroZero" width="520">
</p>

<p align="center">
  Play <b>Metroid: Zero Mission</b> natively on macOS, Windows and Linux.
</p>

<p align="center">
  <a href="https://github.com/sweatyeggs69/MetroZero/actions/workflows/build.yml"><img alt="Build" src="https://github.com/sweatyeggs69/MetroZero/actions/workflows/build.yml/badge.svg"></a>
  <a href="https://github.com/sweatyeggs69/MetroZero/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/sweatyeggs69/MetroZero?include_prereleases&label=release"></a>
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-blue"></a>
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-macOS%20%7C%20Windows%20%7C%20Linux-5b8def">
</p>

> [!IMPORTANT]
> MetroZero does not include the game. You need your own legally dumped ROM of
> **Metroid: Zero Mission (USA)**. MetroZero is an unofficial fan project, not affiliated with, endorsed or
> sponsored by Nintendo. Metroid and all related names are trademarks of their respective owners.

## Download

<a href="https://recomplauncher.com/game/metrozero"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg"></a>

Or get the latest version for your system from the [Releases page](https://github.com/sweatyeggs69/MetroZero/releases/latest).

## Getting started

1. **Open MetroZero** and click **Choose ROM...** to pick your dump. Only the USA release (game code
   `BMXE`, revision 0) is supported; other regions, revisions, hacks and damaged dumps are refused.
   MetroZero only remembers where the file is. It never copies, uploads or changes it.
2. **BIOS (optional).** No BIOS is needed. If you own a GBA BIOS dump, pick it in Settings > Game > BIOS
   to see the original boot animation.
3. **Press Play.** Next time, **Continue** picks up where you left off.

## Controls

| GBA      | Keyboard                  | Controller                        |
| -------- | ------------------------- | --------------------------------- |
| D-pad    | Arrow keys                | D-pad or left stick               |
| A        | X                         | A (bottom) or Y (top) face button |
| B        | Z                         | B (right) or X (left) face button |
| L / R    | A / S                     | Shoulder buttons or triggers      |
| Start    | Enter                     | Start                             |
| Select   | Backspace or Right Shift  | Back                              |

| Shortcut                | Keyboard            | Controller                |
| ----------------------- | ------------------- | ------------------------- |
| Settings menu           | F1 or Esc           |                           |
| Fullscreen              | F11                 |                           |
| Save state / load state | Page Up / Page Down | Back + R / Back + L       |
| Save to a new slot      | F9                  | Back + D-pad Up           |
| Previous / next slot    | F6 / F7             | Back + D-pad Left / Right |
| Fast forward (hold)     | Tab                 | Right stick click         |
| Rewind (hold)           | `` ` `` (backtick)    | Left stick click          |

Everything except F1, Esc and F11 can be rebound in F1 > Controls.

F1 > Controls also has Metroid extras, off until you set them up: a missile toggle, diagonal aim
buttons, and aiming with the right stick.

## Features

- **Settings menu (F1)** for display (scaling, 16:10 and 16:9 widescreen, screen effects), controls and more.
- **Keyboard and controllers**, with full remapping.
- **Save states**: ten slots with thumbnails. Quick save and load use Slot 1 unless you pick another.
- **Fast forward and rewind**: fast forward at 2x or 4x, rewind up to the last 10 seconds.
- **Cheats** (F1 > Cheats/Mods): unlimited health, missiles, super missiles and power bombs.
- **Auto Morph Ball** (F1 > Cheats/Mods > Mods): run into a Morph Ball tunnel and Samus rolls in on her own.
- **Redux Mode** (F1 > Cheats/Mods): optional quality-of-life tweaks, on together or one at a time: faster elevators,
  an accurate clock, bomb jumps in liquids, a Morph Ball that rests when still, and no low health alarm. Inspired by
  [Metroid Zero Mission Redux](https://github.com/ShadowOne333/Metroid-Zero-Mission-Redux) by ShadowOne333; applied
  in memory, so your ROM file is never changed.
- **RetroAchievements**, optional. Cheats, mods and rewind are off in hardcore mode.
- **Import saves** from RetroArch, mGBA, VBA-M and others.
- **Desktop shortcut** that starts the game directly (Settings > Game > Shortcut).
- **Automatic updates** from this repository's releases, with your OK, without touching your saves.

## Saves

Saves, save states and settings live in your user folder, never inside the app:

- macOS: `~/Library/Application Support/MetroZero/MetroZero/`
- Windows: `%APPDATA%\MetroZero\MetroZero\`
- Linux: `~/.local/share/MetroZero/MetroZero/`

Settings > States > **Show save folder** opens it.

## Building from source

You need CMake 3.24 or newer and a C++20 compiler. SDL3, Dear ImGui and the other libraries are
downloaded during configure. On Linux, install SDL3's usual build packages first (the Linux job in
[build.yml](.github/workflows/build.yml) lists them).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/metrozero
```

## Contributing

Bug reports and pull requests are welcome. Keep game content out of the repository: no ROMs, BIOS
files, saves, save states, extracted assets or screenshots of the game.

## License

[MIT](LICENSE). Bundled open-source components and fonts are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
