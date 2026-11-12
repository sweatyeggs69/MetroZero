// Widescreen support specific to Metroid: Zero Mission (USA, BMXE).
//
// A room has three block layers (layer 0 = foreground, 1 = main, 2 = background) that the
// game keeps decompressed in EWRAM, plus a far background (BG3) that is loaded whole into
// VRAM. Only the part of each block layer around the 240-pixel view is copied into its
// 512x256 VRAM tilemap, which acts as a ring buffer. For the extra columns of a widescreen
// view we rebuild tilemap entries straight from RAM, the same way the game's own row and
// column writers do (RoomUpdateHorizontalTilemap / RoomUpdateVerticalTilemap in the community
// decompilation, metroidret/mzm):
//
//   layer L:  map = *(u32*)(0x0300009C + L * 8), width = u16 at +4, height = u16 at +6
//             (both in 16x16 blocks; gBgPointersAndDimensions); block = u16 at
//             map + (by * width + bx) * 2
//   VRAM:     layer L is screen block 2 * L (0x06000000 + L * 0x1000)
//   entry:    4 u16 per block (top left, top right, bottom left, bottom right) at
//             *(u32*)0x03005450 + block * 8 (gTilemapAndClipPointers.pTilemap, which holds the
//             room's tileset followed by the common blocks)
//
// HOFS/VOFS only hold the camera modulo 512/256, so once per frame we find which absolute
// camera position matches what is actually in VRAM by comparing sample tiles of the visible
// area. If nothing matches (title, menus, cutscenes, map screen) that layer is not rebuilt.
// Gameplay is the frames where the main layer (1) matches; only those widen. There, the far
// background wraps as on hardware, and a block layer that does not match is hidden rather
// than showing stale VRAM. Only addresses and layout descriptions belong here; never copy
// game data into the repo.
#include "gba.h"

