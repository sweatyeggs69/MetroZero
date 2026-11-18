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
#include <iterator>

#include "gba.h"

namespace metrozero::gba {

namespace {
constexpr u32 kLayers = 0x0300009C;    // 3 x {u32 map; u16 width; u16 height}
constexpr u32 kTileset = 0x03005450;   // u32 pointer to the block -> 4 entries table
constexpr u32 kCamera = 0x030013B4;    // 4 x {u16 x; u16 y} (gBg0XPosition...), in quarter pixels
constexpr int kMainLayer = 1;
constexpr u32 kRoomEntry = 0x030000BC;  // gCurrentRoomEntry: u8 tileset, u8 bg0Prop, ...
constexpr u16 kBgPropCloseUp = 0x41;    // BG_PROP_CLOSE_UP
constexpr u8 kBgPropRle = 0x10;         // BG_PROP_RLE_COMPRESSED: the layer is a block layer
// BG3CNT while a door transition runs: 512x256, screen block 6, char block 1, priority 0.
constexpr u16 kDoorTransitionBg3Cnt = 0x4604;

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
    auto bg_prop = [&](int layer) {  // gCurrentRoomEntry: u8 tileset, then u8 bg0Prop..bg3Prop
        const u32 a = kRoomEntry + 1 + u32(layer);
        return u8(wide_ram16(a & ~1u) >> ((a & 1) * 8));
    };
    int bg_of_layer[3] = {-1, -1, -1};

