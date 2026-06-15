#include <nlohmann/json.hpp>

#include "server/gameserver/engineio/eio_protocol.hpp"
#include "server/gameserver/socketio/sio_protocol.hpp"
#include "test_framework.hpp"

namespace eio = sbc::server::gameserver::engineio;
namespace sio = sbc::server::gameserver::socketio;

SBC_TEST(eio_packet_encode) {
    eio::Packet p;
    p.type = eio::PacketType::Pong;
    p.data = "probe";
    CHECK(p.encode() == "3probe");

    CHECK(eio::message("2[\"x\"]") == "42[\"x\"]");
}

SBC_TEST(eio_parse_packet) {
    auto open = eio::parse_packet("0{}");
    CHECK(open.has_value());
    CHECK(open->type == eio::PacketType::Open);
    CHECK(open->data == "{}");

    auto ping = eio::parse_packet("2");
    CHECK(ping.has_value());
    CHECK(ping->type == eio::PacketType::Ping);
    CHECK(ping->data.empty());

    CHECK(!eio::parse_packet("").has_value());
    CHECK(!eio::parse_packet("9bad").has_value());
}

SBC_TEST(eio_payload_framing) {
    std::vector<std::string> packets = {"40", "42[\"a\"]", "2"};
    std::string body = eio::encode_payload(packets);
    // Packets joined by the record separator \x1e.
    std::string expected =
        "40\x1e"
        "42[\"a\"]\x1e"
        "2";
    CHECK(body == expected);

    auto decoded = eio::decode_payload(body);
    CHECK(decoded.size() == 3);
    CHECK(decoded[0] == "40");
    CHECK(decoded[1] == "42[\"a\"]");
    CHECK(decoded[2] == "2");

    // Single packet (no separator) round-trips.
    auto single = eio::decode_payload("40");
    CHECK(single.size() == 1);
    CHECK(single[0] == "40");

    // Empty segments are dropped.
    auto empties = eio::decode_payload(std::string("\x1e\x1e", 2));
    CHECK(empties.empty());
}

SBC_TEST(eio_handshake_packet) {
    eio::HandshakeConfig cfg;
    cfg.sid = "abc123";
    std::string pkt = eio::handshake_packet(cfg);
    CHECK(!pkt.empty());
    CHECK(pkt.front() == '0');  // OPEN packet type
    auto j = nlohmann::json::parse(pkt.substr(1));
    CHECK(j["sid"] == "abc123");
    CHECK(j["upgrades"] == nlohmann::json::array({"websocket"}));
    CHECK(j["pingInterval"] == 50000);
    CHECK(j["pingTimeout"] == 30000);
    CHECK(j["maxPayload"] == 180000);
}

SBC_TEST(eio_generate_sid_unique) {
    std::string a = eio::generate_sid();
    std::string b = eio::generate_sid();
    CHECK(a.size() == 30);
    CHECK(b.size() == 30);
    CHECK(a != b);
}

SBC_TEST(sio_connect_packet) {
    std::string pkt = sio::connect_packet("sock-1");
    CHECK(pkt.front() == '0');  // CONNECT
    auto j = nlohmann::json::parse(pkt.substr(1));
    CHECK(j["sid"] == "sock-1");
    // Wire form via engine.io wrap.
    CHECK(eio::message(pkt).substr(0, 2) == "40");
}

SBC_TEST(sio_encode_event) {
    nlohmann::json data = {{"Name", "Player"}, {"MemberNumber", 42}};
    std::string pkt = sio::encode_event("LoginResponse", data);
    CHECK(pkt.front() == '2');  // EVENT
    auto arr = nlohmann::json::parse(pkt.substr(1));
    CHECK(arr.is_array());
    CHECK(arr[0] == "LoginResponse");
    CHECK(arr[1]["MemberNumber"] == 42);
}

SBC_TEST(sio_parse_event) {
    auto m = sio::parse("2[\"ChatRoomChat\",{\"Content\":\"hi\"}]");
    CHECK(m.has_value());
    CHECK(m->type == sio::PacketType::Event);
    CHECK(m->event == "ChatRoomChat");
    CHECK(m->data["Content"] == "hi");
    CHECK(!m->ack_id.has_value());
}

SBC_TEST(sio_parse_event_no_arg) {
    auto m = sio::parse("2[\"AccountDisconnect\"]");
    CHECK(m.has_value());
    CHECK(m->event == "AccountDisconnect");
    CHECK(m->data.is_null());
}

SBC_TEST(sio_parse_ack_id) {
    auto m = sio::parse("231[\"Event\",1]");
    CHECK(m.has_value());
    CHECK(m->event == "Event");
    CHECK(m->ack_id.has_value());
    CHECK(*m->ack_id == 31);
}

SBC_TEST(sio_parse_namespace_skipped) {
    auto m = sio::parse("2/admin,[\"Evt\",1]");
    CHECK(m.has_value());
    CHECK(m->event == "Evt");
}

SBC_TEST(sio_parse_connect_is_ignored) {
    // CONNECT (0) and DISCONNECT (1) are not EVENT/ACK -> parse returns nullopt.
    CHECK(!sio::parse("0{}").has_value());
    CHECK(!sio::parse("1").has_value());
    CHECK(!sio::parse("").has_value());
    CHECK(!sio::parse("2bad-json").has_value());
}
