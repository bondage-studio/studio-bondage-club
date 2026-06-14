#include "common/base64.hpp"

#include <array>

namespace sbc {

namespace {
constexpr char kEnc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::array<int, 256> make_dec_table() {
    std::array<int, 256> t{};
    t.fill(-1);
    for (int i = 0; i < 64; ++i) t[static_cast<unsigned char>(kEnc[i])] = i;
    return t;
}
}  // namespace

std::string base64_encode(std::string_view data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= data.size()) {
        unsigned n = (static_cast<unsigned char>(data[i]) << 16) |
                     (static_cast<unsigned char>(data[i + 1]) << 8) |
                     static_cast<unsigned char>(data[i + 2]);
        out.push_back(kEnc[(n >> 18) & 63]);
        out.push_back(kEnc[(n >> 12) & 63]);
        out.push_back(kEnc[(n >> 6) & 63]);
        out.push_back(kEnc[n & 63]);
        i += 3;
    }
    std::size_t rem = data.size() - i;
    if (rem == 1) {
        unsigned n = static_cast<unsigned char>(data[i]) << 16;
        out.push_back(kEnc[(n >> 18) & 63]);
        out.push_back(kEnc[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        unsigned n = (static_cast<unsigned char>(data[i]) << 16) |
                     (static_cast<unsigned char>(data[i + 1]) << 8);
        out.push_back(kEnc[(n >> 18) & 63]);
        out.push_back(kEnc[(n >> 12) & 63]);
        out.push_back(kEnc[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::string base64_decode(std::string_view data) {
    static const std::array<int, 256> dec = make_dec_table();
    std::string out;
    out.reserve(data.size() / 4 * 3);
    int buf = 0, bits = 0;
    for (char c : data) {
        if (c == '=') break;
        int v = dec[static_cast<unsigned char>(c)];
        if (v < 0) continue;  // skip whitespace/invalid
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

}  // namespace sbc
