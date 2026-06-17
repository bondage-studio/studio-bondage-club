#include "notifier.hpp"

#include <utility>

#include "dbus_notifier.hpp"
#include "notify_send_notifier.hpp"

namespace sbc::desktop {

std::unique_ptr<Notifier> CreateDefaultNotifier(std::string app_name) {
    // Prefer talking to the notification daemon directly over D-Bus (no child
    // process, exact tag-replace via the returned id). It is null when D-Bus was
    // not compiled in or the session bus is unreachable; then fall back to the
    // notify-send CLI, which self-no-ops when even that is unavailable.
    if (auto dbus = TryCreateDBusNotifier(app_name)) {
        return dbus;
    }
    return std::make_unique<NotifySendNotifier>(std::move(app_name));
}

}  // namespace sbc::desktop