// SPDX-License-Identifier: GPL-3.0-or-later

#include "Roms.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

#include "Sha1.hpp"
#include "ZipReader.hpp"

namespace ostrich {

namespace fs = std::filesystem;

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

bool knownSize(uintmax_t size)
{
    for (const Known& k : kKnown)
        if (k.size == size)
            return true;
    return false;
}

FILE* openFile(const fs::path& p, const char* mode)
{
#ifdef _WIN32
    const wchar_t* wm = mode[0] == 'w' ? L"wb" : L"rb";
    return _wfopen(p.c_str(), wm);
#else
    return std::fopen(p.c_str(), mode);
#endif
}

bool readAll(const fs::path& p, size_t size, std::vector<uint8_t>& out)
{
    FILE* f = openFile(p, "rb");
    if (!f)
        return false;
    out.resize(size);
    const bool ok = std::fread(out.data(), 1, size, f) == size;
    std::fclose(f);
    return ok;
}

bool writeAll(const fs::path& p, const std::vector<uint8_t>& data)
{
    FILE* f = openFile(p, "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

std::string u8(const fs::path& p)
{
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

fs::path fromU8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string baseName(const std::string& member)
{
    // Windows PowerShell 5's Compress-Archive writes folders with '\'.
    const size_t cut = member.find_last_of("/\\");
    return cut == std::string::npos ? member : member.substr(cut + 1);
}

bool isZip(const fs::path& p)
{
    std::string ext = u8(p.extension());
    for (char& c : ext)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".zip";
}

fs::path platformFolder()
{
#ifdef _WIN32
    const char* pd = std::getenv("ProgramData");
    return fs::path(std::string(pd && *pd ? pd : "C:\\ProgramData")) / "Ostrich 7765" / "roms";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x)
        return fs::path(x) / "Ostrich 7765" / "roms";
    const char* h = std::getenv("HOME");
    return fs::path(h && *h ? h : ".") / ".local" / "share" / "Ostrich 7765" / "roms";
#endif
}

bool completeIn(const std::array<bool, kNumRomImages>& have, uint32_t machine)
{
    switch (machine) {
    case 0: return have[kRom505Raw] || have[kRom505PinOrder] || have[kRom505Decoded];
    case 1: return have[kRom626];
    case 2: return have[kRom707IC34] && have[kRom707IC35] && have[kRom707IC19] && have[kRom707IC22];
    default: return false;
    }
}

} // namespace

std::vector<std::string> RomLibrary::folders()
{
    std::vector<std::string> out;
    if (const char* e = std::getenv("OSTRICH_ROMS"); e && *e)
        out.push_back(e);
    // getenv gives the folders in the system's narrow encoding, as path() expects.
    out.push_back(platformFolder().string());
    return out;
}

std::string RomLibrary::installFolder()
{
    return u8(platformFolder());
}

void RomLibrary::scan()
{
    for (auto& i : images_)
        i.clear();
    for (auto& p : paths_)
        p.clear();
    for (auto& m : members_)
        m.clear();
    for (const std::string& folder : folders())
        scanFolder(fs::path(folder));
}

void RomLibrary::scanFolder(const fs::path& dir)
{
    std::error_code ec;
    std::vector<fs::path> zips;
    std::vector<uint8_t> data;
    fs::directory_iterator it(dir, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_regular_file(ec2))
            continue;
        if (isZip(it->path())) {
            zips.push_back(it->path());
            continue;
        }
        const auto size = it->file_size(ec2);
        if (ec2 || !knownSize(size) || !readAll(it->path(), size_t(size), data))
            continue;
        take(data, it->path(), std::string());
    }
    std::sort(zips.begin(), zips.end());
    for (const fs::path& z : zips)
        scanFile(z);
}

bool RomLibrary::scanFile(const fs::path& file, std::string* why)
{
    const std::string name = u8(file.filename());
    std::vector<uint8_t> data;
    if (isZip(file)) {
        bool any = false;
        for (const zip::Entry& e : zip::list(file))
            if (knownSize(e.size) && zip::extract(file, e, data))
                any |= take(data, file, e.name);
        if (!any && why)
            *why = name + ": no known ROM image in this zip";
        return any;
    }
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) {
        if (why)
            *why = name + ": cannot be read";
        return false;
    }
    if (!knownSize(size)) {
        if (why)
            *why = name + ": " + std::to_string(size) + " bytes, not the size of a known ROM image";
        return false;
    }
    if (!readAll(file, size_t(size), data)) {
        if (why)
            *why = name + ": cannot be read";
        return false;
    }
    const std::string sha = sha1Hex(data.data(), data.size());
    if (!take(data, file, std::string())) {
        if (why)
            *why = name + ": not a known dump (SHA-1 " + sha + ")";
        return false;
    }
    return true;
}

bool RomLibrary::take(std::vector<uint8_t>& data, const fs::path& from, const std::string& member)
{
    const std::string sha = sha1Hex(data.data(), data.size());
    bool known = false;
    for (const Known& k : kKnown)
        if (k.size == data.size() && sha == k.sha1) {
            known = true;
            if (!has(k.image)) {
                images_[k.image] = data;
                paths_[k.image] = u8(from);
                members_[k.image] = member;
            }
        }
    return known;
}

std::string RomLibrary::shownName(RomImage i) const
{
    const std::string file = u8(fromU8(paths_[i]).filename());
    return members_[i].empty() ? file : file + " : " + baseName(members_[i]);
}

bool RomLibrary::complete(uint32_t machine) const
{
    std::array<bool, kNumRomImages> have{};
    for (int i = 0; i < kNumRomImages; ++i)
        have[size_t(i)] = has(RomImage(i));
    return completeIn(have, machine);
}

int RomLibrary::install(const RomLibrary& found, std::string& error) const
{
    std::array<bool, kNumRomImages> have{};
    for (int i = 0; i < kNumRomImages; ++i)
        have[size_t(i)] = has(RomImage(i));
    const fs::path dir = platformFolder();
    int copied = 0;
    for (int n = 0; n < kNumRomImages; ++n) {
        const auto i = RomImage(n);
        if (!found.has(i) || have[size_t(n)] || completeIn(have, kRomMachine[n]))
            continue;
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string name = found.members_[n].empty()
            ? u8(fromU8(found.paths_[n]).filename()) : baseName(found.members_[n]);
        // A different file of that name already there: keep it, write beside it.
        fs::path to = dir / fromU8(name);
        for (int k = 2; fs::exists(to, ec) && k < 100; ++k)
            to = dir / fromU8(u8(fromU8(name).stem()) + " (" + std::to_string(k) + ")"
                                  + u8(fromU8(name).extension()));
        if (!writeAll(to, found.images_[n])) {
            error = "could not write " + u8(to);
            continue;
        }
        have[size_t(n)] = true;
        ++copied;
    }
    return copied;
}

} // namespace ostrich
