// JNI bridge between the Android host (Kotlin NativeServer) and the in-process
// C++ host (server::EmbeddedServer). The server runs entirely on its own Asio
// worker threads, so nativeStart() returns as soon as the listener is bound and
// the WebView can be pointed at the returned "host:port".

#include <jni.h>

#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>

#include <spdlog/sinks/android_sink.h>
#include <spdlog/spdlog.h>

#include "server/embedded_server.hpp"

namespace {

std::mutex g_mu;
std::unique_ptr<sbc::server::EmbeddedServer> g_server;
bool g_logging_ready = false;

std::string to_string(JNIEnv* env, jstring s) {
    if (s == nullptr) return {};
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out(chars != nullptr ? chars : "");
    if (chars != nullptr) env->ReleaseStringUTFChars(s, chars);
    return out;
}

// Route spdlog (which otherwise writes to stdout/stderr, invisible on Android)
// to logcat under the "StudioBC" tag, once.
void ensure_logging() {
    if (g_logging_ready) return;
    try {
        auto logger = spdlog::android_logger_mt("sbc", "StudioBC");
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("%v");
    } catch (...) {
        // If the sink can't be created, keep the default logger; not fatal.
    }
    g_logging_ready = true;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL Java_com_studio_bondageclub_NativeServer_nativeStart(
    JNIEnv* env, jobject /*thiz*/, jstring configDir, jstring cacheDir, jstring host, jint port) {
    std::lock_guard<std::mutex> lock(g_mu);
    ensure_logging();

    const std::string config_dir = to_string(env, configDir);
    const std::string cache_dir = to_string(env, cacheDir);
    const std::string host_str = to_string(env, host);

    // paths_android.cpp reads these to place the config + cache under the app's
    // private storage. Must be set before EmbeddedServer constructs the store.
    if (!config_dir.empty()) ::setenv("SBC_CONFIG_DIR", config_dir.c_str(), 1);
    if (!cache_dir.empty()) ::setenv("SBC_CACHE_DIR", cache_dir.c_str(), 1);

    if (g_server) {
        // Already running; report the existing bind so the caller can proceed.
        return env->NewStringUTF(host_str.c_str());
    }

    try {
        auto server = std::make_unique<sbc::server::EmbeddedServer>();
        std::string address =
            server->start(/*config_path=*/"", host_str, static_cast<std::uint16_t>(port));
        g_server = std::move(server);
        return env->NewStringUTF(address.c_str());
    } catch (const std::exception& e) {
        spdlog::error("native start failed error={}", e.what());
        // Empty return signals failure; the activity surfaces it to the user.
        return env->NewStringUTF("");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_studio_bondageclub_NativeServer_nativeStop(JNIEnv* /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_server) {
        g_server->stop();
        g_server.reset();
    }
}
