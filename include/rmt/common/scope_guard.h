#pragma once
// RAII scope guard: runs a cleanup callable on destruction unless dismissed.
// Used for Socket/Timer/handle cleanup per CODING_STANDARDS.md section 6.
//
// Exception safety in the destructor: the cleanup callable is invoked inside
// a try/catch(...) that swallows any exception. A throwing cleanup during
// stack unwinding (while another exception is already in flight) would
// otherwise call std::terminate. Callers that need to observe cleanup
// failures must do their own error handling inside the callable.
#include <type_traits>
#include <utility>

namespace rmt::common {

template <typename F>
class ScopeGuard {
public:
    // Construct from any callable. SFINAE prevents this from shadowing the
    // copy/move constructors when passed an existing ScopeGuard.
    template <typename G,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<G>, ScopeGuard>>>
    explicit ScopeGuard(G&& f) noexcept(std::is_nothrow_constructible_v<F, G&&>)
        : action_(std::forward<G>(f)) {}

    ScopeGuard(ScopeGuard&& other) noexcept
        : action_(std::move(other.action_)), dismissed_(other.dismissed_) {
        other.dismissed_ = true;
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

    ~ScopeGuard() noexcept {
        if (dismissed_) return;
        try {
            action_();
        } catch (...) {
            // Swallow: see file header note.
        }
    }

    // After dismiss(), destruction no longer invokes the cleanup callable.
    void dismiss() noexcept { dismissed_ = true; }

private:
    F action_;
    bool dismissed_ = false;
};

// Convenience factory. Return type deduces to ScopeGuard<std::decay_t<F>>.
template <typename F>
ScopeGuard<std::decay_t<F>> make_scope_guard(F&& f) {
    return ScopeGuard<std::decay_t<F>>(std::forward<F>(f));
}

}  // namespace rmt::common
