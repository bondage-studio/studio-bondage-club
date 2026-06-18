#include "net/content_decoding.hpp"

#include <cstddef>
#include <string>
#include <string_view>

#include <zlib.h>

#include "test_framework.hpp"

using namespace sbctest;

namespace {

// gzip-compress `in` using zlib (windowBits 15+16 selects the gzip wrapper), so
// the decoder under test sees real gzip framing.
std::string gzip_compress(const std::string& in) {
    z_stream strm{};
    deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    strm.avail_in = static_cast<uInt>(in.size());
    std::string out;
    char buf[16384];
    int ret = Z_OK;
    do {
        strm.next_out = reinterpret_cast<Bytef*>(buf);
        strm.avail_out = sizeof(buf);
        ret = deflate(&strm, Z_FINISH);
        out.append(buf, sizeof(buf) - strm.avail_out);
    } while (ret != Z_STREAM_END);
    deflateEnd(&strm);
    return out;
}

// Feed `enc` through a fresh decoder for `ce` in `chunk`-sized pieces, mimicking
// how the upstream body streams in.
std::string decode_chunked(const std::string& enc, const std::string& ce, std::size_t chunk) {
    auto d = sbc::net::make_decoder(ce);
    std::string out;
    for (std::size_t i = 0; i < enc.size(); i += chunk) {
        out += d->decode(std::string_view(enc).substr(i, chunk));
    }
    out += d->finish();
    return out;
}

}  // namespace

SBC_TEST(content_decoding_gzip_roundtrip) {
    std::string original;
    for (int i = 0; i < 1000; ++i) original += "The quick brown fox jumps over the lazy dog. ";
    std::string gz = gzip_compress(original);
    CHECK(gz.size() < original.size());  // genuinely compressed

    CHECK(decode_chunked(gz, "gzip", gz.size()) == original);  // one shot
    CHECK(decode_chunked(gz, "gzip", 1) == original);          // byte-by-byte streaming
    CHECK(decode_chunked(gz, "gzip", 7) == original);          // odd chunk boundaries
    CHECK(decode_chunked(gz, "x-gzip", 64) == original);       // x-gzip alias
}

SBC_TEST(content_decoding_identity_passthrough) {
    auto d = sbc::net::make_decoder("identity");
    CHECK(d != nullptr);
    std::string out = d->decode("abc");
    out += d->decode("def");
    out += d->finish();
    CHECK(out == "abcdef");

    auto e = sbc::net::make_decoder("");  // absent Content-Encoding
    CHECK(e != nullptr);
    CHECK(e->decode("xyz") + e->finish() == "xyz");
}

SBC_TEST(content_decoding_factory_selection) {
    CHECK(sbc::net::make_decoder("br") != nullptr);
    CHECK(sbc::net::make_decoder("BR") != nullptr);    // case-insensitive
    CHECK(sbc::net::make_decoder("gzip") != nullptr);
    CHECK(sbc::net::make_decoder("gzip, br") != nullptr);  // first token honored

    // Encodings we never advertise -> null, so the caller falls back to an
    // uncached raw passthrough.
    CHECK(sbc::net::make_decoder("zstd") == nullptr);
    CHECK(sbc::net::make_decoder("compress") == nullptr);
}
