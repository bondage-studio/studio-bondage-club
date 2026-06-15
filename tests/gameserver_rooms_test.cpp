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

// CapturingWriter records the buffered response body. Only write_full is used by
// the Engine.IO long-poll path (handshake / poll / post).
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
    sbc::net::HijackedConnection hijack() override {
        throw std::runtime_error("no hijack in test");
    }
};

Request make_req(const std::string& method, const std::string& query, std::string body = {},
                 const std::string& address = {}) {
    Request r;
    r.method = method;
    r.target = "/socket.io/?" + query;
    r.path = "/socket.io/";
    r.raw_query = query;
    r.body = std::move(body);
    r.remote_address = address;
    return r;
}

// extract_sid pulls the sid out of an Engine.IO OPEN packet body (0{...json...}).
std::string extract_sid(const std::string& open_body) {
    auto j = nlohmann::json::parse(open_body.substr(1));
    return j.at("sid").get<std::string>();
}

// connect runs handshake + Socket.IO CONNECT for one client and returns its sid,
// draining the queued 40{sid} so the outbox is empty afterwards.
asio::awaitable<std::string> connect(sio::SocketIoServer& hub, const std::string& address) {
    // Each test client uses a distinct address so the per-IP connection rate limit
    // (2/sec) does not interfere with room tests.
    CapturingWriter hs;
    Request hs_req = make_req("GET", "EIO=4&transport=polling", {}, address);
    co_await hub.handle_request(hs_req, hs);
    std::string sid = extract_sid(hs.body);

    CapturingWriter post;
    Request post_req = make_req("POST", "EIO=4&transport=polling&sid=" + sid, "40", address);
    co_await hub.handle_request(post_req, post);

    auto conn = hub.find(sid);
    co_await conn->drain_for_poll();  // discard the 40{sid}
    co_return sid;
}

}  // namespace

SBC_TEST(rooms_broadcast_reaches_members) {
    asio::io_context ioc;
    sio::SocketIoServer hub(ioc.get_executor());

    // Connect handler: every socket joins room "r".
    hub.set_connect_handler([](std::shared_ptr<sio::Socket> socket) { socket->join("r"); });

    std::string drained_a, drained_b, drained_c;
    bool done = false;

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            std::string a = co_await connect(hub, "10.0.0.1");
            std::string b = co_await connect(hub, "10.0.0.2");
            std::string c = co_await connect(hub, "10.0.0.3");  // joins "r" too

            // Broadcast to the room, excluding c.
            hub.emit_to_room("r", "ChatRoomMessage", nlohmann::json{{"Content", "hi"}}, c);

            drained_a = co_await hub.find(a)->drain_for_poll();
            drained_b = co_await hub.find(b)->drain_for_poll();

            // c was excluded, so its outbox is empty; emit something directly to it
            // so the poll does not park forever, then drain.
            hub.find(c)->emit("Direct", nlohmann::json{{"x", 1}});
            drained_c = co_await hub.find(c)->drain_for_poll();

            done = true;
            ioc.stop();
            co_return;
        },
        asio::detached);

    ioc.run();

    CHECK(done);
    CHECK(drained_a.find("ChatRoomMessage") != std::string::npos);
    CHECK(drained_a.find("\"hi\"") != std::string::npos);
    CHECK(drained_b.find("ChatRoomMessage") != std::string::npos);
    CHECK(drained_c.find("ChatRoomMessage") == std::string::npos);
    CHECK(drained_c.find("Direct") != std::string::npos);

    CHECK(hub.online_count() == 3);
    CHECK(hub.room_count() == 1);
}

SBC_TEST(rooms_leave_removes_membership) {
    asio::io_context ioc;
    sio::SocketIoServer hub(ioc.get_executor());
    hub.set_connect_handler([](std::shared_ptr<sio::Socket> socket) { socket->join("r"); });

    std::string drained;
    bool done = false;

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            std::string a = co_await connect(hub, "10.0.0.1");
            // Leave the room, then broadcast — a must not receive it.
            hub.leave_room("r", a);
            hub.emit_to_room("r", "ChatRoomMessage", nlohmann::json{{"Content", "hi"}});
            // Room is now empty (only member left).
            // Give a a direct packet so drain returns promptly.
            hub.find(a)->emit("Direct", nlohmann::json{});
            drained = co_await hub.find(a)->drain_for_poll();
            done = true;
            ioc.stop();
            co_return;
        },
        asio::detached);

    ioc.run();

    CHECK(done);
    CHECK(drained.find("ChatRoomMessage") == std::string::npos);
    CHECK(drained.find("Direct") != std::string::npos);
    CHECK(hub.room_count() == 0);
}
