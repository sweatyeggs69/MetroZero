// Widescreen sprite range: the patch to the game's sprite on-screen check goes in with the
// margin, follows margin changes and comes out again, and leaves other code alone.
#include <cstdio>
#include <vector>

#include "gba/gba.h"

using namespace metrozero::gba;

static int failures = 0;
#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

// The original instructions at 0x0800DE44..0x0800DE69 (see wide_level.cpp).
static const u16 kOriginal[] = {0x1898, 0x0400, 0x0C03, 0x9C00, 0x18A2, 0x0412, 0x4660, 0x3029, 0x7800, 0x0080,
                                0x1A19, 0x0409, 0x0C14, 0x27F0, 0x00BF, 0x19C0, 0x1818, 0x0400, 0x0C00};
constexpr std::size_t kAt = 0xDE44;

static u16 at(Gba& g, std::size_t i) { return u16(g.rom_mut()[kAt + 2 * i] | (g.rom_mut()[kAt + 2 * i + 1] << 8)); }

static bool is_original(Gba& g) {
    for (std::size_t i = 0; i < std::size(kOriginal); ++i)
        if (at(g, i) != kOriginal[i]) return false;
    return true;
}

int main() {
    std::vector<u8> rom(0x10000, 0);
    for (std::size_t i = 0; i < std::size(kOriginal); ++i) {
        rom[kAt + 2 * i] = u8(kOriginal[i]);
        rom[kAt + 2 * i + 1] = u8(kOriginal[i] >> 8);
    }
    {
        Gba g(rom, "");
        CHECK(is_original(g));
        g.set_wide_margin(22);
        CHECK(!is_original(g));
        CHECK(at(g, 0) == 0x189B);           // adds r3, r3, r2
        CHECK(at(g, 8) == (0x3000 | 88));    // adds r0, #(22 * 4)
        CHECK(at(g, 18) == 0x46C0);          // nop
        g.set_wide_margin(8);
        CHECK(at(g, 8) == (0x3000 | 32));
        g.set_wide_margin(8, false);         // hardcore: original code, wide picture
        CHECK(is_original(g));
        CHECK(g.wide_margin() == 8);
        g.set_wide_margin(22);
        g.set_wide_margin(0);
        CHECK(is_original(g));
    }
    {
        // Unknown bytes there: never touched.
        std::vector<u8> other = rom;
        other[kAt + 4] ^= 0xFF;
        Gba g(other, "");
        g.set_wide_margin(22);
        CHECK(g.rom_mut()[kAt + 4] == other[kAt + 4]);
        CHECK(at(g, 8) == kOriginal[8]);
    }
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("wide sprite patch: ok\n");
    return 0;
}
