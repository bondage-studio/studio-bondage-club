#include "server/homepage.hpp"

#include <nlohmann/json.hpp>

#include "server/api_util.hpp"

namespace sbc::server {

namespace asio = boost::asio;

namespace {

constexpr const char* kAdminRootID = "studio-admin-root";
constexpr const char* kBootstrapScriptID = "studio-bootstrap-data";
constexpr const char* kStatusRootID = "studio-homepage-status";

std::string trim_right_slash(const std::string& s) {
    std::string out = s;
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

std::string html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&#34;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

}  // namespace

asio::awaitable<void> serve_homepage_shell(Request& req, ResponseWriter& w,
                                           const std::string& upstream,
                                           const std::string& admin_base_path,
                                           bool local_game_server) {
    if (!req.is_get() && !req.is_head()) {
        co_await method_not_allowed(w);
        co_return;
    }

    nlohmann::ordered_json bootstrap;
    bootstrap["homepageSourcePath"] = "/api/homepage";
    bootstrap["upstreamBase"] = upstream;
    bootstrap["serviceWorkerPath"] = kServiceWorkerPath;
    bootstrap["adminRootID"] = kAdminRootID;
    bootstrap["statusRootID"] = kStatusRootID;
    // Boot-time default for the local/remote game-server switch; a localStorage
    // override (set when the user flips the panel toggle) takes precedence.
    bootstrap["defaultLocalGameServer"] = local_game_server;

    std::string script_path = trim_right_slash(admin_base_path) + "/assets/studio-panel.js";
    const std::string sid = kStatusRootID;

    std::string html =
        "<!doctype html>\n"
        "<html lang=\"en\">\n"
        "  <head>\n"
        "    <meta charset=\"utf-8\">\n"
        "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "    <title>Studio Bondage Club</title>\n"
        "    <style>\n"
        "      body {\n"
        "        margin: 0;\n"
        "        min-width: 320px;\n"
        "        min-height: 100vh;\n"
        "        color: #172126;\n"
        "        background: #eef3f3;\n"
        "        font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, \"Segoe "
        "UI\", sans-serif;\n"
        "      }\n"
        "      #" + sid + " {\n"
        "        display: grid;\n"
        "        min-height: 100vh;\n"
        "        place-items: center;\n"
        "        padding: 24px;\n"
        "      }\n"
        "      #" + sid + " > section {\n"
        "        width: min(520px, 100%);\n"
        "        border: 1px solid #d3dee1;\n"
        "        border-radius: 8px;\n"
        "        padding: 20px;\n"
        "        background: #ffffff;\n"
        "        box-shadow: 0 18px 44px rgba(18, 42, 50, 0.14);\n"
        "      }\n"
        "      #" + sid + " h1 { margin: 0 0 8px; font-size: 1.25rem; line-height: 1.2; }\n"
        "      #" + sid + " p { margin: 0; color: #5f6d74; line-height: 1.45; }\n"
        "      #" + sid + "[data-state=\"error\"] > section { border-color: #deb4af; background: "
        "#fff8f7; }\n"
        "      #" + sid + "[data-state=\"error\"] p { color: #7a2822; }\n"
        "    </style>\n"
        "    <script id=\"" + kBootstrapScriptID + "\" type=\"application/json\">" +
        bootstrap.dump() +
        "</script>\n"
        "  </head>\n"
        "  <body>\n"
        "    <main id=\"" + sid + "\" data-state=\"loading\">\n"
        "      <section>\n"
        "        <h1>Studio Bondage Club</h1>\n"
        "        <p data-studio-status-text>Loading local panel and homepage cache.</p>\n"
        "      </section>\n"
        "    </main>\n"
        "    <div id=\"" + std::string(kAdminRootID) + "\"></div>\n"
        "    <script type=\"module\" src=\"" + html_escape(script_path) + "\"></script>\n"
        "  </body>\n"
        "</html>";

    HeaderMap headers;
    headers.set("Content-Type", "text/html; charset=utf-8");
    headers.set("Cache-Control", "no-store");
    if (req.is_head()) {
        co_await w.send_header(200, std::move(headers), static_cast<std::int64_t>(html.size()));
        co_await w.finish();
    } else {
        co_await w.write_full(200, std::move(headers), std::move(html));
    }
}

}  // namespace sbc::server
