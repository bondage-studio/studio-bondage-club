#include "cache_resource_handler.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"

#include "common/http_util.hpp"
#include "server/embedded_server.hpp"

namespace sbc::desktop {

namespace {
// Media type without parameters ("text/html; charset=utf-8" -> "text/html").
// Chromium derives the resource type primarily from the mime type, so set it
// explicitly in addition to the Content-Type header.
std::string media_type(const std::string& content_type) {
    std::size_t semi = content_type.find(';');
    std::string mt = content_type.substr(0, semi);
    std::size_t b = mt.find_first_not_of(" \t");
    std::size_t e = mt.find_last_not_of(" \t");
    if (b == std::string::npos) return {};
    return mt.substr(b, e - b + 1);
}
}  // namespace

CacheResourceHandler::CacheResourceHandler(sbc::server::EmbeddedServer* server, host::CacheHit hit)
    : server_(server), hit_(std::move(hit)) {}

bool CacheResourceHandler::Open(CefRefPtr<CefRequest> /*request*/, bool& handle_request,
                                CefRefPtr<CefCallback> callback) {
    // Resolve asynchronously: read the body off the IO thread so a large cached
    // asset does not stall the network stack, then continue the load.
    handle_request = false;
    CefPostTask(TID_FILE_USER_BLOCKING,
                base::BindOnce(&CacheResourceHandler::ReadBodyThenContinue,
                               CefRefPtr<CacheResourceHandler>(this), callback));
    return true;
}

void CacheResourceHandler::ReadBodyThenContinue(CefRefPtr<CefCallback> callback) {
    body_ = server_->read_cache_body(hit_.store, hit_.key);
    callback->Continue();
}

void CacheResourceHandler::GetResponseHeaders(CefRefPtr<CefResponse> response,
                                              int64_t& response_length,
                                              CefString& /*redirectUrl*/) {
    response->SetStatus(hit_.status_code);
    if (std::string ctype = hit_.header.get("Content-Type"); !ctype.empty()) {
        if (std::string mt = media_type(ctype); !mt.empty()) response->SetMimeType(mt);
    }

    CefResponse::HeaderMap cef_headers;
    for (const auto& e : hit_.header.entries()) {
        // Content-Length is conveyed via response_length; the probe already
        // stripped it, but guard against re-adding a conflicting value.
        if (iequals(e.first, "Content-Length")) continue;
        cef_headers.insert({e.first, e.second});
    }
    response->SetHeaderMap(cef_headers);

    // Length from the bytes actually read: a clean HIT matches hit_.body_size, but
    // a body that vanished between probe and read collapses to an honest 0.
    response_length = static_cast<int64_t>(body_.size());
}

bool CacheResourceHandler::Skip(int64_t bytes_to_skip, int64_t& bytes_skipped,
                                CefRefPtr<CefResourceSkipCallback> /*callback*/) {
    std::size_t avail = body_.size() - std::min(offset_, body_.size());
    int64_t n = std::min<int64_t>(bytes_to_skip, static_cast<int64_t>(avail));
    offset_ += static_cast<std::size_t>(n);
    bytes_skipped = n;
    return true;
}

bool CacheResourceHandler::Read(void* data_out, int bytes_to_read, int& bytes_read,
                                CefRefPtr<CefResourceReadCallback> /*callback*/) {
    if (offset_ >= body_.size() || bytes_to_read <= 0) {
        bytes_read = 0;
        return false;  // response complete
    }
    std::size_t n = std::min(static_cast<std::size_t>(bytes_to_read), body_.size() - offset_);
    std::memcpy(data_out, body_.data() + offset_, n);
    offset_ += n;
    bytes_read = static_cast<int>(n);
    return true;
}

}  // namespace sbc::desktop