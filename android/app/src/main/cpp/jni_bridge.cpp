#include <jni.h>

#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <spdlog/sinks/android_sink.h>
#include <spdlog/spdlog.h>

#include "cache/backend.hpp"
#include "host/provider.hpp"
#include "server/embedded_server.hpp"

namespace {

std::mutex g_mu;
std::unique_ptr<sbc::server::EmbeddedServer> g_server;
bool g_logging_ready = false;

// Cached at JNI_OnLoad for the native->web push path (NativeRpc.dispatchToWeb).
JavaVM* g_vm = nullptr;
jclass g_native_rpc_cls = nullptr;
jmethodID g_dispatch_mid = nullptr;

// Cached at JNI_OnLoad for the direct cache-serve path (NativeCache.nativeProbe
// constructs a NativeCacheHit to hand back to Kotlin).
jclass g_cache_hit_cls = nullptr;
jmethodID g_cache_hit_ctor = nullptr;

std::string to_string(JNIEnv* env, jstring s) {
    if (s == nullptr) return {};
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out(chars != nullptr ? chars : "");
    if (chars != nullptr) env->ReleaseStringUTFChars(s, chars);
    return out;
}

// The RPC sender fires on Asio worker threads, which are not attached to the JVM.
// Attach lazily per thread and detach when the thread exits (TLS guard dtor), so
// we never leak an attachment or abort the VM by exiting an attached thread.
struct JniThread {
    JNIEnv* env = nullptr;
    bool attached = false;
    ~JniThread() {
        if (attached && g_vm != nullptr) g_vm->DetachCurrentThread();
    }
};
thread_local JniThread t_jni;

JNIEnv* jni_env() {
    if (t_jni.env != nullptr) return t_jni.env;
    if (g_vm == nullptr) return nullptr;
    jint rc = g_vm->GetEnv(reinterpret_cast<void**>(&t_jni.env), JNI_VERSION_1_6);
    if (rc == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&t_jni.env, nullptr) != JNI_OK) {
            t_jni.env = nullptr;
            return nullptr;
        }
        t_jni.attached = true;
    } else if (rc != JNI_OK) {
        t_jni.env = nullptr;
    }
    return t_jni.env;
}

// Push one outbound RPC frame (res/event) to the Kotlin NativeRpc.dispatchToWeb,
// which marshals it to the WebView bridge. Called from Asio worker threads.
void dispatch_to_web(const std::string& frame) {
    JNIEnv* env = jni_env();
    if (env == nullptr || g_native_rpc_cls == nullptr || g_dispatch_mid == nullptr) return;
    jstring s = env->NewStringUTF(frame.c_str());
    env->CallStaticVoidMethod(g_native_rpc_cls, g_dispatch_mid, s);
    if (s != nullptr) env->DeleteLocalRef(s);
    if (env->ExceptionCheck()) env->ExceptionClear();
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

// Whether config.android.hardwareAcceleration is enabled. The gecko flavor reads
// this to decide whether to force WebRender/accelerated-canvas Gecko prefs.
// Defaults to true when the server isn't running (it is started before this is
// queried in onCreate).
extern "C" JNIEXPORT jboolean JNICALL
Java_com_studio_bondageclub_NativeServer_nativeHardwareAccelerationEnabled(JNIEnv* /*env*/,
                                                                           jobject /*thiz*/) {
    std::lock_guard<std::mutex> lock(g_mu);
    bool enabled = g_server ? g_server->hardware_acceleration() : true;
    return enabled ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_studio_bondageclub_NativeServer_nativeStop(JNIEnv* /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_server) {
        g_server->stop();
        g_server.reset();
    }
}

// --- Native RPC bridge (NativeRpc.kt) ---

// nativeInit wires the outbound sink once, after the server is up. Idempotent.
extern "C" JNIEXPORT void JNICALL
Java_com_studio_bondageclub_NativeRpc_nativeInit(JNIEnv* /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_server) {
        g_server->set_rpc_sender([](std::string frame) { dispatch_to_web(frame); });
    }
}

// nativeDeliver feeds one inbound JSON frame (with its capability token) from the
// WebView bridge into the RPC dispatcher.
extern "C" JNIEXPORT void JNICALL Java_com_studio_bondageclub_NativeRpc_nativeDeliver(
    JNIEnv* env, jobject /*thiz*/, jstring message) {
    std::string frame = to_string(env, message);
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_server) {
        g_server->deliver_rpc_frame(std::move(frame));
    }
}

// nativeReset tears down the live session (page navigation / activity destroy).
extern "C" JNIEXPORT void JNICALL
Java_com_studio_bondageclub_NativeRpc_nativeReset(JNIEnv* /*env*/, jobject /*thiz*/) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_server) {
        g_server->reset_rpc();
    }
}

