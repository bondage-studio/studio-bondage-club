#include "server/static_assets.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "server/api_util.hpp"

#if defined(SBC_EMBED_WEB)
#include "server/embedded_assets.hpp"
#endif

namespace sbc::server {

namespace asio = boost::asio;
namespace fs = std::filesystem;

namespace {

// FilesystemAssetSource reads from a web/dist directory, guarding against path
// traversal by ensuring the resolved path stays within the root.
class FilesystemAssetSource : public AssetSource {
public:
    explicit FilesystemAssetSource(fs::path root) : root_(std::move(root)) {}

    bool available() const override {
        std::error_code ec;
        return fs::is_directory(root_, ec);
    }

    std::optional<std::string> read(const std::string& rel_path) override {
        fs::path candidate = root_ / rel_path;
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(candidate, ec);
        if (ec) return std::nullopt;
        fs::path root_canonical = fs::weakly_canonical(root_, ec);
        if (ec) return std::nullopt;
        // Ensure canonical is within root_canonical. Use generic_string() (always
        // std::string with '/' separators) rather than native() — the latter is a
        // std::wstring on Windows, where rfind() rejects a narrow string literal.
        std::string rel = canonical.lexically_relative(root_canonical).generic_string();
        if (rel.empty() || rel.rfind("..", 0) == 0) return std::nullopt;
        if (!fs::is_regular_file(canonical, ec)) return std::nullopt;
        std::ifstream in(canonical, std::ios::binary);
        if (!in) return std::nullopt;
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

private:
    fs::path root_;
};

fs::path resolve_web_root() {
    if (const char* env = std::getenv("SBC_WEB_ROOT"); env && *env) return fs::path(env);
    return fs::path("web") / "dist";
}

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::string content_type_for(const std::string& filename) {
    static const std::unordered_map<std::string, std::string> kTypes = {
        {".html", "text/html; charset=utf-8"},
        {".htm", "text/html; charset=utf-8"},
        {".js", "text/javascript; charset=utf-8"},
        {".mjs", "text/javascript; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".json", "application/json"},
        {".map", "application/json"},
        {".svg", "image/svg+xml"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".webp", "image/webp"},
        {".ico", "image/x-icon"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".txt", "text/plain; charset=utf-8"},
        {".wasm", "application/wasm"},
    };
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    auto it = kTypes.find(to_lower(filename.substr(dot)));
    return it == kTypes.end() ? "application/octet-stream" : it->second;
}

std::shared_ptr<AssetSource> default_asset_source() {
#if defined(SBC_EMBED_WEB)
    return make_embedded_asset_source();
#else
    return std::make_shared<FilesystemAssetSource>(resolve_web_root());
#endif
}

namespace {

asio::awaitable<void> serve_bytes(Request& req, ResponseWriter& w, int status, HeaderMap headers,
                                  std::string body) {
    if (req.is_head()) {
        co_await w.send_header(status, std::move(headers), static_cast<std::int64_t>(body.size()));
        co_await w.finish();
    } else {
        co_await w.write_full(status, std::move(headers), std::move(body));
    }
}

}  // namespace

asio::awaitable<void> serve_web_asset(Request& req, ResponseWriter& w, const std::string& base_path,
                                      AssetSource& source) {
    if (!req.is_get() && !req.is_head()) {
        co_await method_not_allowed(w);
        co_return;
    }
    if (!source.available()) {
        co_await not_found(w);
        co_return;
    }

    std::string rel = req.path;
    if (rel.rfind(base_path, 0) == 0) rel = rel.substr(base_path.size());
    if (rel.empty() || rel == "/" || rel.rfind("../", 0) == 0) {
        co_await not_found(w);
        co_return;
    }
    // Normalize: drop any leading slashes.
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());

    auto body = source.read(rel);
    if (!body) {
        co_await not_found(w);
        co_return;
    }

    HeaderMap headers;
    headers.set("Content-Type", content_type_for(rel));
    headers.set("Cache-Control", "no-cache");
    co_await serve_bytes(req, w, 200, std::move(headers), std::move(*body));
}

asio::awaitable<void> serve_service_worker(Request& req, ResponseWriter& w, AssetSource& source) {
    if (!req.is_get() && !req.is_head()) {
        co_await method_not_allowed(w);
        co_return;
    }
    auto body = source.read("studio-service-worker.js");
    if (!body) {
        co_await not_found(w);
        co_return;
    }
    HeaderMap headers;
    headers.set("Content-Type", "application/javascript; charset=utf-8");
    headers.set("Cache-Control", "no-store");
    headers.set("Service-Worker-Allowed", "/");
    co_await serve_bytes(req, w, 200, std::move(headers), std::move(*body));
}

}  // namespace sbc::server
