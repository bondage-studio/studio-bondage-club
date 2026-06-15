#pragma once

#include <functional>
#include <string>

#include <boost/asio/awaitable.hpp>

#include "common/http_util.hpp"

namespace sbc::server {

// Request is a parsed inbound HTTP request, decoupled from Beast types so
// handlers and the App router stay framework-agnostic.
struct Request {
    std::string method;     // "GET", "POST", ...
    std::string target;     // raw request-target, e.g. "/Assets/foo.js?x=1"
    std::string path;       // decoded path component, e.g. "/Assets/foo.js"
    std::string raw_query;  // query string without leading '?'
    HeaderMap headers;
    std::string body;        // fully-read request body (bounded by MaxBodyBytes)
    std::string remote_address;  // peer IP
    unsigned version = 11;   // 11 = HTTP/1.1, 10 = HTTP/1.0
    bool keep_alive = true;

    bool is_get() const { return method == "GET"; }
    bool is_head() const { return method == "HEAD"; }
    bool is_options() const { return method == "OPTIONS"; }
    std::string host() const { return headers.get("Host"); }
};

class ResponseWriter;  // defined in response_writer.hpp

// Handler is the top-level request handler signature (App::serve).
using Handler = std::function<boost::asio::awaitable<void>(Request&, ResponseWriter&)>;

}  // namespace sbc::server
