#pragma once

#include <memory>
#include <string>

#include "notifier.hpp"

namespace sbc::desktop {

// Builds a Notifier that talks to the desktop's notification daemon directly over
// D-Bus (org.freedesktop.Notifications), without spawning notify-send. Returns
// null when D-Bus support was not compiled in (libdbus-1 absent at build time) or
// the session bus is unreachable at runtime — the caller then falls back to
// another backend. The header pulls in no D-Bus types, so it is safe to include
// unconditionally.
std::unique_ptr<Notifier> TryCreateDBusNotifier(std::string app_name);

}  // namespace sbc::desktop