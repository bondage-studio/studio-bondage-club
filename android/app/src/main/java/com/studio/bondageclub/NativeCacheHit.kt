package com.studio.bondageclub

/**
 * A confirmed fresh cache HIT returned by [NativeCache.nativeProbe]. Constructed
 * from native code (jni_bridge.cpp).
 *
 * [handle] is an opaque pointer to a heap-owned C++ host::CacheHit. Read its body
 * with [NativeCache.nativeReadBody] and release it with [NativeCache.nativeFree]
 * exactly once (when the response stream is closed). [headers] is a flat array of
 * name, value, name, value, ... pairs (the serve-ready response head).
 */
class NativeCacheHit(
    val handle: Long,
    val statusCode: Int,
    val headers: Array<String>,
)
