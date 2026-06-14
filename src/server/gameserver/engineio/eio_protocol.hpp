#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sbc::server::gameserver::engineio {

// Engine.IO v4 packet types. The wire form of a packet is a single type digit
// followed by its (optional) string data, e.g. "4<message-payload>".
enum class PacketType : char {
    Open = '0',
    Close = '1',
    Ping = '2',
    Pong = '3',
    Message = '4',
    Upgrade = '5',
    Noop = '6',
};

// Record separator that joins multiple packets in a single HTTP long-poll body.
inline constexpr char kRecordSeparator = '\x1e';

// Default Engine.IO handshake timing. These mirror the original BC server
// (docs/Bondage-Club-Server/app.js) so the unmodified client behaves identically.
inline constexpr int kPingInterval = 50000;
inline constexpr int kPingTimeout = 30000;
inline constexpr int kMaxPayload = 180000;

struct Packet {
    PacketType type = PacketType::Noop;
    std::string data;

    // encode renders the packet to its wire form: the type digit then data.
    std::string encode() const;
};

// message wraps a Socket.IO payload as an Engine.IO MESSAGE packet ("4"+payload).
std::string message(std::string_view payload);

// parse_packet decodes one wire-form packet (type digit + data). Returns
// nullopt when the input is empty or the type digit is unrecognized.
std::optional<Packet> parse_packet(std::string_view encoded);

// encode_payload joins already-encoded packets with the record separator for a
// polling HTTP response body.
std::string encode_payload(const std::vector<std::string>& encoded_packets);

// decode_payload splits a polling HTTP request body into its constituent
// encoded packets (the record-separator-delimited segments). Empty segments are
// dropped.
std::vector<std::string> decode_payload(std::string_view body);

struct HandshakeConfig {
    std::string sid;
    std::vector<std::string> upgrades = {"websocket"};
    int ping_interval = kPingInterval;
    int ping_timeout = kPingTimeout;
    int max_payload = kMaxPayload;
};

// handshake_packet builds the OPEN packet body, e.g.
// 0{"sid":"...","upgrades":["websocket"],"pingInterval":50000,...}.
std::string handshake_packet(const HandshakeConfig& cfg);

// generate_sid returns a fresh random session id (URL-safe, suitable for use as
// both the Engine.IO sid and the Socket.IO socket id).
std::string generate_sid();

}  // namespace sbc::server::gameserver::engineio
