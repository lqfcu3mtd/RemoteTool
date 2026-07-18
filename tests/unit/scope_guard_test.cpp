// ScopeGuard unit tests (CODING_STANDARDS.md section 6).
// Pure C++17, no sockets, no Windows APIs -> compiles under MinGW or MSVC.
#include <stdexcept>

#include "rmt/common/scope_guard.h"
#include "rmt/test.h"

using rmt::common::ScopeGuard;
using rmt::common::make_scope_guard;

namespace {

void run_scope_guard_tests() {
    // --- Normal scope exit: cleanup runs exactly once ---
    {
        int count = 0;
        {
            auto g = make_scope_guard([&] { ++count; });
        }
        RMT_CHECK(count == 1);
    }

    // --- Exception in scope (caught outside): cleanup still runs ---
    {
        int count = 0;
        try {
            auto g = make_scope_guard([&] { ++count; });
            throw std::runtime_error("boom");
        } catch (const std::runtime_error&) {
            // expected
        }
        RMT_CHECK(count == 1);
    }

    // --- dismiss(): cleanup does not run ---
    {
        int count = 0;
        {
            auto g = make_scope_guard([&] { ++count; });
            g.dismiss();
        }
        RMT_CHECK(count == 0);
    }

    // --- Move construction: new object cleans up, source does not ---
    {
        int count = 0;
        {
            auto g1 = make_scope_guard([&] { ++count; });
            auto g2 = std::move(g1);
        }
        RMT_CHECK(count == 1);
    }

    // --- Cleanup callable that throws: destructor swallows, no terminate ---
    // Verifies the documented behavior in the header: an exception escaping
    // the destructor would call std::terminate during stack unwinding.
    {
        bool reached_after_throw = false;
        {
            auto g = make_scope_guard([&] {
                reached_after_throw = true;
                throw std::runtime_error("cleanup boom");
            });
        }
        RMT_CHECK(reached_after_throw);
    }

    // --- Move-then-dismiss: dismissed new object does not run cleanup ---
    {
        int count = 0;
        {
            auto g1 = make_scope_guard([&] { ++count; });
            auto g2 = std::move(g1);
            g2.dismiss();
        }
        RMT_CHECK(count == 0);
    }
}

}  // namespace

int main() {
    run_scope_guard_tests();
    auto& c = rmt::test::ctx();
    std::printf("scope_guard_test: %d passed, %d failed\n", c.passed, c.failed);
    return c.ok() ? 0 : 1;
}
