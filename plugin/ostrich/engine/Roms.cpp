// SPDX-License-Identifier: GPL-3.0-or-later

#include "Roms.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "Sha1.hpp"

namespace ostrich {

namespace {

struct Known {
    RomImage image;
    size_t size;
    const char* sha1;
};

// roms/README.md.
constexpr Known kKnown[] = {
    {kRom707IC34, 0x8000, "fd2bfe67d4d03d2b2134aa7feebe9167c44b1f8d"},
    {kRom707IC35, 0x8000, "5cc866f345906d817147ae2a61bc36d7be926511"},
    {kRom707IC19, 0x8000, "965e23ad71e1f95d56307fa67272725dff46ba67"},
    {kRom707IC22, 0x8000, "6c7c0f002ed66e4ccf182a4538d9bb239623ac43"},
    {kRom505Raw, 0x20000, "6441d3e7b53aff4511b23021dc854b7a5cc57689"},
    {kRom505PinOrder, 0x20000, "14e80b926aed1e78cb027752dfbbe51d9b3dcf4d"},
    {kRom505Decoded, 0x20000, "70f622f07fd94bcbfdb731f303ab4e8df845469f"},
    {kRom626, 0x40000, "d7891edecc68a0652d2cad82a637430ee8568818"},
};

bool readAll(const std::filesystem::path& p, size_t size, std::vector<uint8_t>& out)
{
#ifdef _WIN32
    FILE* f = _wfopen(p.c_str(), L"rb");
#else
    FILE* f = std::fopen(p.c_str(), "rb");
#endif
    if (!f)
        return false;
    out.resize(size);
    const bool ok = std::fread(out.data(), 1, size, f) == size;
    std::fclose(f);
    return ok;
}

} // namespace

std::vector<std::string> RomLibrary::folders()
{
    std::vector<std::string> out;
    if (const char* e = std::getenv("OSTRICH_ROMS"); e && *e)
        out.push_back(e);
#ifdef _WIN32
    const char* pd = std::getenv("ProgramData");
    out.push_back(std::string(pd && *pd ? pd : "C:\\ProgramData") + "\\Ostrich 7765\\roms");
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x)
        out.push_back(std::string(x) + "/Ostrich 7765/roms");
    else if (const char* h = std::getenv("HOME"); h && *h)
        out.push_back(std::string(h) + "/.local/share/Ostrich 7765/roms");
#endif
    return out;
}

void RomLibrary::scan()
{
    for (auto& i : images_)
        i.clear();
    for (auto& p : paths_)
        p.clear();

    namespace fs = std::filesystem;
    std::vector<uint8_t> data;
    for (const std::string& folder : folders()) {
        std::error_code ec;
        // getenv gives the folders in the system's narrow encoding, as path() expects.
        fs::directory_iterator it(fs::path(folder), ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code ec2;
            if (!it->is_regular_file(ec2))
                continue;
            const auto size = size_t(it->file_size(ec2));
            if (ec2)
                continue;
            bool wanted = false;
            for (const Known& k : kKnown)
                wanted |= k.size == size && !has(k.image);
            if (!wanted || !readAll(it->path(), size, data))
                continue;
            const std::string sha = sha1Hex(data.data(), size);
            for (const Known& k : kKnown)
                if (k.size == size && !has(k.image) && sha == k.sha1) {
                    images_[k.image] = data;
                    const auto u8 = it->path().u8string();
                    paths_[k.image].assign(u8.begin(), u8.end());
                }
        }
    }
}

} // namespace ostrich
