#pragma once

#include <string>

#include "notifier.hpp"

namespace sbc::desktop {

// Notifier backed by the `notify-send` CLI (libnotify-bin), present on
// essentially every Linux desktop. Each Show is fire-and-forget: the child is
// spawned and reaped on a detached thread, so it never blocks the CEF UI thread
// and leaves no zombie. If notify-send is missing the spawn fails and Show
// silently no-ops. A non-empty tag is mapped to the freedesktop "synchronous"
// hint so a repeat with the same tag replaces the previous notification in place
// on compliant daemons (GNOME, KDE, dunst).
class NotifySendNotifier : public Notifier {
public:
    explicit NotifySendNotifier(std::string app_name);

    void Show(const std::string& title, const std::string& body,
              const std::string& tag) override;

private:
    std::string app_name_;
};

}  // namespace sbc::desktop