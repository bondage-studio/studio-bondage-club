#include "net/io_runtime.hpp"

#include <algorithm>

namespace sbc::net {

IoRuntime::IoRuntime(std::size_t threads) {
    if (threads == 0) {
        threads = std::max<std::size_t>(2, std::thread::hardware_concurrency());
    }
    work_.emplace(boost::asio::make_work_guard(ioc_));
    threads_.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i) {
        threads_.emplace_back([this] { ioc_.run(); });
    }
}

IoRuntime::~IoRuntime() {
    stop();
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
}

void IoRuntime::stop() {
    if (work_) {
        work_.reset();  // allow run() to return once outstanding work drains
    }
    ioc_.stop();
}

}  // namespace sbc::net
