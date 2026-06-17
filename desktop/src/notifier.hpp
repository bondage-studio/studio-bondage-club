#pragma once

#include <memory>
#include <string>

namespace sbc::desktop {

// Notifier presents OS desktop notifications. CEF's (Alloy) runtime wires no
// notification presenter, so the page's Web Notifications — forwarded from the
// renderer's window.Notification shim (SbcRenderProcessHandler) to the browser
// process (SbcClient) — are surfaced through an implementation of this
// interface. This is the desktop analogue of Android's GeckoWebNotification.
//
// The backend is intentionally abstracted: the freedesktop ecosystem offers
// several ways to raise a notification (the notify-send CLI, a direct D-Bus call
// to org.freedesktop.Notifications, libnotify, ...) with different trade-offs.
// SbcClient depends only on this interface; CreateDefaultNotifier picks a
// concrete one.
class Notifier {
public:
    virtual ~Notifier() = default;

    // Show presents a notification, or — when |tag| is non-empty and a
    // notification with that tag is still visible — replaces it in place
    // (mirroring the Android tag behaviour). Called on the CEF UI thread;
    // implementations must not block it.
    virtual void Show(const std::string& title, const std::string& body,
                      const std::string& tag) = 0;
};

// Builds the notifier best suited to the current environment, labelling
// notifications with |app_name|. Never returns null.
std::unique_ptr<Notifier> CreateDefaultNotifier(std::string app_name);

}  // namespace sbc::desktop