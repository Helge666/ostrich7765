// SPDX-License-Identifier: GPL-3.0-or-later

#include "SoundOrder.hpp"

namespace ostrich {

namespace {

constexpr uint32_t kSlot = 0x2000;

// Raw TR-505 dump to pin order: pin-order data bit i = raw bit kDataBits[i], pin-order
// address bit i = raw address bit kAddrBits[i].
constexpr int kDataBits[8] = {7, 6, 5, 0, 1, 2, 4, 3};
constexpr int kAddrBits[17] = {0, 12, 7, 6, 9, 11, 10, 2, 3, 4, 8, 5, 1, 13, 14, 15, 16};

std::vector<uint8_t> rawToPin(const std::vector<uint8_t>& raw)
{
    uint8_t lut[256];
    for (int v = 0; v < 256; ++v) {
        int o = 0;
        for (int i = 0; i < 8; ++i)
            o |= (v >> kDataBits[i] & 1) << i;
        lut[v] = uint8_t(o);
    }
    std::vector<uint8_t> pin(raw.size());
    for (uint32_t a = 0; a < pin.size(); ++a) {
        uint32_t r = 0;
        for (int i = 0; i < 17; ++i)
            r |= (a >> i & 1u) << kAddrBits[i];
        pin[a] = lut[raw[r]];
    }
    return pin;
}

// One 128 KiB bank, pin order to sound order (TR-505 layout). slot7Single: the TR-626's
// slot 7 holds one sound instead of two.
void bankToSound(const uint8_t* pin, uint8_t* out, bool flipA0, bool slot7Single)
{
    for (uint32_t a = 0; a < 0x20000; ++a) {
        uint32_t src;
        if (a < 0x8000) {
            src = flipA0 ? a ^ 1u : a;                          // slots 0-3: one sound each
        } else if (a < 0x10000) {
            const uint32_t base = a & ~(kSlot - 1), k = a & (kSlot - 1);
            if (slot7Single && base == 0xE000)
                src = a;
            else                                                // two sounds, even / odd
                src = base + (k < 0x1000 ? 2 * k : 2 * (k - 0x1000) + 1);
        } else if (a < 0x18000) {                               // slots 8-11, slot bits fastest
            const uint32_t k = a - 0x10000;
            src = 0x10000 + ((k & 1) << 13 | (k >> 1 & 1) << 14 | k >> 2);
        } else if (a < 0x1C000) {                               // slots 12-13, likewise
            const uint32_t k = a - 0x18000;
            src = 0x18000 + ((k & 1) << 13 | k >> 1);
        } else {
            src = a;                                            // slots 14, 15
        }
        out[a] = pin[src];
    }
}

} // namespace

std::vector<uint8_t> tr505SoundOrder(const std::vector<uint8_t>& dump, bool raw)
{
    if (dump.size() != 0x20000)
        return {};
    const std::vector<uint8_t> pin = raw ? rawToPin(dump) : dump;
    std::vector<uint8_t> out(0x20000);
    bankToSound(pin.data(), out.data(), true, false);
    return out;
}

std::vector<uint8_t> tr626SoundOrder(const std::vector<uint8_t>& dump)
{
    if (dump.size() != 0x40000)
        return {};
    std::vector<uint8_t> pin(dump.size());
    for (uint32_t a = 0; a < pin.size(); ++a)                   // undo the A6/A8 swap
        pin[a] = dump[(a & ~0x140u) | (a >> 8 & 1u) << 6 | (a >> 6 & 1u) << 8];
    std::vector<uint8_t> out(dump.size());
    for (uint32_t b = 0; b < 2; ++b)
        bankToSound(pin.data() + b * 0x20000, out.data() + b * 0x20000, false, true);
    return out;
}

} // namespace ostrich
