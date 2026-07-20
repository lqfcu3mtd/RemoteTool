#pragma once
// Thread-safe event queue for UI updates from the network thread.
// CODING_STANDARDS.md section 6: GUI thread must not run io_context;
// network events are posted to this queue and consumed by the UI thread
// via WM_TIMER.

#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace rmt::gui {

class EventQueue {
public:
    EventQueue() = default;

    // Push from any thread (typically the network thread).
    void push(std::function<void()> action) {
        std::lock_guard<std::mutex> lk(m_);
        q_.push_back(std::move(action));
    }

    // Drain and execute all pending events on the calling thread (UI thread).
    void process() {
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard<std::mutex> lk(m_);
            batch.assign(std::make_move_iterator(q_.begin()),
                         std::make_move_iterator(q_.end()));
            q_.clear();
        }
        for (auto& fn : batch) {
            if (fn) fn();
        }
    }

    bool empty() const {
        std::lock_guard<std::mutex> lk(m_);
        return q_.empty();
    }

private:
    mutable std::mutex m_;
    std::deque<std::function<void()>> q_;
};

}  // namespace rmt::gui
