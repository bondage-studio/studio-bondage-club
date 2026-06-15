#include <optional>
#include <string>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include "server/gameserver/socketio/server.hpp"
#include "server/gameserver/socketio/socket_facade.hpp"
#include "server/http_types.hpp"
#include "server/response_writer.hpp"
#include "test_framework.hpp"

using namespace sbc::server;
namespace sio = sbc::server::gameserver::socketio;
namespace asio = boost::asio;

namespace {

class CapturingWriter : public ResponseWriter {
public:
    int status = 0;
    std::string body;
    asio::awaitable<void> write_full(int s, sbc::HeaderMap, std::string b) override {
        status = s;
        body = std::move(b);
        co_return;
    }
    asio::awaitable<void> send_header(int, sbc::HeaderMap, std::optional<std::int64_t>) override {
        co_return;
    }
    asio::awaitable<void> write_chunk(std::string_view) override { co_return; }
    asio::awaitable<void> finish() override { co_return; }
    sbc::net::HijackedConnection hijack() override { throw std::runtime_error("no hijack"); }
};

Request req(const std::string& method, const std::string& query, std::string body,
            const std::string& address) {
    Request r;
    r.method = method;
    r.target = "/socket.io/?" + query;
    r.path = "/socket.io/";
    r.raw_query = query;
    r.body = std::move(body);
    r.remote_address = address;
    return r;
}

std::string sid_of(const std::string& open_body) {
    return nlohmann::json::parse(open_body.substr(1)).at("sid").get<std::string>();
}

asio::awaitable<bool> connect(sio::SocketIoServer& hub, const std::string& address, int& counter) {
    CapturingWriter hs;
    Request hs_req = req("GET", "EIO=4&transport=polling", {}, address);
    co_await hub.handle_request(hs_req, hs);
    std::string sid = sid_of(hs.body);
    int before = counter;
    CapturingWriter post;
    Request post_req = req("POST", "EIO=4&transport=polling&sid=" + sid, "40", address);
    co_await hub.handle_request(post_req, post);
    co_return counter > before;  // handler ran only if not rate-limited
}

}  // namespace

SBC_TEST(rate_limit_rejects_rapid_connections_from_same_ip) {
    asio::io_context ioc;
    sio::SocketIoServer hub(ioc.get_executor());
    int connects = 0;
    hub.set_connect_handler([&](std::shared_ptr<sio::Socket>) { ++connects; });

    bool done = false;
    bool a1 = false, a2 = false, a3 = false, other = false;
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            // Two connections from one IP are allowed; the third within a second is
            // rejected (IP_CONNECTION_RATE_LIMIT = 2).
            a1 = co_await connect(hub, "1.2.3.4", connects);
            a2 = co_await connect(hub, "1.2.3.4", connects);
            a3 = co_await connect(hub, "1.2.3.4", connects);
            // A different IP is unaffected.
            other = co_await connect(hub, "5.6.7.8", connects);
            done = true;
            ioc.stop();
            co_return;
        },
        asio::detached);
    ioc.run();

    CHECK(done);
    CHECK(a1);
    CHECK(a2);
    CHECK(!a3);
    CHECK(other);
}

SBC_TEST(live_limits_loosen_the_ip_rate) {
    asio::io_context ioc;
    sio::SocketIoServer hub(ioc.get_executor());
    // Raise the per-IP/sec rate so a 3rd rapid connection is now accepted —
    // proving the transport reads the limit live via set_limits/limits().
    sio::TransportLimits lim;
    lim.ip_connection_rate_per_sec = 5;
    hub.set_limits(lim);

    int connects = 0;
    hub.set_connect_handler([&](std::shared_ptr<sio::Socket>) { ++connects; });

    bool a1 = false, a2 = false, a3 = false, done = false;
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            a1 = co_await connect(hub, "1.2.3.4", connects);
            a2 = co_await connect(hub, "1.2.3.4", connects);
            a3 = co_await connect(hub, "1.2.3.4", connects);
            done = true;
            ioc.stop();
            co_return;
        },
        asio::detached);
    ioc.run();

    CHECK(done);
    CHECK(a1);
    CHECK(a2);
    CHECK(a3);
}
