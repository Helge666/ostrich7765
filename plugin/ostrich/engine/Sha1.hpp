// SPDX-License-Identifier: GPL-3.0-or-later
//
// SHA-1 (FIPS 180-4), only to recognise ROM images by their contents.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ostrich {

inline std::string sha1Hex(const uint8_t* data, size_t size)
{
    uint32_t h[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
    auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    auto block = [&](const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 |
                   uint32_t(p[4 * i + 2]) << 8 | uint32_t(p[4 * i + 3]);
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu; }
            else { f = b ^ c ^ d; k = 0xca62c1d6u; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    };

    size_t i = 0;
    for (; i + 64 <= size; i += 64)
        block(data + i);
    uint8_t tail[128] = {};
    const size_t rest = size - i;
    for (size_t j = 0; j < rest; ++j)
        tail[j] = data[i + j];
    tail[rest] = 0x80;
    const size_t len = rest < 56 ? 64 : 128;
    const uint64_t bits = uint64_t(size) * 8;
    for (int j = 0; j < 8; ++j)
        tail[len - 1 - j] = uint8_t(bits >> (8 * j));
    block(tail);
    if (len == 128)
        block(tail + 64);

    char hex[41];
    for (int j = 0; j < 5; ++j)
        std::snprintf(hex + 8 * j, 9, "%08x", unsigned(h[j]));
    return std::string(hex, 40);
}

} // namespace ostrich
