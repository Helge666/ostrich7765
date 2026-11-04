// SPDX-License-Identifier: GPL-3.0-or-later
//
// The user's ROM images, found in the ROM folders (roms/README.md) and recognised by their
// SHA-1, whatever the files are called: loose files, or entries of zips (MAME's sets).
// The panel's ROM setup picks a file anywhere; what its folder holds and the ROM folders
// lack is copied into the install folder (install()).

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ostrich {

enum RomImage {
    kRom707IC34, kRom707IC35, kRom707IC19, kRom707IC22,
    kRom505Raw, kRom505PinOrder, kRom505Decoded,
    kRom626,
    kNumRomImages
};

// Which machine (OstrichShared.hpp's Machine) an image belongs to.
constexpr uint32_t kRomMachine[kNumRomImages] = { 2, 2, 2, 2, 0, 0, 0, 1 };

class RomLibrary
{
public:
    // The folders searched, in order: $OSTRICH_ROMS, then the platform's ROM folder.
    static std::vector<std::string> folders();
    // Where install() copies to: the platform's ROM folder (UTF-8).
    static std::string installFolder();

    // Reads every folder (scanFolder). Not real-time safe (it reads files).
    void scan();
    // Adds what one folder holds: loose files of a known size, then the entries of its zips;
    // an image already found is kept. Files of an unknown size or content are skipped.
    void scanFolder(const std::filesystem::path& dir);
    // Adds what one file is: an image, or a zip's entries. False if nothing known is in it;
    // then `why` says what it is (size, SHA-1, or "no known image in the zip").
    bool scanFile(const std::filesystem::path& file, std::string* why = nullptr);

    bool has(RomImage i) const { return !images_[i].empty(); }
    const std::vector<uint8_t>& image(RomImage i) const { return images_[i]; }
    // The file it came from (UTF-8), and for a zip the entry's name, else empty.
    const std::string& path(RomImage i) const { return paths_[i]; }
    const std::string& member(RomImage i) const { return members_[i]; }
    // What the panel shows: the file's name, or "<zip> : <entry>".
    std::string shownName(RomImage i) const;

    // A machine has all it needs: the TR-707 its four images, the TR-505 one of its three
    // forms, the TR-626 its one. The TR-727: no dumps known.
    bool complete(uint32_t machine) const;

    // Writes the images `found` has and this library lacks (and that its machine still
    // needs) into installFolder(), as plain files under their own names (an entry of a zip
    // under its entry name). Returns how many; `error` is set if a write failed.
    int install(const RomLibrary& found, std::string& error) const;

private:
    // True if data is a known image (kept unless one is already there).
    bool take(std::vector<uint8_t>& data, const std::filesystem::path& from, const std::string& member);

    std::array<std::vector<uint8_t>, kNumRomImages> images_;
    std::array<std::string, kNumRomImages> paths_, members_;
};

} // namespace ostrich
