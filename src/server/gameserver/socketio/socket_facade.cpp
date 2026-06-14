#include "server/gameserver/socketio/socket_facade.hpp"

#include "server/gameserver/socketio/server.hpp"

namespace sbc::server::gameserver::socketio {

std::string Socket::id() const {
    if (auto c = conn_.lock()) return c->sid();
    return {};
}

std::string Socket::address() const {
    if (auto c = conn_.lock()) return c->address();
    return {};
}

void Socket::on(std::string event, EventHandler fn) {
    if (auto c = conn_.lock()) c->on(std::move(event), std::move(fn));
}

void Socket::off(const std::string& event) {
    if (auto c = conn_.lock()) c->off(event);
}

void Socket::once_disconnect(DisconnectHandler fn) {
    if (auto c = conn_.lock()) c->set_disconnect_handler(std::move(fn));
}

void Socket::on_any(AnyHandler fn) {
    if (auto c = conn_.lock()) c->set_any_handler(std::move(fn));
}

void Socket::emit(std::string_view event, const nlohmann::json& data) {
    if (auto c = conn_.lock()) c->emit(event, data);
}

void Socket::join(const std::string& room) {
    if (auto c = conn_.lock()) {
        c->note_joined(room);
        if (hub_) hub_->join_room(room, c->sid());
    }
}

void Socket::leave(const std::string& room) {
    if (auto c = conn_.lock()) {
        c->note_left(room);
        if (hub_) hub_->leave_room(room, c->sid());
    }
}

Socket::RoomEmitter Socket::to(const std::string& room) {
    std::string sid;
    if (auto c = conn_.lock()) sid = c->sid();
    return RoomEmitter(hub_, room, sid);
}

void Socket::disconnect() {
    if (auto c = conn_.lock()) c->disconnect();
}

void Socket::RoomEmitter::emit(std::string_view event, const nlohmann::json& data) {
    if (hub_) hub_->emit_to_room(room_, event, data, except_sid_);
}

}  // namespace sbc::server::gameserver::socketio
