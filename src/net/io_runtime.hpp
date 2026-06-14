#pragma once

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <cstddef>
#include <optional>
#include <thread>
#include <vector>

namespace sbc::net {

// IoRuntime is the single owner of the Asio io_context and its worker threads.
// All network coroutines run on this context. RAII: the constructor starts the
// worker pool; the destructor stops and joins it.
class IoRuntime {
public:
    explicit IoRuntime(std::size_t threads = 0);
    ~IoRuntime();

    IoRuntime(const IoRuntime&) = delete;
    IoRuntime& operator=(const IoRuntime&) = delete;

    boost::asio::io_context& context() { return ioc_; }
    boost::asio::io_context::executor_type executor() { return ioc_.get_executor(); }

    [[nodiscard]] std::size_t thread_count() const { return threads_.size(); }

    // stop releases the work guard and stops the io_context. Safe to call from
    // any thread (e.g. a signal handler context).
    void stop();

private:
    boost::asio::io_context ioc_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    std::vector<std::thread> threads_;
};

}  // namespace sbc::net
