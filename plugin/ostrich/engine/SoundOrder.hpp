// SPDX-License-Identifier: GPL-3.0-or-later
//
// The TR-505's and TR-626's sound ROMs brought into sound order (every sound contiguous), as
// tools/tr505_rom.py and tools/tr626_rom.py do; analysis/TR-505.md, TR-626.md.

#pragma once

#include <cstdint>
#include <vector>

namespace ostrich {

// TR-505, 128 KiB: from the raw dump (raw = true) or the pin-order one. The sound-order
// form needs nothing.
std::vector<uint8_t> tr505SoundOrder(const std::vector<uint8_t>& dump, bool raw);

// TR-626, 256 KiB: from the known dump (A6/A8 swapped).
std::vector<uint8_t> tr626SoundOrder(const std::vector<uint8_t>& dump);

constexpr const char* kTr505SoundOrderSha1 = "70f622f07fd94bcbfdb731f303ab4e8df845469f";
constexpr const char* kTr626SoundOrderSha1 = "b61f9ceae9c8570c48ce6d53ead6d5ee1bfc187b";

} // namespace ostrich
