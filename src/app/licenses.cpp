#include "app/licenses.h"

#include <cstdio>

extern const unsigned char metrozero_text_license[];
extern const unsigned metrozero_text_license_size;
extern const unsigned char metrozero_text_notices[];
extern const unsigned metrozero_text_notices_size;
extern const unsigned char metrozero_text_dmsans_ofl[];
extern const unsigned metrozero_text_dmsans_ofl_size;

namespace metrozero::licenses {

namespace {
std::string_view view(const unsigned char* p, unsigned n) { return {reinterpret_cast<const char*>(p), n}; }
}  // namespace

std::string_view metrozero() { return view(metrozero_text_license, metrozero_text_license_size); }
std::string_view notices() { return view(metrozero_text_notices, metrozero_text_notices_size); }
std::string_view font_ofl() { return view(metrozero_text_dmsans_ofl, metrozero_text_dmsans_ofl_size); }

void print_all() {
    for (std::string_view t : {metrozero(), notices(), font_ofl()}) {
        std::fwrite(t.data(), 1, t.size(), stdout);
        std::fputs("\n\n", stdout);
    }
}

}  // namespace metrozero::licenses
