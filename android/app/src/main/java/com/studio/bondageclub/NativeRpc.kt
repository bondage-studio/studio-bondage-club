package com.studio.bondageclub

/**
 * In-process RPC bridge between the WebView page and the C++ core, bypassing the
 * localhost WebSocket hop. The flavor host (system: WebMessageListener; gecko: a
 * built-in WebExtension port) installs [onOutbound] to forward native->web frames
 * and calls [nativeDeliver] for each web->native frame.
 *
 * Security is unchanged from the WebSocket path: the bridge object injected into
 * the page is visible to all page JS, so the C++ side verifies the capability
 * token on every inbound frame (see EmbeddedServer::deliver_rpc_frame). This
 * object only moves opaque JSON strings.
 */
object NativeRpc {
    init {
        System.loadLibrary("sbc_jni")
    }

    /** Sink for outbound frames (res/event). Invoked from native worker threads. */
    @Volatile
    var onOutbound: ((String) -> Unit)? = null

    /** Wire the native outbound sink once the server is running. Idempotent. */
    external fun nativeInit()

    /** Feed one inbound JSON frame (carrying its capability token) to the core. */
    external fun nativeDeliver(message: String)

    /** Tear down the live native session (page navigation / activity destroy). */
    external fun nativeReset()

    /** Called from native (any thread) with each outbound frame. */
    @JvmStatic
    fun dispatchToWeb(message: String) {
        onOutbound?.invoke(message)
    }
}
