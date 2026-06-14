#include "net/socks5.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>

#include <array>
#include <vector>

#include "common/error.hpp"
#include "common/url.hpp"

namespace sbc::net {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

Socks5Config socks5_config_from_url(const Url& url) {
    Socks5Config c;
    c.host = url.host();
    c.port = url.port();
    if (url.has_userinfo()) {
        c.user = url.user();
        c.password = url.password();
    }
    return c;
}

asio::awaitable<tcp::socket> socks5_connect(asio::any_io_executor executor, Socks5Config config,
                                            std::string dest_host, std::uint16_t dest_port) {
    tcp::resolver resolver(executor);
    auto endpoints = co_await resolver.async_resolve(config.host, std::to_string(config.port),
                                                     asio::use_awaitable);
    tcp::socket socket(executor);
    co_await asio::async_connect(socket, endpoints, asio::use_awaitable);

    const bool use_auth = !config.user.empty();

    // Greeting: VER, NMETHODS, METHODS...
    std::vector<unsigned char> greeting = {0x05};
    if (use_auth) {
        greeting.push_back(0x02);
        greeting.push_back(0x00);  // no-auth
        greeting.push_back(0x02);  // user/pass
    } else {
        greeting.push_back(0x01);
        greeting.push_back(0x00);
    }
    co_await asio::async_write(socket, asio::buffer(greeting), asio::use_awaitable);

    std::array<unsigned char, 2> method_reply{};
    co_await asio::async_read(socket, asio::buffer(method_reply), asio::use_awaitable);
    if (method_reply[0] != 0x05) throw Error("socks5: bad version in method reply");

    if (method_reply[1] == 0x02) {
        if (!use_auth) throw Error("socks5: proxy requires authentication");
        std::vector<unsigned char> auth = {0x01};
        auth.push_back(static_cast<unsigned char>(config.user.size()));
        auth.insert(auth.end(), config.user.begin(), config.user.end());
        auth.push_back(static_cast<unsigned char>(config.password.size()));
        auth.insert(auth.end(), config.password.begin(), config.password.end());
        co_await asio::async_write(socket, asio::buffer(auth), asio::use_awaitable);

        std::array<unsigned char, 2> auth_reply{};
        co_await asio::async_read(socket, asio::buffer(auth_reply), asio::use_awaitable);
        if (auth_reply[1] != 0x00) throw Error("socks5: authentication failed");
    } else if (method_reply[1] != 0x00) {
        throw Error("socks5: no acceptable authentication method");
    }

    // CONNECT request with domain ATYP (let the proxy resolve).
    if (dest_host.size() > 255) throw Error("socks5: destination host too long");
    std::vector<unsigned char> connect_req = {0x05, 0x01, 0x00, 0x03};
    connect_req.push_back(static_cast<unsigned char>(dest_host.size()));
    connect_req.insert(connect_req.end(), dest_host.begin(), dest_host.end());
    connect_req.push_back(static_cast<unsigned char>((dest_port >> 8) & 0xFF));
    connect_req.push_back(static_cast<unsigned char>(dest_port & 0xFF));
    co_await asio::async_write(socket, asio::buffer(connect_req), asio::use_awaitable);

    // Reply: VER, REP, RSV, ATYP, BND.ADDR, BND.PORT
    std::array<unsigned char, 4> head{};
    co_await asio::async_read(socket, asio::buffer(head), asio::use_awaitable);
    if (head[0] != 0x05) throw Error("socks5: bad version in connect reply");
    if (head[1] != 0x00) {
        throw Error("socks5: connect failed (code " + std::to_string(head[1]) + ")");
    }
    std::size_t addr_len = 0;
    switch (head[3]) {
        case 0x01: addr_len = 4; break;
        case 0x04: addr_len = 16; break;
        case 0x03: {
            std::array<unsigned char, 1> len{};
            co_await asio::async_read(socket, asio::buffer(len), asio::use_awaitable);
            addr_len = len[0];
            break;
        }
        default: throw Error("socks5: unknown address type in reply");
    }
    std::vector<unsigned char> rest(addr_len + 2);  // addr + port
    co_await asio::async_read(socket, asio::buffer(rest), asio::use_awaitable);

    co_return socket;
}

}  // namespace sbc::net
