#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace sbc::net {

// ContentDecoder incrementally decodes a Content-Encoding'd upstream body into
// identity bytes. Feed compressed chunks to decode() as they stream in; call
// finish() once after the last chunk to flush trailing output. This lets the
// reverse proxy request gzip/br from upstream (small, fast transfer) while
// caching and serving a single canonical identity representation.
class ContentDecoder {
public:
    virtual ~ContentDecoder() = default;

    // decode consumes one compressed chunk and returns whatever identity bytes
    // are now available (possibly empty). Throws on malformed input.
    virtual std::string decode(std::string_view in) = 0;

    // finish flushes any remaining output after the final chunk. Returns the
    // trailing identity bytes (usually empty for a well-formed stream).
    virtual std::string finish() = 0;
};

// make_decoder returns a decoder for the given Content-Encoding header value
// (case-insensitive; only the first comma-separated token is honored):
//   - Identity passthrough for "" / "identity"
//   - gzip/deflate (zlib, auto-detecting the gzip or zlib wrapper) for
//     "gzip" / "x-gzip" / "deflate"
//   - Brotli for "br"
//   - nullptr for any other (unsupported) encoding — caller should fall back to
//     an uncached raw passthrough.
std::unique_ptr<ContentDecoder> make_decoder(std::string_view content_encoding);

}  // namespace sbc::net
