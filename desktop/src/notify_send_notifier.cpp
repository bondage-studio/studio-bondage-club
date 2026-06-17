#include "notify_send_notifier.hpp"

#include <spawn.h>
#include <sys/wait.h>

#include <thread>
#include <utility>
#include <vector>

// POSIX exposes the process environment via this symbol; it is not declared in
// any header by default. posix_spawnp passes it as the child's environment so
// notify-send inherits DISPLAY/DBUS_SESSION_BUS_ADDRESS and reaches the daemon.
extern char** environ;

namespace sbc::desktop {

namespace {
constexpr char kNotifySend[] = "notify-send";
}  // namespace

NotifySendNotifier::NotifySendNotifier(std::string app_name)
    : app_name_(std::move(app_name)) {}

void NotifySendNotifier::Show(const std::string& title, const std::string& body,
                              const std::string& tag) {
    std::vector<std::string> argv;
    argv.emplace_back(kNotifySend);
    argv.emplace_back("-a");
    argv.emplace_back(app_name_);
    if (!tag.empty()) {
        // Honoured by GNOME/KDE/dunst to replace a prior notification with the
        // same tag instead of stacking a new one.
        argv.emplace_back("--hint=string:x-canonical-private-synchronous:" + tag);
    }
    // Terminate option parsing so a title/body beginning with '-' is never taken
    // for a flag.
    argv.emplace_back("--");
    argv.emplace_back(title);
    argv.emplace_back(body);

    std::thread([argv = std::move(argv)]() {
        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto& arg : argv) {
            c_argv.push_back(const_cast<char*>(arg.c_str()));
        }
        c_argv.push_back(nullptr);

        pid_t pid = 0;
        if (posix_spawnp(&pid, kNotifySend, nullptr, nullptr, c_argv.data(), environ) == 0) {
            // Reap the short-lived child so it never lingers as a zombie.
            int status = 0;
            waitpid(pid, &status, 0);
        }
    }).detach();
}

}  // namespace sbc::desktop