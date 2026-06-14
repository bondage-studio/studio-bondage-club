#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace sbc::server::gameserver::socketio {

// Socket.IO v4 packet types. A Socket.IO packet is carried inside an Engine.IO
// MESSAGE packet, so on the wire an event looks like "42[\"Event\",arg]": the
// leading '4' is the Engine.IO MESSAGE type, the '2' is the Socket.IO EVENT type.
enum class PacketType : char {
    Connect = '0',
    Disconnect = '1',
    Event = '2',
    Ack = '3',
    ConnectError = '4',
    BinaryEvent = '5',
    BinaryAck = '6',
};

// Message is a decoded EVENT or ACK packet (the only kinds the game logic cares
// about). For EVENT packets, `event` is args[0] and `data` is args[1] (or null
// when absent); `args` always holds the full decoded JSON array.
struct Message {
    PacketType type = PacketType::Event;
    std::optional<int> ack_id;  // present when the packet carried an ack id
    std::string event;          // EVENT name (args[0]); empty for ACK
    nlohmann::json data;        // EVENT first argument (args[1]) or null
    nlohmann::json args;        // full args array as decoded
};

// connect_packet builds the Socket.IO CONNECT acknowledgement payload
// (0{"sid":"..."}). Wrap it with engineio::message() for the wire form "40{...}".
std::string connect_packet(std::string_view sid);

// encode_event builds an EVENT payload (2["name",data]). Wrap it with
// engineio::message() for the wire form "42[...]".
std::string encode_event(std::string_view name, const nlohmann::json& data);

// parse decodes a Socket.IO packet payload — the data portion of an Engine.IO
// MESSAGE packet (i.e. with the leading '4' already stripped). Returns nullopt
// for packet kinds other than EVENT/ACK or on malformed input. A leading
// namespace ("/nsp,") is tolerated and ignored (BC uses only the default "/").
std::optional<Message> parse(std::string_view payload);

}  // namespace sbc::server::gameserver::socketio
