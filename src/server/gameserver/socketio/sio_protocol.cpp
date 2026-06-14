#include "server/gameserver/socketio/sio_protocol.hpp"

#include <cctype>

namespace sbc::server::gameserver::socketio {

std::string connect_packet(std::string_view sid) {
    nlohmann::json j;
    j["sid"] = std::string(sid);
    std::string out;
    out.push_back(static_cast<char>(PacketType::Connect));
    out.append(j.dump());
    return out;
}

std::string encode_event(std::string_view name, const nlohmann::json& data) {
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back(std::string(name));
    arr.push_back(data);
    std::string out;
    out.push_back(static_cast<char>(PacketType::Event));
    out.append(arr.dump());
    return out;
}

std::optional<Message> parse(std::string_view payload) {
    if (payload.empty()) return std::nullopt;
    char type_digit = payload.front();
    if (type_digit < '0' || type_digit > '6') return std::nullopt;
    auto type = static_cast<PacketType>(type_digit);
    if (type != PacketType::Event && type != PacketType::Ack) return std::nullopt;

    std::size_t pos = 1;
    // Optional namespace: "/<name>," — tolerated and discarded.
    if (pos < payload.size() && payload[pos] == '/') {
        std::size_t comma = payload.find(',', pos);
        if (comma == std::string_view::npos)
            pos = payload.size();
        else
            pos = comma + 1;
    }

    // Optional ack id: a run of digits.
    std::optional<int> ack_id;
    std::size_t digits_start = pos;
    while (pos < payload.size() && std::isdigit(static_cast<unsigned char>(payload[pos]))) ++pos;
    if (pos > digits_start) {
        ack_id = std::stoi(std::string(payload.substr(digits_start, pos - digits_start)));
    }

    Message msg;
    msg.type = type;
    msg.ack_id = ack_id;

    std::string_view json_part = payload.substr(pos);
    if (json_part.empty()) {
        msg.args = nlohmann::json::array();
        return msg;
    }

    nlohmann::json parsed = nlohmann::json::parse(json_part, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_array()) return std::nullopt;
    msg.args = std::move(parsed);

    if (type == PacketType::Event && !msg.args.empty()) {
        if (msg.args[0].is_string()) msg.event = msg.args[0].get<std::string>();
        if (msg.args.size() > 1) msg.data = msg.args[1];
    }
    return msg;
}

}  // namespace sbc::server::gameserver::socketio
