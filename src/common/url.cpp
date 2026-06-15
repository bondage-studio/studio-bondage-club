#include "common/url.hpp"

#include <boost/url.hpp>

#include "common/error.hpp"

namespace sbc {

namespace urls = boost::urls;

namespace {

urls::url_view view_of(const std::string& buffer) {
    auto r = urls::parse_uri_reference(buffer);
    if (!r) throw Error("invalid url: " + buffer);
    return r.value();
}

std::uint16_t default_port_for(std::string_view scheme) {
    if (scheme == "https") return 443;
    if (scheme == "http") return 80;
    if (scheme == "socks5" || scheme == "socks5h") return 1080;
    if (scheme == "ws") return 80;
    if (scheme == "wss") return 443;
    return 0;
}

}  // namespace

Url Url::parse(std::string_view raw) {
    auto r = urls::parse_uri_reference(raw);
    if (!r) throw Error("invalid url: " + std::string(raw));
    Url u;
    u.buffer_ = r.value().buffer();
    return u;
}

std::optional<Url> Url::try_parse(std::string_view raw) {
    auto r = urls::parse_uri_reference(raw);
    if (!r) return std::nullopt;
    Url u;
    u.buffer_ = r.value().buffer();
    return u;
}

Url Url::resolve(const Url& base, std::string_view reference) {
    auto base_r = urls::parse_uri(base.buffer_);
    if (!base_r) throw Error("invalid base url: " + base.buffer_);
    auto ref_r = urls::parse_uri_reference(reference);
    if (!ref_r) throw Error("invalid url reference: " + std::string(reference));
    urls::url out;
    auto rc = urls::resolve(base_r.value(), ref_r.value(), out);
    if (rc.has_error()) throw Error("resolve url failed: " + std::string(reference));
    Url u;
    u.buffer_ = out.buffer();
    return u;
}

std::string Url::scheme() const {
    return std::string(view_of(buffer_).scheme());
}

std::string Url::host() const {
    return view_of(buffer_).host();
}

std::string Url::user() const {
    return view_of(buffer_).user();
}

std::string Url::password() const {
    return view_of(buffer_).password();
}

bool Url::has_userinfo() const {
    return view_of(buffer_).has_userinfo();
}

std::string Url::port_string() const {
    auto v = view_of(buffer_);
    return v.has_port() ? std::string(v.port()) : std::string();
}

std::uint16_t Url::port() const {
    auto v = view_of(buffer_);
    if (v.has_port()) return static_cast<std::uint16_t>(v.port_number());
    return default_port_for(v.scheme());
}

std::string Url::authority() const {
    return std::string(view_of(buffer_).encoded_authority());
}

std::string Url::path() const {
    return view_of(buffer_).path();
}

std::string Url::encoded_path() const {
    return std::string(view_of(buffer_).encoded_path());
}

std::string Url::query() const {
    return std::string(view_of(buffer_).encoded_query());
}

std::string Url::fragment() const {
    return std::string(view_of(buffer_).encoded_fragment());
}

bool Url::has_query() const {
    return view_of(buffer_).has_query();
}

void Url::set_path(std::string_view p) {
    auto r = urls::parse_uri_reference(buffer_);
    if (!r) throw Error("invalid url: " + buffer_);
    urls::url u = r.value();
    u.set_encoded_path(p);
    buffer_ = u.buffer();
}

void Url::set_query(std::string_view q) {
    auto r = urls::parse_uri_reference(buffer_);
    if (!r) throw Error("invalid url: " + buffer_);
    urls::url u = r.value();
    if (q.empty()) {
        u.remove_query();
    } else {
        u.set_encoded_query(q);
    }
    buffer_ = u.buffer();
}

void Url::clear_query_and_fragment() {
    auto r = urls::parse_uri_reference(buffer_);
    if (!r) throw Error("invalid url: " + buffer_);
    urls::url u = r.value();
    u.remove_query();
    u.remove_fragment();
    buffer_ = u.buffer();
}

void Url::ensure_trailing_slash() {
    auto r = urls::parse_uri_reference(buffer_);
    if (!r) throw Error("invalid url: " + buffer_);
    urls::url u = r.value();
    std::string p(u.encoded_path());
    if (p.empty() || p.back() != '/') {
        p += '/';
        u.set_encoded_path(p);
        buffer_ = u.buffer();
    }
}

std::string Url::origin() const {
    auto v = view_of(buffer_);
    std::string out(v.scheme());
    out += "://";
    out += std::string(v.encoded_authority());
    return out;
}

std::string Url::string() const {
    return buffer_;
}

}  // namespace sbc
