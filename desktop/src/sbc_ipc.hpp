#pragma once

namespace sbc::desktop {

// CefProcessMessage names for the native RPC bridge, shared by the renderer
// (SbcRenderProcessHandler) and browser (SbcClient) sides. Each message carries
// a single string argument: the opaque JSON RPC frame.
inline constexpr char kRpcToBrowser[] = "sbc_rpc_out";  // page -> native
inline constexpr char kRpcToPage[] = "sbc_rpc_in";      // native -> page
inline constexpr char kNotifyShow[] = "sbc_notify_show";

}  // namespace sbc::desktop
