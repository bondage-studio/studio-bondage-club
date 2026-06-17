// Linux desktop client entry point: a single executable that serves as both the
// CEF browser process and (re-launched with --type=...) its renderer/gpu/utility
// subprocesses. In the browser process it starts the embedded server, then opens
// a Chromium window pointed at the local origin and runs the CEF message loop.

#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_task.h"
#include "include/cef_version.h"
#include "include/wrapper/cef_closure_task.h"

#include "config/config.hpp"
#include "platform/paths.hpp"
#include "server/config_listener.hpp"
#include "server/embedded_server.hpp"

#include "sbc_app.hpp"
#include "sbc_client.hpp"

using namespace sbc;

namespace {

// parse_config_flag extracts -config/--config <path> from argv (matches src/main.cpp).
std::string parse_config_flag(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "-config" || args[i] == "--config") && i + 1 < args.size()) {
            return args[i + 1];
        }
        const std::string prefix = "-config=";
        const std::string prefix2 = "--config=";
        if (args[i].rfind(prefix, 0) == 0) return args[i].substr(prefix.size());
        if (args[i].rfind(prefix2, 0) == 0) return args[i].substr(prefix2.size());
    }
    return {};
}

// build_user_agent tags the default Chrome UA with "StudioBC-Desktop" so the web
// bundle's isNativeRuntime() recognises this as a native host (skips the service
// worker; uses the injected RPC bridge). The Chrome product token is preserved so
// the game's own UA sniffing is unaffected.
std::string build_user_agent() {
    return std::string(
               "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/") +
           std::to_string(CHROME_VERSION_MAJOR) + "." + std::to_string(CHROME_VERSION_MINOR) + "." +
           std::to_string(CHROME_VERSION_BUILD) + "." + std::to_string(CHROME_VERSION_PATCH) +
           " Safari/537.36 StudioBC-Desktop";
}

}  // namespace

int main(int argc, char* argv[]) {
    CefMainArgs main_args(argc, argv);
    CefRefPtr<desktop::SbcApp> app(new desktop::SbcApp());

    // Subprocess dispatch. In a renderer/gpu/utility subprocess this returns the
    // exit code (>= 0) after running it to completion; the embedded server is
    // only ever started in the browser process below.
    int exit_code = CefExecuteProcess(main_args, app, nullptr);
    if (exit_code >= 0) {
        return exit_code;
    }

    // --- Browser process ---
    spdlog::set_pattern("time=%Y-%m-%dT%H:%M:%SZ level=%l %v", spdlog::pattern_time_type::utc);

    const std::string config_path = parse_config_flag(argc, argv);

    auto server = std::make_unique<server::EmbeddedServer>();
    std::string address;
    try {
        address = server->start(config_path, /*host_override=*/"127.0.0.1", /*port_override=*/0);
    } catch (const std::exception& e) {
        spdlog::error("start error={}", e.what());
        return 1;
    }

    CefRefPtr<desktop::SbcClient> client(new desktop::SbcClient(server.get(), "http://" + address));
    app->SetBrowserConfig(client, "http://" + address + "/");

    // Desktop config: seed the window size + GPU switch (read before CefInitialize),
    // and react live to panel edits. The listener fires on an Asio worker, so hop
    // to the CEF UI thread before touching the window. Restart-tier changes (GPU)
    // are already persisted; they apply on the next launch.
    const config::DesktopConfig& dc = server->desktop_config();
    app->SetDesktopConfig(server.get(), dc.hardware_acceleration, dc.window_width, dc.window_height);
    server->on_config_change(
        server::ConfigPhase::Notify, "desktop",
        [client](const config::Config& /*old_cfg*/, const config::Config& new_cfg) {
            const int w = new_cfg.desktop.window_width;
            const int h = new_cfg.desktop.window_height;
            CefPostTask(TID_UI, base::BindOnce(&desktop::SbcClient::ApplyDesktopConfig, client, w, h));
        });

    CefSettings settings;
    settings.no_sandbox = true;
    const std::string cache_dir =
        (platform::user_cache_dir() / "studio-bondage-club" / "cef").string();
    CefString(&settings.root_cache_path).FromString(cache_dir);
    const std::string user_agent = build_user_agent();
    CefString(&settings.user_agent).FromString(user_agent);

    CefInitialize(main_args, settings, app, nullptr);
    CefRunMessageLoop();
    CefShutdown();

    server->stop();
    return 0;
}
