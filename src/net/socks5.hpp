#pragma once

#include <cstdint>
#include <string>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace sbc {
class Url;
}

namespace sbc::net {

struct Socks5Config {
    std::string host;
    std::uint16_t port = 1080;
    std::string user;      // empty -> no authentication
    std::string password;
};

// socks5_config_from_url builds a Socks5Config from a parsed socks5:// URL.
Socks5Config socks5_config_from_url(const Url& url);

// socks5_connect performs an RFC 1928 (+ 1929 user/pass auth) handshake through
// the proxy and returns a socket connected to dest_host:dest_port. The proxy
// resolves the destination (domain ATYP), matching socks5h behaviour. Throws on
// any protocol/connection error.
boost::asio::awaitable<boost::asio::ip::tcp::socket> socks5_connect(
    boost::asio::any_io_executor executor, Socks5Config config, std::string dest_host,
    std::uint16_t dest_port);

}  // namespace sbc::net
