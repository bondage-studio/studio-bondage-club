#include "net/content_decoding.hpp"

#include <cctype>
#include <cstring>

#include <brotli/decode.h>
#include <zlib.h>

#include "common/error.hpp"

namespace sbc::net {

namespace {

constexpr std::size_t kChunk = 64 * 1024;

// First comma-separated token of `s`, trimmed and lowercased.
std::string first_token_lower(std::string_view s) {
    std::size_t end = s.find(',');
    std::string_view tok = s.substr(0, end);
    std::size_t b = 0, e = tok.size();
    while (b < e && std::isspace(static_cast<unsigned char>(tok[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(tok[e - 1]))) --e;
    std::string out(tok.substr(b, e - b));
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Passthrough for identity / absent Content-Encoding.
class IdentityDecoder : public ContentDecoder {
public:
    std::string decode(std::string_view in) override { return std::string(in); }
    std::string finish() override { return {}; }
};

// gzip / zlib-wrapped deflate via zlib's streaming inflate. windowBits 15+32
// auto-detects the gzip and zlib headers.
class GzipDecoder : public ContentDecoder {
public:
    GzipDecoder() {
        std::memset(&strm_, 0, sizeof(strm_));
        if (inflateInit2(&strm_, 15 + 32) != Z_OK) throw Error("zlib inflateInit2 failed");
    }
    ~GzipDecoder() override { inflateEnd(&strm_); }

    std::string decode(std::string_view in) override { return run(in, Z_NO_FLUSH); }
    std::string finish() override {
        if (done_) return {};
        return run(std::string_view{}, Z_FINISH);
    }

private:
    std::string run(std::string_view in, int flush) {
        std::string out;
        strm_.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
        strm_.avail_in = static_cast<uInt>(in.size());
        unsigned char buf[kChunk];
        for (;;) {
            strm_.next_out = buf;
            strm_.avail_out = sizeof(buf);
            int ret = inflate(&strm_, flush);
            if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR ||
                ret == Z_NEED_DICT) {
                throw Error(std::string("gzip inflate error: ") +
                            (strm_.msg ? strm_.msg : "corrupt stream"));
            }
            out.append(reinterpret_cast<char*>(buf), sizeof(buf) - strm_.avail_out);
            if (ret == Z_STREAM_END) {
                done_ = true;
                break;
            }
            if (ret == Z_BUF_ERROR) break;  // no progress: needs more input
            if (strm_.avail_out != 0) break;  // current input fully drained
        }
        return out;
    }

    z_stream strm_;
    bool done_ = false;
};

// Brotli via the streaming decoder API.
class BrotliDecoder : public ContentDecoder {
public:
    BrotliDecoder() : state_(BrotliDecoderCreateInstance(nullptr, nullptr, nullptr)) {
        if (!state_) throw Error("brotli decoder allocation failed");
    }
    ~BrotliDecoder() override { BrotliDecoderDestroyInstance(state_); }

    std::string decode(std::string_view in) override { return run(in); }
    std::string finish() override {
        if (done_) return {};
        return run(std::string_view{});
    }

private:
    std::string run(std::string_view in) {
        std::string out;
        const uint8_t* next_in = reinterpret_cast<const uint8_t*>(in.data());
        std::size_t avail_in = in.size();
        uint8_t buf[kChunk];
        for (;;) {
            uint8_t* next_out = buf;
            std::size_t avail_out = sizeof(buf);
            BrotliDecoderResult r = BrotliDecoderDecompressStream(state_, &avail_in, &next_in,
                                                                  &avail_out, &next_out, nullptr);
            out.append(reinterpret_cast<char*>(buf), sizeof(buf) - avail_out);
            if (r == BROTLI_DECODER_RESULT_SUCCESS) {
                done_ = true;
                break;
            }
            if (r == BROTLI_DECODER_RESULT_ERROR) throw Error("brotli decode error");
            if (r == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) break;  // feed next chunk
            // BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT: loop with a fresh buffer.
        }
        return out;
    }

    BrotliDecoderState* state_;
    bool done_ = false;
};

}  // namespace

std::unique_ptr<ContentDecoder> make_decoder(std::string_view content_encoding) {
    std::string tok = first_token_lower(content_encoding);
    if (tok.empty() || tok == "identity") return std::make_unique<IdentityDecoder>();
    if (tok == "gzip" || tok == "x-gzip" || tok == "deflate") return std::make_unique<GzipDecoder>();
    if (tok == "br") return std::make_unique<BrotliDecoder>();
    return nullptr;
}

}  // namespace sbc::net
