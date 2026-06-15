#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>

#include "common/http_util.hpp"
#include "net/stream.hpp"

namespace sbc::server {

// ResponseWriter is the handler-facing output interface. It supports three modes:
//   1. Buffered:  write_full() in one shot (JSON/API/homepage).
//   2. Streaming: send_header() then write_chunk()* then finish() (cache serve,
//      proxy pass, SSE).
//   3. Hijack:    take ownership of the raw connection (WebSocket, SSE).
class ResponseWriter {
public:
    virtual ~ResponseWriter() = default;

    // write_full sends a complete buffered response. Sets Content-Length.
    virtual boost::asio::awaitable<void> write_full(int status, HeaderMap headers,
                                                    std::string body) = 0;

    // send_header sends the status line and headers. When content_length is set,
    // a Content-Length header is emitted; otherwise the body is chunked.
    virtual boost::asio::awaitable<void> send_header(
        int status, HeaderMap headers,
        std::optional<std::int64_t> content_length) = 0;

    // write_chunk streams a body chunk. Must follow send_header().
    virtual boost::asio::awaitable<void> write_chunk(std::string_view data) = 0;

    // finish completes a streamed response.
    virtual boost::asio::awaitable<void> finish() = 0;

    // hijack relinquishes the underlying connection to the caller. After this
    // the Session stops managing the socket. Throws if the header was sent.
    virtual net::HijackedConnection hijack() = 0;

    bool header_sent() const { return header_sent_; }
    // After a hijack the response is considered fully handled by the caller.
    bool hijacked() const { return hijacked_; }

protected:
    bool header_sent_ = false;
    bool hijacked_ = false;
};

}  // namespace sbc::server
