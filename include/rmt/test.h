#pragma once
// Minimal, dependency-free test harness for the dev-check build.
// The production build uses CTest per IMPLEMENTATION_PLAN.md; this header
// only exists so we can compile-check the platform-independent core with a
// plain compiler (MinGW/GCC or MSVC) before the user runs the real CMake build.
#include <cstdio>
#include <string_view>

namespace rmt::test {
struct Context {
    int passed = 0;
    int failed = 0;
    bool ok() const noexcept { return failed == 0; }
};
inline Context& ctx() {
    static Context c;
    return c;
}
inline void record(bool cond, std::string_view expr, const char* file, int line,
                   std::string_view msg) {
    if (cond) {
        ctx().passed++;
    } else {
        ctx().failed++;
        std::fprintf(stderr, "FAIL %.*s [%s:%d]%s%.*s\n",
                     static_cast<int>(expr.size()), expr.data(), file, line,
                     msg.empty() ? "" : " - ", static_cast<int>(msg.size()), msg.data());
    }
}
}  // namespace rmt::test

#define RMT_CHECK(cond) ::rmt::test::record((cond), #cond, __FILE__, __LINE__, "")
#define RMT_CHECK_MSG(cond, msg) ::rmt::test::record((cond), #cond, __FILE__, __LINE__, (msg))
