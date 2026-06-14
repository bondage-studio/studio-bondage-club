#include "server/gameserver/engineio/eio_protocol.hpp"

#include <array>
#include <random>

#include <nlohmann/json.hpp>

namespace sbc::server::gameserver::engineio {

namespace {

bool is_packet_type(char c) { return c >= '0' && c <= '6'; }

}  // namespace

std::string Packet::encode() const {
    std::string out;
    out.reserve(1 + data.size());
    out.push_back(static_cast<char>(type));
    out.append(data);
    return out;
}

std::string message(std::string_view payload) {
    std::string out;
    out.reserve(1 + payload.size());
    out.push_back(static_cast<char>(PacketType::Message));
    out.append(payload);
    return out;
}

std::optional<Packet> parse_packet(std::string_view encoded) {
    if (encoded.empty()) return std::nullopt;
    if (!is_packet_type(encoded.front())) return std::nullopt;
    Packet p;
    p.type = static_cast<PacketType>(encoded.front());
    p.data.assign(encoded.substr(1));
    return p;
}

std::string encode_payload(const std::vector<std::string>& encoded_packets) {
    std::string out;
    for (std::size_t i = 0; i < encoded_packets.size(); ++i) {
        if (i != 0) out.push_back(kRecordSeparator);
        out.append(encoded_packets[i]);
    }
    return out;
}

std::vector<std::string> decode_payload(std::string_view body) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= body.size()) {
        std::size_t sep = body.find(kRecordSeparator, start);
        std::string_view seg = body.substr(
            start, sep == std::string_view::npos ? std::string_view::npos : sep - start);
        if (!seg.empty()) out.emplace_back(seg);
        if (sep == std::string_view::npos) break;
        start = sep + 1;
    }
    return out;
}

std::string handshake_packet(const HandshakeConfig& cfg) {
    nlohmann::json j;
    j["sid"] = cfg.sid;
    j["upgrades"] = cfg.upgrades;
    j["pingInterval"] = cfg.ping_interval;
    j["pingTimeout"] = cfg.ping_timeout;
    j["maxPayload"] = cfg.max_payload;
    std::string out;
    out.push_back(static_cast<char>(PacketType::Open));
    out.append(j.dump());
    return out;
}

std::string generate_sid() {
    // 15 random bytes, hex-encoded (30 chars). The client treats the sid as an
    // opaque token, so any unique URL-safe string suffices.
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 255);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(30);
    for (int i = 0; i < 15; ++i) {
        int b = dist(rng);
        out.push_back(kHex[(b >> 4) & 0xF]);
        out.push_back(kHex[b & 0xF]);
    }
    return out;
}

}  // namespace sbc::server::gameserver::engineio
