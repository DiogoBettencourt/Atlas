#pragma once

// Internal helper for the streaming backends (not part of the public API):
// a background poller that calls `on_cancel` as soon as the CancelCheck turns
// true. The backends use it to close the connection to the model server, which
// is what makes a request that is waiting for the first token (while the
// server is still reading the prompt) stop right away instead of only at the
// next chunk.
//
// Destroying the watch stops and joins the thread, so it must be declared
// after (destroyed before) anything `on_cancel` touches.

#include "atlas/agent/LLMBackend.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace atlas::agent {

class CancelWatch {
public:
    CancelWatch(CancelCheck check, std::function<void()> on_cancel) {
        if (!check) return;
        thread_ = std::thread([this, check = std::move(check), on_cancel = std::move(on_cancel)] {
            std::unique_lock<std::mutex> lock(mutex_);
            while (!done_) {
                // Keeps calling on_cancel while the check stays true: the first
                // call can land before the connection exists, and then it
                // would be a no-op.
                if (check()) on_cancel();
                wake_.wait_for(lock, std::chrono::milliseconds(50), [this] { return done_; });
            }
        });
    }

    CancelWatch(const CancelWatch&) = delete;
    CancelWatch& operator=(const CancelWatch&) = delete;

    ~CancelWatch() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

private:
    std::mutex mutex_;
    std::condition_variable wake_;
    bool done_ = false;
    std::thread thread_;
};

} // namespace atlas::agent
