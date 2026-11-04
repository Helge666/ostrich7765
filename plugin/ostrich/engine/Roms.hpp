// SPDX-License-Identifier: GPL-3.0-or-later
//
// The user's ROM images, found in the ROM folders (roms/README.md) and recognised by their
// SHA-1, whatever the files are called.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ostrich {

enum RomImage {
    kRom707IC34, kRom707IC35, kRom707IC19, kRom707IC22,
    kRom505Raw, kRom505PinOrder, kRom505Decoded,
    kRom626,
    kNumRomImages
};

class RomLibrary
{
public:
    // The folders searched, in order: $OSTRICH_ROMS, then the platform's ROM folder.
    static std::vector<std::string> folders();

    // Reads every folder; files of an unknown size or content are skipped. Not real-time
    // safe (it reads files).
    void scan();

    bool has(RomImage i) const { return !images_[i].empty(); }
    const std::vector<uint8_t>& image(RomImage i) const { return images_[i]; }
    const std::string& path(RomImage i) const { return paths_[i]; }

private:
    std::array<std::vector<uint8_t>, kNumRomImages> images_;
    std::array<std::string, kNumRomImages> paths_;
};

} // namespace ostrich