namespace metrozero::gba {

namespace {
constexpr u32 kLayers = 0x0300009C;    // 3 x {u32 map; u16 width; u16 height}
constexpr u32 kTileset = 0x03005450;   // u32 pointer to the block -> 4 entries table
constexpr u32 kCamera = 0x030013B4;    // 4 x {u16 x; u16 y} (gBg0XPosition...), in quarter pixels
constexpr int kMainLayer = 1;

bool in_ram(u32 a) { return (a >> 24) == 0x02 || (a >> 24) == 0x03; }
}  // namespace

u16 Gba::wide_ram16(u32 a) const {
    if ((a >> 24) == 0x02) return u16(ewram_[a & 0x3FFFF] | (ewram_[(a + 1) & 0x3FFFF] << 8));
    if ((a >> 24) == 0x03) return u16(iwram_[a & 0x7FFF] | (iwram_[(a + 1) & 0x7FFF] << 8));
    if ((a >> 24) >= 0x08 && (a >> 24) <= 0x09) {
        const u32 off = a & 0x01FFFFFE;
        if (off + 1 < rom_.size()) return u16(rom_[off] | (rom_[off + 1] << 8));
    }
    return 0;
}

u32 Gba::wide_ram32(u32 a) const { return u32(wide_ram16(a)) | (u32(wide_ram16(a + 2)) << 16); }

u16 Gba::wide_level_entry(int bg, s32 tx, s32 ty) const {
    const WideLevel& L = wide_level_[bg];
    const u16 block = wide_ram16(L.map + u32(((ty >> 1) * L.width + (tx >> 1)) * 2));
    const u32 quarter = u32(tx & 1) + u32(ty & 1) * 2;
    return wide_ram16(wide_ram32(kTileset) + (u32(block) * 4 + quarter) * 2);
}

void Gba::wide_level_detect() {
    for (auto& L : wide_level_) L = WideLevel{};
    wide_scene_ = false;
    wide_main_bg_ = -1;
    wide_hud_prio_ = -1;
    if (!wide_margin_) return;
    const u16 dispcnt = u16(io_[0] | (io_[1] << 8));
    if ((dispcnt & 7) != 0) return;

    const u32 tileset = wide_ram32(kTileset);
    const bool tileset_ok = in_ram(tileset) || ((tileset >> 24) >= 0x08 && (tileset >> 24) <= 0x09);
    bool matched[3] = {false, false, false};
    int bg_of_layer[3] = {-1, -1, -1};

    for (int bg = 0; bg < 4; ++bg) {
        const u16 cnt = u16(io_[0x08 + 2 * bg] | (io_[0x09 + 2 * bg] << 8));
        const u32 sbb = (cnt >> 8) & 31;
        // A block layer sits at screen block 2 * layer and is 512x256.
        if (sbb > 4 || (sbb & 1) || ((cnt >> 14) & 3) != 1) continue;
        const int layer = int(sbb / 2);
        if (!(dispcnt & (0x100 << bg)) || bg_of_layer[layer] >= 0) continue;
        bg_of_layer[layer] = bg;
        WideLevel& L = wide_level_[bg];
        L.layer = layer;
        L.map = wide_ram32(kLayers + u32(layer) * 8);
        L.width = wide_ram16(kLayers + u32(layer) * 8 + 4);
        L.height = wide_ram16(kLayers + u32(layer) * 8 + 6);
        if (!tileset_ok || (L.map >> 24) != 0x02 || L.width <= 0 || L.height <= 0 || L.width > 256 || L.height > 256)
            continue;
        if ((L.map & 0x3FFFF) + u32(L.width * L.height * 2) > 0x40000) continue;
        const s32 tiles_w = L.width * 2, tiles_h = L.height * 2;

        const u32 hofs = u32(io_[0x10 + 4 * bg] | (io_[0x11 + 4 * bg] << 8)) & 0x1FF;
        const u32 vofs = u32(io_[0x12 + 4 * bg] | (io_[0x13 + 4 * bg] << 8)) & 0x1FF;
        const u32 screen_base = sbb * 0x800;
        const s32 level_w = tiles_w * 8, level_h = tiles_h * 8;
        // Sample tiles spread over the visible area. The best-matching camera wins if at
        // least 15 of every 16 samples agree. The game updates VRAM and the RAM layout together
        // (e.g. broken blocks), so a bigger gap means the game drew something else into the
        // layer; its sides are hidden then.
        static constexpr int kSx[] = {4, 36, 68, 100, 132, 164, 196, 228};
        static constexpr int kSy[] = {12, 60, 108, 148};
        auto score = [&](s32 cx, s32 cy) {
            int checked = 0, match = 0;
            for (int sy : kSy)
                for (int sx : kSx) {
                    const s32 ax = cx + sx, ay = cy + sy;
                    if (ax < 0 || ay < 0 || ax >= level_w || ay >= level_h) continue;
                    const u32 vx = u32(ax >> 3) & 63, vy = u32(ay >> 3) & 31;
                    const u32 off = screen_base + (vx >> 5) * 0x800 + vy * 64 + (vx & 31) * 2;
                    const u16 vram_e = u16(vram_[off] | (vram_[off + 1] << 8));
                    ++checked;
                    if (vram_e == wide_level_entry(bg, ax >> 3, ay >> 3)) ++match;
                }
            return checked >= 16 && match * 16 >= checked * 15 ? match : -1;
        };
        // Try the game's own camera for this layer first (it wins ties, which matters for a
        // mostly empty layer that matches in many places), then every other candidate.
        const s32 hint_x = s32(wide_ram16(kCamera + u32(layer) * 4)) >> 2;
        const s32 hint_y = s32(wide_ram16(kCamera + u32(layer) * 4 + 2)) >> 2;
        int best = -1;
        s32 best_x = 0, best_y = 0;
        if (u32(hint_x & 511) == hofs && u32(hint_y & 255) == (vofs & 255)) {
            best = score(hint_x, hint_y);
            best_x = hint_x;
            best_y = hint_y;
        }
        for (s32 cy = s32(vofs & 0xFF); cy < level_h; cy += 256) {
            for (s32 cx = s32(hofs); cx < level_w; cx += 512) {
                const int match = score(cx, cy);
                if (match > best) {
                    best = match;
                    best_x = cx;
                    best_y = cy;
                }
            }
        }
        if (best < 0) continue;
        L.tiles_w = tiles_w;
        L.tiles_h = tiles_h;
        L.cam_x = best_x - s32(hofs);
        L.cam_y = best_y - s32(vofs);
        L.mode = WideMode::Level;
        matched[layer] = true;
    }

    wide_scene_ = matched[kMainLayer];
    if (!wide_scene_) {
        for (auto& L : wide_level_) L.mode = WideMode::Vram;
        return;
    }
    wide_main_bg_ = bg_of_layer[kMainLayer];
    // The HUD (energy, missiles, minimap) is drawn with priority 0 objects; unused objects
    // are parked at x = 255, which is visible in a wide view. Keep both in the 240 view.
    wide_hud_prio_ = 0;
    // Block layers that did not match would show stale ring columns: hide their sides.
    for (int layer = 0; layer < 3; ++layer)
        if (bg_of_layer[layer] >= 0 && !matched[layer]) wide_level_[bg_of_layer[layer]].mode = WideMode::Hide;
}

}  // namespace metrozero::gba
