#include "server/session.hpp"

#include <chrono>
#include <optional>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http.hpp>
#include <spdlog/spdlog.h>

#include "common/error.hpp"
#include "common/url.hpp"
#include "server/response_writer.hpp"

namespace sbc::server {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
using tcp = asio::ip::tcp;

namespace {

constexpr auto kIdleTimeout = std::chrono::seconds(120);
constexpr auto kWriteTimeout = std::chrono::seconds(120);
constexpr std::size_t kHeaderLimit = 1024 * 1024;       // 1 MiB
constexpr std::size_t kBodyLimit = 32 * 1024 * 1024;    // 32 MiB

// Headers Beast computes itself; never copy them through from a handler.
bool is_reserved_header(std::string_view key) {
    return key == "Content-Length" || key == "Transfer-Encoding";
}

// BeastResponseWriter implements ResponseWriter over a Beast tcp_stream,
// supporting buffered, chunked-streaming, and hijack output modes.
class BeastResponseWriter : public ResponseWriter {
public:
    BeastResponseWriter(net::TcpStream& stream, beast::flat_buffer& buffer, unsigned version,
                        bool keep_alive)
        : stream_(stream), buffer_(buffer), version_(version), keep_alive_(keep_alive) {}

    bool finished() const { return finished_; }
    bool keep_alive() const { return keep_alive_; }

    asio::awaitable<void> write_full(int status, HeaderMap headers, std::string body) override {
        http::response<http::string_body> res;
        res.version(version_);
        res.result(status);
        apply_headers(res, headers);
        res.keep_alive(keep_alive_);
        res.body() = std::move(body);
        res.prepare_payload();
        header_sent_ = true;
        finished_ = true;
        stream_.expires_after(kWriteTimeout);
        co_await http::async_write(stream_, res, asio::use_awaitable);
    }

    asio::awaitable<void> send_header(int status, HeaderMap headers,
                                      std::optional<std::int64_t> content_length) override {
        res_.emplace();
        res_->version(version_);
        res_->result(status);
        apply_headers(*res_, headers);
        res_->keep_alive(keep_alive_);
        if (content_length) {
            res_->content_length(static_cast<std::uint64_t>(*content_length));
        } else {
            res_->chunked(true);
        }
        sr_.emplace(*res_);
        header_sent_ = true;
        stream_.expires_after(kWriteTimeout);
        co_await http::async_write_header(stream_, *sr_, asio::use_awaitable);
    }

    asio::awaitable<void> write_chunk(std::string_view data) override {
        if (data.empty() || !sr_) co_return;
        res_->body().data = const_cast<char*>(data.data());
        res_->body().size = data.size();
        res_->body().more = true;
        stream_.expires_after(kWriteTimeout);
        auto [ec, n] =
            co_await http::async_write(stream_, *sr_, asio::as_tuple(asio::use_awaitable));
        (void)n;
        if (ec == http::error::need_buffer) co_return;  // chunk consumed; awaiting more
        if (ec) throw boost::system::system_error(ec);
    }

    asio::awaitable<void> finish() override {
        if (finished_) co_return;
        finished_ = true;
        if (!sr_) co_return;
        res_->body().data = nullptr;
        res_->body().size = 0;
        res_->body().more = false;
        stream_.expires_after(kWriteTimeout);
        co_await http::async_write(stream_, *sr_, asio::use_awaitable);
    }

    net::HijackedConnection hijack() override {
        if (header_sent_) throw Error("cannot hijack connection after header sent");
        hijacked_ = true;
        return net::HijackedConnection{std::move(stream_), std::move(buffer_)};
    }

private:
    template <typename Body>
    void apply_headers(http::response<Body>& res, const HeaderMap& headers) {
        for (const auto& e : headers.entries()) {
            if (is_reserved_header(e.first)) continue;
            res.insert(e.first, e.second);
        }
    }

    net::TcpStream& stream_;
    beast::flat_buffer& buffer_;
    unsigned version_;
    bool keep_alive_;
    bool finished_ = false;
    std::optional<http::response<http::buffer_body>> res_;
    std::optional<http::response_serializer<http::buffer_body>> sr_;
};

Request build_request(http::request<http::string_body>& req) {
    Request out;
    out.method = std::string(req.method_string());
    out.target = std::string(req.target());
    out.version = req.version();
    out.keep_alive = req.keep_alive();
    for (const auto& field : req.base()) {
        out.headers.add(std::string(field.name_string()), std::string(field.value()));
    }
    out.body = std::move(req.body());

    if (auto u = Url::try_parse(out.target)) {
        out.path = u->path();
        out.raw_query = u->query();
    } else {
        auto qpos = out.target.find('?');
        out.path = out.target.substr(0, qpos);
        out.raw_query = qpos == std::string::npos ? "" : out.target.substr(qpos + 1);
    }
    if (out.path.empty()) out.path = "/";
    return out;
}

}  // namespace

Session::Session(tcp::socket socket, Handler handler, ConnectionStats& stats)
    : stream_(std::move(socket)),
      handler_(std::move(handler)),
      stats_(stats) {}

asio::awaitable<void> Session::run() {
    stats_.active.fetch_add(1, std::memory_order_relaxed);
    struct ActiveGuard {
        ConnectionStats& s;
        ~ActiveGuard() { s.active.fetch_sub(1, std::memory_order_relaxed); }
    } active_guard{stats_};

    bool close_connection = false;
    for (;;) {
        http::request_parser<http::string_body> parser;
        parser.header_limit(kHeaderLimit);
        parser.body_limit(kBodyLimit);

        stream_.expires_after(kIdleTimeout);
        auto [ec, n] = co_await http::async_read(stream_, buffer_, parser,
                                                 asio::as_tuple(asio::use_awaitable));
        (void)n;
        if (ec) break;  // EOF / timeout / parse error -> close

        Request req = build_request(parser.get());
        {
            boost::system::error_code rec;
            auto ep = stream_.socket().remote_endpoint(rec);
            if (!rec) req.remote_address = ep.address().to_string();
        }
        BeastResponseWriter writer(stream_, buffer_, req.version, req.keep_alive);

        stats_.handling.fetch_add(1, std::memory_order_relaxed);
        bool handler_failed = false;
        std::string handler_error;
        try {
            stream_.expires_never();  // handler manages its own timeouts
            co_await handler_(req, writer);
        } catch (const std::exception& e) {
            handler_failed = true;
            handler_error = e.what();
        }
        if (handler_failed) {
            spdlog::warn("handler error path={} error={}", req.path, handler_error);
            close_connection = true;
            if (!writer.header_sent() && !writer.hijacked()) {
                try {
                    HeaderMap h;
                    h.set("Content-Type", "text/plain; charset=utf-8");
                    co_await writer.write_full(500, std::move(h), "internal server error\n");
                } catch (...) {
                }
            }
        }
        stats_.handling.fetch_sub(1, std::memory_order_relaxed);

        if (writer.hijacked()) co_return;  // connection owned by the handler now

        if (!writer.finished()) {
            try {
                co_await writer.finish();
            } catch (...) {
                close_connection = true;
            }
        }

        if (close_connection || !req.keep_alive) break;
    }

    boost::system::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
    co_return;
}

}  // namespace sbc::server
