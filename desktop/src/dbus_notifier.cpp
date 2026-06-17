#include "dbus_notifier.hpp"

#ifdef SBC_HAVE_DBUS

#include <dbus/dbus.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace sbc::desktop {

namespace {

constexpr char kService[] = "org.freedesktop.Notifications";
constexpr char kPath[] = "/org/freedesktop/Notifications";
constexpr char kInterface[] = "org.freedesktop.Notifications";

// Appends one a{sv} dict entry { key: <string value> } to an open hints array.
void AppendStringHint(DBusMessageIter* hints, const char* key, const std::string& value) {
    DBusMessageIter entry;
    dbus_message_iter_open_container(hints, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    DBusMessageIter variant;
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    const char* v = value.c_str();
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &v);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(hints, &entry);
}

// Notifier backed by a direct D-Bus connection to the notification daemon.
//
// libdbus connections are single-threaded, so the owned connection is used only
// from a dedicated worker thread; Show() just enqueues a request and returns,
// keeping the CEF UI thread unblocked. The daemon returns a notification id per
// Notify; we remember it per tag and pass it as replaces_id so a repeat with the
// same tag updates the existing notification in place (the Android tag behaviour,
// done precisely rather than relying on a daemon hint).
class DBusNotifier : public Notifier {
public:
    // Takes ownership of |conn| (a private session-bus connection).
    DBusNotifier(std::string app_name, DBusConnection* conn)
        : app_name_(std::move(app_name)), conn_(conn) {
        worker_ = std::thread([this] { Run(); });
    }

    ~DBusNotifier() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
        // Private connections must be explicitly closed before the final unref.
        dbus_connection_close(conn_);
        dbus_connection_unref(conn_);
    }

    void Show(const std::string& title, const std::string& body,
              const std::string& tag) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(Request{title, body, tag});
        }
        cv_.notify_one();
    }

private:
    struct Request {
        std::string title;
        std::string body;
        std::string tag;
    };

    void Run() {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) {
                return;  // woken only to stop
            }
            Request req = std::move(queue_.front());
            queue_.pop_front();
            lock.unlock();
            SendNotify(req);
            lock.lock();
        }
    }

    // Issues org.freedesktop.Notifications.Notify and records the returned id.
    // Runs on the worker thread, so ids_ needs no locking.
    void SendNotify(const Request& req) {
        DBusMessage* msg =
            dbus_message_new_method_call(kService, kPath, kInterface, "Notify");
        if (!msg) {
            return;
        }

        const char* app = app_name_.c_str();
        dbus_uint32_t replaces_id = 0;
        if (!req.tag.empty()) {
            auto it = ids_.find(req.tag);
            if (it != ids_.end()) {
                replaces_id = it->second;
            }
        }
        const char* icon = "";
        const char* summary = req.title.c_str();
        const char* body = req.body.c_str();
        dbus_int32_t expire_timeout = -1;  // daemon default

        DBusMessageIter args;
        dbus_message_iter_init_append(msg, &args);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &replaces_id);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &icon);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &summary);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &body);

        DBusMessageIter actions;  // empty as[]
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &actions);
        dbus_message_iter_close_container(&args, &actions);

        DBusMessageIter hints;  // a{sv}
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &hints);
        if (!req.tag.empty()) {
            // Belt-and-braces for daemons that key replacement off the tag hint
            // rather than replaces_id.
            AppendStringHint(&hints, "x-canonical-private-synchronous", req.tag);
        }
        dbus_message_iter_close_container(&args, &hints);

        dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &expire_timeout);

        DBusError err;
        dbus_error_init(&err);
        DBusMessage* reply =
            dbus_connection_send_with_reply_and_block(conn_, msg, kTimeoutMs, &err);
        dbus_message_unref(msg);

        if (reply) {
            dbus_uint32_t id = 0;
            if (dbus_message_get_args(reply, &err, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID) &&
                !req.tag.empty()) {
                ids_[req.tag] = id;
            }
            dbus_message_unref(reply);
        }
        if (dbus_error_is_set(&err)) {
            dbus_error_free(&err);
        }
    }

    static constexpr int kTimeoutMs = 3000;

    const std::string app_name_;
    DBusConnection* conn_;

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Request> queue_;
    bool stop_ = false;

    std::unordered_map<std::string, dbus_uint32_t> ids_;  // worker thread only
};

}  // namespace

std::unique_ptr<Notifier> TryCreateDBusNotifier(std::string app_name) {
    // Safe to call repeatedly; makes libdbus use its default thread locking.
    dbus_threads_init_default();

    DBusError err;
    dbus_error_init(&err);
    // A private connection so we fully own its lifetime (and its worker-thread
    // use does not race the shared one libdbus may hand out elsewhere).
    DBusConnection* conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
    }
    if (!conn) {
        return nullptr;  // no session bus — caller falls back
    }
    // Don't let a bus disconnect abort the whole process.
    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    return std::make_unique<DBusNotifier>(std::move(app_name), conn);
}

}  // namespace sbc::desktop

#else  // !SBC_HAVE_DBUS

namespace sbc::desktop {

std::unique_ptr<Notifier> TryCreateDBusNotifier(std::string /*app_name*/) {
    return nullptr;
}

}  // namespace sbc::desktop

#endif  // SBC_HAVE_DBUS