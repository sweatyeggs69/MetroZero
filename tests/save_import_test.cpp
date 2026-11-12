// Tests for the save importer with synthetic buffers (no real save data).
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "app/save_import.h"

namespace {
int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

using Buf = std::vector<std::uint8_t>;

Buf fake_sram() {
    Buf b(metrozero::kSramSize, 0xFF);
    std::memcpy(b.data() + metrozero::game::kSaveHeaderOffset, metrozero::game::kSaveHeader.data(),
                metrozero::game::kSaveHeader.size());
    b[0x40] = 0x01;  // some non-blank data even while the header is not set
    for (int i = 0; i < 116; ++i) b[0x200 + i] = std::uint8_t(i * 7);
    return b;
}
}  // namespace

int main() {
    using metrozero::normalize_save;
    const Buf sram = fake_sram();

    {  // raw 32 KiB
        auto r = normalize_save(sram, "ZeroMission.sav");
        CHECK(r.ok);
        CHECK(r.data == sram);
        CHECK(r.warning.empty());
    }
    {  // RetroArch .srm with a 16-byte RTC trailer
        Buf b = sram;
        b.insert(b.end(), 16, 0x12);
        auto r = normalize_save(b, "ZeroMission.srm");
        CHECK(r.ok);
        CHECK(r.data == sram);
    }
    {  // 64 KiB padded with 0xFF
        Buf b = sram;
        b.insert(b.end(), metrozero::kSramSize, 0xFF);
        auto r = normalize_save(b, "x.sav");
        CHECK(r.ok && r.data == sram);
    }
    {  // 128 KiB mirrored
        Buf b;
        for (int i = 0; i < 4; ++i) b.insert(b.end(), sram.begin(), sram.end());
        auto r = normalize_save(b, "x.sav");
        CHECK(r.ok && r.data == sram);
        CHECK(r.format.find("mirrored") != std::string::npos);
    }
    {  // 64 KiB with real data in the second half (a Flash save from another game)
        Buf b = sram;
        Buf other(metrozero::kSramSize, 0x55);
        b.insert(b.end(), other.begin(), other.end());
        auto r = normalize_save(b, "x.sav");
        CHECK(!r.ok);
    }
    {  // DeSmuME .dsv footer
        Buf b = sram;
        const char* footer = "|<--Snip above here to create a raw sav by excluding this DeSmuME savedata footer:";
        b.insert(b.end(), footer, footer + std::strlen(footer));
        b.insert(b.end(), 24, 0x01);
        auto r = normalize_save(b, "x.dsv");
        CHECK(r.ok && r.data == sram);
    }
    {  // save states are rejected
        CHECK(!normalize_save(sram, "ZeroMission.ss1").ok);
        CHECK(!normalize_save(sram, "ZeroMission.state").ok);
        CHECK(!normalize_save(sram, "ZeroMission.state3").ok);
        Buf b = sram;
        std::memcpy(b.data(), "RASTATE", 7);
        CHECK(!normalize_save(b, "x.sav").ok);
    }
    {  // EEPROM sizes and junk sizes are rejected
        CHECK(!normalize_save(Buf(8192, 0), "x.sav").ok);
        CHECK(!normalize_save(Buf(512, 0), "x.sav").ok);
        CHECK(!normalize_save(Buf(1000, 0), "x.sav").ok);
        CHECK(!normalize_save(Buf(), "x.sav").ok);
    }
    {  // Zero Mission's save signature: the version string after the options block's checksum
        using metrozero::game::kSaveHeader;
        using metrozero::game::kSaveHeaderOffset;
        CHECK(kSaveHeader == "ZeroMissionUSAver005");
        Buf ok = fake_sram();
        CHECK(metrozero::has_game_header(ok));
        CHECK(normalize_save(ok, "x.sav").ok);
        {  // the checksum in front of it changes with every save, so it does not count
            Buf other_sum = ok;
            other_sum[0] ^= 0x5A;
            CHECK(metrozero::has_game_header(other_sum));
        }
        for (std::size_t at : {kSaveHeaderOffset, kSaveHeaderOffset + 11, kSaveHeaderOffset + kSaveHeader.size() - 1}) {
            Buf bad = ok;
            bad[at] ^= 0x01;
            CHECK(!metrozero::has_game_header(bad));
            CHECK(!normalize_save(bad, "x.sav").ok);
        }
    }
    {  // another game's 32 KiB SRAM is rejected; a blank one is accepted with a warning
        Buf other(metrozero::kSramSize, 0x00);
        std::memcpy(other.data(), "OTHERGAME!", 10);
        CHECK(!normalize_save(other, "x.sav").ok);
        auto blank = normalize_save(Buf(metrozero::kSramSize, 0xFF), "x.sav");
        CHECK(blank.ok && !blank.warning.empty());
    }
    {  // backups: newest first, pruned to `keep`
        namespace fs = std::filesystem;
        fs::path dir = fs::temp_directory_path() / "metrozero_save_test";
        fs::remove_all(dir);
        fs::create_directories(dir / "backups");
        fs::path save = dir / "abc.sav";
        CHECK(metrozero::write_file_atomic(save, sram));
        for (int i = 0; i < 13; ++i) CHECK(!metrozero::backup_save(save, dir / "backups", 10).empty());
        auto list = metrozero::list_backups(dir / "backups", "abc");
        CHECK(list.size() == 10);
        Buf back;
        CHECK(metrozero::read_file(list.front(), back) && back == sram);
        fs::remove_all(dir);
    }

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("save import tests passed\n");
    return 0;
}