    for (int bg = 0; bg < 4; ++bg) {
        const u16 cnt = u16(io_[0x08 + 2 * bg] | (io_[0x09 + 2 * bg] << 8));
        const u32 sbb = (cnt >> 8) & 31;
        // A block layer sits at screen block 2 * layer and is 512x256.
        if (sbb > 4 || (sbb & 1) || ((cnt >> 14) & 3) != 1) continue;
        const int layer = int(sbb / 2);
        if (!(dispcnt & (0x100 << bg)) || bg_of_layer[layer] >= 0) continue;
        bg_of_layer[layer] = bg;
        // Only a layer the room loads as blocks has a current layout in RAM. A room without a
        // block BG0 (or BG2) leaves the previous room's layout and size there and fills the
        // VRAM map with a blank tile, so with few tiles in view a stale layout could still
        // pass the check below and its blocks (the last room's lava, pillars) showed in the
        // side columns (issue #22).
        if (layer != kMainLayer && !(bg_prop(layer) & kBgPropRle)) continue;
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
        // The game's own camera for this layer is the best guess, but it runs up to a frame
        // ahead of VRAM and the scroll registers (the game moves the camera before VBlank
        // copies the new rows and columns), so it rarely equals HOFS/VOFS while scrolling.
        // Take the register-consistent camera nearest to it: when that one matches it wins
        // outright. Only when it does not (title, cutscenes) are the other positions tried,
        // and then only a single match counts: a mostly empty or repetitive layer matches in
        // many places, and taking any of those shows the wrong part of the room in the side
        // columns (issue #13: garbage blocks at the edges while jumping or scrolling).
        const s32 hint_x = s32(wide_ram16(kCamera + u32(layer) * 4)) >> 2;
        const s32 hint_y = s32(wide_ram16(kCamera + u32(layer) * 4 + 2)) >> 2;
        auto nearest = [](s32 hint, s32 reg, s32 period) {
            return hint + ((reg - hint) % period + period + period / 2) % period - period / 2;
        };
        s32 best_x = nearest(hint_x, s32(hofs), 512), best_y = nearest(hint_y, s32(vofs & 0xFF), 256);
        int best = score(best_x, best_y);
        if (best < 0) {
            int found = 0;
            for (s32 cy = s32(vofs & 0xFF); cy < level_h; cy += 256) {
                for (s32 cx = s32(hofs); cx < level_w; cx += 512) {
                    const int match = score(cx, cy);
                    if (match < 0) continue;
                    ++found;
                    best = match;
                    best_x = cx;
                    best_y = cy;
                }
            }
            if (found != 1) best = -1;
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
    // A close-up (the intro's face shot of Samus) is drawn on BG0 over the room. Its picture
    // is only 240 pixels wide and BG0 wraps at 256, so with wide sides the rest of the room
    // showed through and the shoulders repeated at the edges. The room keeps BG0 enabled but
    // fully transparent (alpha 0) when the close-up is not showing.
    if (wide_scene_ && (dispcnt & 0x100) && wide_ram16(kRoomEntry) >> 8 == kBgPropCloseUp) {
        const u16 bldcnt = u16(io_[0x50] | (io_[0x51] << 8));
        const bool faded_out = (bldcnt & 0xC1) == 0x41 && (io_[0x52] & 31) == 0;
        if (!faded_out) wide_scene_ = false;
    }
    if (!wide_scene_) {
        for (auto& L : wide_level_) L.mode = WideMode::Vram;
        // Door transition: the room is blanked and the door tube, the only thing on a black
        // 512x256 BG3 map, slides across the screen. That map is complete (not a ring
        // buffer), so it is drawn wrapped as on hardware instead of being cut at the old edge.
        const u16 bg3cnt = u16(io_[0x0E] | (io_[0x0F] << 8));
        if ((dispcnt & 0x0F00) == 0x0800 && bg3cnt == kDoorTransitionBg3Cnt) {
            wide_scene_ = true;
            wide_hud_prio_ = 0;
        }
        return;
    }
    wide_main_bg_ = bg_of_layer[kMainLayer];
    // The HUD (energy, missiles, minimap) is drawn with priority 0 objects in the top rows;
    // unused objects are parked at x = 255, which is visible in a wide view. Keep both in the
    // 240 view (ppu_render_objects). Other priority 0 objects, such as the Kraid and Ridley statues, are
    // part of the room and continue into the sides.
    wide_hud_prio_ = 0;
    // Block layers that did not match would show stale ring columns: hide their sides.
    for (int layer = 0; layer < 3; ++layer)
        if (bg_of_layer[layer] >= 0 && !matched[layer]) wide_level_[bg_of_layer[layer]].mode = WideMode::Hide;
}

// The game only draws a sprite (enemies, items, doors' lights...) while its position is within
// the 240x160 view plus the sprite's own draw distance (SpriteCheckOnScreen, 0x0800DDD4 in the
// community decompilation). With wide sides, sprites with a small draw distance popped in only
// once they reached the old edge (issue #13: the Morph Ball and Geemers). The patch adds the
// margin to the horizontal draw distance. It needs one more instruction than the original, so
// it drops a 16-bit mask of the camera that the two following uses do not need (one is shifted
// left by 16, the other masked again).
//
//   original                         patched
//   0800DE44 adds r0, r3, r2         adds r3, r3, r2          (camera X + 512, unmasked)
//   0800DE46 lsls r0, r0, #16        ldr  r4, [sp]
//   0800DE48 lsrs r3, r0, #16        adds r2, r4, r2
//   0800DE4A ldr  r4, [sp]           lsls r2, r2, #16
//   0800DE4C adds r2, r4, r2         mov  r0, ip
//   0800DE4E lsls r2, r2, #16        adds r0, #0x29
//   0800DE50 mov  r0, ip             ldrb r0, [r0]            (drawDistanceHorizontal)
//   0800DE52 adds r0, #0x29          lsls r0, r0, #2
//   0800DE54 ldrb r0, [r0]           adds r0, #(margin * 4)   (the new instruction)
//   0800DE56 lsls r0, r0, #2         subs r1, r3, r0
//   0800DE58 subs r1, r3, r0         lsls r1, r1, #16
//   0800DE5A lsls r1, r1, #16        lsrs r4, r2, #16
//   0800DE5C lsrs r4, r2, #16        movs r7, #0xF0
//   0800DE5E movs r7, #0xF0          lsls r7, r7, #2
//   0800DE60 lsls r7, r7, #2         adds r0, r0, r7
//   0800DE62 adds r0, r0, r7         adds r0, r3, r0
//   0800DE64 adds r0, r3, r0         lsls r0, r0, #16
//   0800DE66 lsls r0, r0, #16        lsrs r0, r0, #16
//   0800DE68 lsrs r0, r0, #16        nop
void Gba::wide_sprite_patch(int margin) {
    if (margin == wide_sprite_margin_) return;
    static constexpr u32 kAt = 0x0000DE44;
    static constexpr u16 kOriginal[] = {0x1898, 0x0400, 0x0C03, 0x9C00, 0x18A2, 0x0412, 0x4660, 0x3029, 0x7800, 0x0080,
                                        0x1A19, 0x0409, 0x0C14, 0x27F0, 0x00BF, 0x19C0, 0x1818, 0x0400, 0x0C00};
    static constexpr u16 kPatched[] = {0x189B, 0x9C00, 0x18A2, 0x0412, 0x4660, 0x3029, 0x7800, 0x0080, 0x3000,
                                       0x1A19, 0x0409, 0x0C14, 0x27F0, 0x00BF, 0x19C0, 0x1818, 0x0400, 0x0C00, 0x46C0};
    static_assert(std::size(kOriginal) == std::size(kPatched));
    auto at = [&](std::size_t i) { return u16(rom_[kAt + 2 * i] | (rom_[kAt + 2 * i + 1] << 8)); };
    // Only touch the code when it is one of the two known forms (the ROM is verified, but stay
    // safe if anything else ever changes these bytes).
    bool original = rom_.size() > kAt + sizeof(kOriginal), patched = original;
    for (std::size_t i = 0; i < std::size(kOriginal) && (original || patched); ++i) {
        const u16 want_patched = i == 8 ? u16(0x3000 | (at(i) & 0xFF)) : kPatched[i];
        original = original && at(i) == kOriginal[i];
        patched = patched && at(i) == want_patched;
    }
    if (!original && !patched) return;
    for (std::size_t i = 0; i < std::size(kOriginal); ++i) {
        const u16 v = margin ? (i == 8 ? u16(0x3000 | u16(margin * 4)) : kPatched[i]) : kOriginal[i];
        rom_[kAt + 2 * i] = u8(v);
        rom_[kAt + 2 * i + 1] = u8(v >> 8);
    }
    wide_sprite_margin_ = margin;
}

}  // namespace metrozero::gba
