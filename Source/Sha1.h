#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <array>

// WebSocketハンドシェイク用の最小SHA-1
namespace pm
{
inline std::array<uint8_t, 20> sha1 (const std::string& msg)
{
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::string m = msg;
    const uint64_t bitLen = (uint64_t) msg.size() * 8;
    m.push_back ((char) 0x80);
    while (m.size() % 64 != 56)
        m.push_back (0);
    for (int i = 7; i >= 0; --i)
        m.push_back ((char) ((bitLen >> (i * 8)) & 0xff));

    auto rol = [] (uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };

    for (size_t off = 0; off < m.size(); off += 64)
    {
        uint32_t w[80];
        for (size_t i = 0; i < 16; ++i)
            w[i] = ((uint32_t) (uint8_t) m[off + i * 4] << 24) | ((uint32_t) (uint8_t) m[off + i * 4 + 1] << 16)
                 | ((uint32_t) (uint8_t) m[off + i * 4 + 2] << 8) | (uint32_t) (uint8_t) m[off + i * 4 + 3];
        for (int i = 16; i < 80; ++i)
            w[i] = rol (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i)
        {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
            const uint32_t t = rol (a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol (b, 30); b = a; a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::array<uint8_t, 20> out {};
    const uint32_t hs[5] = { h0, h1, h2, h3, h4 };
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j)
            out[(size_t) (i * 4 + j)] = (uint8_t) (hs[i] >> (24 - j * 8));
    return out;
}
} // namespace pm
