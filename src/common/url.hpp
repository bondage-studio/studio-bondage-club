#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sbc {

// Url is a thin value wrapper over Boost.URL providing the URL behavior the
// proxy relies on. It centralizes URL handling so platform/test concerns stay in
// one place.
class Url {
public:
    Url() = default;

    static Url parse(std::string_view raw);
    static std::optional<Url> try_parse(std::string_view raw);

    static Url resolve(const Url& base, std::string_view reference);

    std::string scheme() const;        // "http" / "https" / "socks5" ...
    std::string host() const;          // hostname without port
    std::string user() const;          // decoded userinfo username ("" if none)
    std::string password() const;      // decoded userinfo password ("" if none)
    bool has_userinfo() const;
    std::uint16_t port() const;        // explicit or scheme default
    std::string port_string() const;   // "" if no explicit port
    std::string authority() const;     // host[:port]
    std::string path() const;          // decoded path
    std::string encoded_path() const;  // percent-encoded path
    std::string query() const;         // raw query (without '?')
    std::string fragment() const;      // raw fragment (without '#')

    bool has_query() const;
    bool is_https() const { return scheme() == "https"; }

    void set_path(std::string_view p);       // sets encoded path
    void set_query(std::string_view q);      // raw query, "" clears it
    void clear_query_and_fragment();
    void ensure_trailing_slash();

    std::string origin() const;
    std::string string() const;

private:
    // Stored as the serialized form to keep the header free of Boost includes.
    std::string buffer_;
};

}  // namespace sbc