// --- Direct cache serving (NativeCache.kt) ---
// A clean fresh HIT hands back a NativeCacheHit whose `handle` is a heap-owned
// host::CacheHit*; the body is read lazily via nativeReadBody and the handle freed
// via nativeFree when the response stream closes. nativeReadBody/nativeFree take no
// global lock — the CacheHit owns its store shared_ptr, so the read stays safe even
// while the server is being torn down.

// nativeProbe returns a NativeCacheHit on a confirmed fresh HIT, else null.
// |headers| is a flat [name, value, ...] array.
extern "C" JNIEXPORT jobject JNICALL Java_com_studio_bondageclub_NativeCache_nativeProbe(
    JNIEnv* env, jobject /*thiz*/, jstring method, jstring target, jobjectArray headers) {
    const std::string method_str = to_string(env, method);
    const std::string target_str = to_string(env, target);

    sbc::HeaderMap header_map;
    if (headers != nullptr) {
        const jsize n = env->GetArrayLength(headers);
        for (jsize i = 0; i + 1 < n; i += 2) {
            auto name = static_cast<jstring>(env->GetObjectArrayElement(headers, i));
            auto value = static_cast<jstring>(env->GetObjectArrayElement(headers, i + 1));
            header_map.add(to_string(env, name), to_string(env, value));
            if (name != nullptr) env->DeleteLocalRef(name);
            if (value != nullptr) env->DeleteLocalRef(value);
        }
    }

    std::optional<sbc::host::CacheHit> hit;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!g_server) return nullptr;
        hit = g_server->probe_cache(method_str, target_str, header_map);
    }
    if (!hit) return nullptr;
    if (g_cache_hit_cls == nullptr || g_cache_hit_ctor == nullptr) return nullptr;

    auto* heap = new sbc::host::CacheHit(std::move(*hit));

    // Flatten the serve-ready response headers into a String[] of name,value,...
    const auto& entries = heap->header.entries();
    jclass string_cls = env->FindClass("java/lang/String");
    jobjectArray header_arr =
        env->NewObjectArray(static_cast<jsize>(entries.size() * 2), string_cls, nullptr);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        jstring name = env->NewStringUTF(entries[i].first.c_str());
        jstring value = env->NewStringUTF(entries[i].second.c_str());
        env->SetObjectArrayElement(header_arr, static_cast<jsize>(i * 2), name);
        env->SetObjectArrayElement(header_arr, static_cast<jsize>(i * 2 + 1), value);
        env->DeleteLocalRef(name);
        env->DeleteLocalRef(value);
    }
    env->DeleteLocalRef(string_cls);

    return env->NewObject(g_cache_hit_cls, g_cache_hit_ctor, reinterpret_cast<jlong>(heap),
                          static_cast<jint>(heap->status_code), header_arr);
}

// nativeReadBody reads the HIT's body bytes (lazily, on the WebView stream thread).
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_studio_bondageclub_NativeCache_nativeReadBody(
    JNIEnv* env, jobject /*thiz*/, jlong handle) {
    auto* hit = reinterpret_cast<sbc::host::CacheHit*>(handle);
    std::string body;
    if (hit != nullptr && hit->store) {
        try {
            body = hit->store->open_body(hit->key);
        } catch (const std::exception& e) {
            spdlog::warn("android cache read: key={} error={}", hit->key, e.what());
        }
    }
    jbyteArray out = env->NewByteArray(static_cast<jsize>(body.size()));
    if (!body.empty()) {
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(body.size()),
                                reinterpret_cast<const jbyte*>(body.data()));
    }
    return out;
}

// nativeFree releases the heap-owned CacheHit once the response stream is closed.
extern "C" JNIEXPORT void JNICALL Java_com_studio_bondageclub_NativeCache_nativeFree(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    delete reinterpret_cast<sbc::host::CacheHit*>(handle);
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    // Cache NativeRpc.dispatchToWeb for the native->web push path. FindClass here
    // resolves against the app classloader (JNI_OnLoad runs on the loading thread).
    jclass cls = env->FindClass("com/studio/bondageclub/NativeRpc");
    if (cls != nullptr) {
        g_native_rpc_cls = static_cast<jclass>(env->NewGlobalRef(cls));
        g_dispatch_mid = env->GetStaticMethodID(cls, "dispatchToWeb", "(Ljava/lang/String;)V");
        env->DeleteLocalRef(cls);
    }
    // Cache NativeCacheHit's ctor for the direct cache-serve path.
    jclass hit_cls = env->FindClass("com/studio/bondageclub/NativeCacheHit");
    if (hit_cls != nullptr) {
        g_cache_hit_cls = static_cast<jclass>(env->NewGlobalRef(hit_cls));
        g_cache_hit_ctor = env->GetMethodID(hit_cls, "<init>", "(JI[Ljava/lang/String;)V");
        env->DeleteLocalRef(hit_cls);
    }
    return JNI_VERSION_1_6;
}
