package com.studio.bondageclub

/**
 * Direct cache-serving bridge for the system flavor: lets the WebViewClient serve
 * a confirmed-fresh cache HIT straight from the C++ store, skipping the localhost
 * loopback. The analogue of the desktop CEF CacheResourceHandler.
 *
 * [nativeProbe] does a fast metadata-only DB read and returns a [NativeCacheHit]
 * only for a clean, fresh, unconditional GET hit (anything that would revalidate,
 * range, or miss returns null so the caller falls back to the normal path). The
 * body is read lazily via [nativeReadBody] off the interception thread, and the
 * handle released via [nativeFree] when the response stream closes.
 */
object NativeCache {
    init {
        System.loadLibrary("sbc_jni")
    }

    /** Probe for a fresh HIT. [headers] is a flat [name, value, ...] array. */
    external fun nativeProbe(method: String, target: String, headers: Array<String>): NativeCacheHit?

    /** Read the HIT's body bytes. Call off the interception thread. */
    external fun nativeReadBody(handle: Long): ByteArray

    /** Release the heap-owned native CacheHit. Call exactly once. */
    external fun nativeFree(handle: Long)
}
