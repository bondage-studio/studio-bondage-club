#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "server/gameserver/socketio/connection.hpp"

namespace sbc::server::gameserver::socketio {

class SocketIoServer;

// Socket is the handler-facing facade over a Connection, mirroring the subset of
// the socket.io Socket API that the BC server uses. It holds a weak reference to
// its Connection so handler closures can capture the Socket without creating an
// ownership cycle (the Connection owns the Socket and the handlers).
class Socket {
public:
    Socket(std::weak_ptr<Connection> conn, SocketIoServer* hub) : conn_(std::move(conn)), hub_(hub) {}

    // id returns the Socket.IO socket id (equal to the Engine.IO sid). BC uses
    // this as Account.ID.
    std::string id() const;

    // address returns the peer IP (for the account-creation rate limit).
    std::string address() const;

    void on(std::string event, EventHandler fn);
    void off(const std::string& event);
    void once_disconnect(DisconnectHandler fn);
    void on_any(AnyHandler fn);

    // emit sends an event to this socket.
    void emit(std::string_view event, const nlohmann::json& data);

    void join(const std::string& room);
    void leave(const std::string& room);

    // RoomEmitter targets a room for a single emit, mirroring socket.io's
    // `socket.to(room).emit(...)` (broadcast to the room excluding this socket).
    class RoomEmitter {
    public:
        RoomEmitter(SocketIoServer* hub, std::string room, std::string except_sid)
            : hub_(hub), room_(std::move(room)), except_sid_(std::move(except_sid)) {}
        void emit(std::string_view event, const nlohmann::json& data);

    private:
        SocketIoServer* hub_;
        std::string room_;
        std::string except_sid_;
    };
    RoomEmitter to(const std::string& room);

    void disconnect();

private:
    std::weak_ptr<Connection> conn_;
    SocketIoServer* hub_;
};

}  // namespace sbc::server::gameserver::socketio
