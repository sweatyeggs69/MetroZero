// MetroZero's colors for the shared launcher theme (theme.h): near-black blue-tinted surfaces, the
// bright cyan of the MetroZero logo as the accent and its yellow outline as the focus ring. This
// is the only theme file that differs between ports.
#include "app/theme.h"

namespace metrozero::theme {

namespace {
ImVec4 rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }
}  // namespace

const Palette& palette() {
    static const Palette p{
        rgb(10, 13, 20),            // surface
        rgb(16, 20, 30),            // card
        rgb(24, 30, 44),            // raised
        rgb(34, 42, 60),            // high
        rgb(150, 180, 225, 0.14f),  // line
        rgb(240, 246, 252),         // ink
        rgb(160, 176, 198),         // ink_muted
        rgb(104, 118, 140),         // ink_faint
        rgb(26, 173, 255),          // accent
        rgb(110, 205, 255),         // accent_hover
        rgb(0, 140, 230),           // accent_active
        rgb(26, 173, 255, 0.15f),   // accent_soft
        rgb(6, 14, 26),             // on_accent
        rgb(255, 230, 0),           // highlight
        rgb(52, 211, 153),          // good
        rgb(248, 113, 113),         // bad
        rgb(20, 90, 170),           // glow
    };
    return p;
}

}  // namespace metrozero::theme
