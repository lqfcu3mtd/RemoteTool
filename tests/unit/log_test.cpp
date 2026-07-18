// Logger tests (CODING_STANDARDS.md section 8, CONFIG_SPEC.md section 3).
// Pure C++17, no sockets, no Windows APIs -> compiles under MinGW or MSVC.
#include <atomic>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "rmt/common/log.h"
#include "rmt/test.h"

using rmt::common::Logger;
using rmt::common::LogLevel;
using rmt::common::stderr_sink;

namespace {

struct CountingSink {
    std::atomic<int> debug_count{0};
    std::atomic<int> info_count{0};
    std::atomic<int> warn_count{0};
    std::atomic<int> error_count{0};
    std::atomic<int> total{0};

    void operator()(LogLevel level, std::string_view /*msg*/) {
        switch (level) {
            case LogLevel::Debug: debug_count.fetch_add(1); break;
            case LogLevel::Info:  info_count.fetch_add(1);  break;
            case LogLevel::Warn:  warn_count.fetch_add(1);  break;
            case LogLevel::Error: error_count.fetch_add(1); break;
        }
        total.fetch_add(1);
    }
};

void run_log_tests() {
    // --- Acceptance 1: set_level(Error) drops info/warn, keeps error ---
    {
        Logger logger;
        CountingSink sink;
        logger.set_sink([&](LogLevel l, std::string_view m) { sink(l, m); });
        logger.set_level(LogLevel::Error);
        logger.info("info msg");
        logger.warn("warn msg");
        logger.error("error msg");
        RMT_CHECK(sink.info_count.load() == 0);
        RMT_CHECK(sink.warn_count.load() == 0);
        RMT_CHECK(sink.error_count.load() == 1);
        RMT_CHECK(sink.total.load() == 1);
    }

    // --- Acceptance 2: set_level(Debug) lets all levels through ---
    {
        Logger logger;
        CountingSink sink;
        logger.set_sink([&](LogLevel l, std::string_view m) { sink(l, m); });
        logger.set_level(LogLevel::Debug);
        logger.debug("d");
        logger.info("i");
        logger.warn("w");
        logger.error("e");
        RMT_CHECK(sink.debug_count.load() == 1);
        RMT_CHECK(sink.info_count.load() == 1);
        RMT_CHECK(sink.warn_count.load() == 1);
        RMT_CHECK(sink.error_count.load() == 1);
        RMT_CHECK(sink.total.load() == 4);
    }

    // --- Acceptance 3: custom sink receives (level, message) ---
    {
        Logger logger;
        LogLevel captured_level = LogLevel::Info;
        std::string captured_msg;
        logger.set_sink([&](LogLevel l, std::string_view m) {
            captured_level = l;
            captured_msg = std::string(m);
        });
        logger.warn("hello world");
        RMT_CHECK(captured_level == LogLevel::Warn);
        RMT_CHECK(captured_msg == "hello world");
    }

    // --- Acceptance 4: sink throwing must not propagate ---
    {
        Logger logger;
        logger.set_sink([](LogLevel, std::string_view) {
            throw std::runtime_error("sink failed");
        });
        bool no_exception = true;
        try {
            logger.info("safe");
            logger.error("also safe");
        } catch (...) {
            no_exception = false;
        }
        RMT_CHECK_MSG(no_exception, "log() propagated a sink exception");
    }

    // --- Acceptance 5: multithreaded concurrent log() does not crash ---
    {
        Logger logger;
        std::atomic<int> count{0};
        logger.set_sink([&](LogLevel, std::string_view) { count.fetch_add(1); });
        logger.set_level(LogLevel::Debug);
        const int threads = 8;
        const int per_thread = 500;
        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(threads));
        for (int t = 0; t < threads; ++t) {
            pool.emplace_back([&logger, t, per_thread]() {
                for (int i = 0; i < per_thread; ++i) {
                    LogLevel lvl = static_cast<LogLevel>((t + i) % 4);
                    logger.log(lvl, "concurrent msg");
                }
            });
        }
        for (auto& th : pool) th.join();
        RMT_CHECK(count.load() == threads * per_thread);
    }

    // --- Default level is Info (debug dropped, info passes) ---
    {
        Logger logger;
        CountingSink sink;
        logger.set_sink([&](LogLevel l, std::string_view m) { sink(l, m); });
        logger.debug("should drop");
        logger.info("should pass");
        RMT_CHECK(sink.debug_count.load() == 0);
        RMT_CHECK(sink.info_count.load() == 1);
    }

    // --- Default stderr_sink works without crash (no set_sink call) ---
    {
        Logger logger;
        logger.info("default sink info");
        logger.error("default sink error");
        // No assertion: we only verify stderr_sink does not crash.
    }

    // --- set_sink(nullptr) falls back to stderr_sink without crash ---
    {
        Logger logger;
        logger.set_sink(nullptr);
        logger.info("after null reset");
        RMT_CHECK(true);  // reached here without crashing
    }
}

}  // namespace

int main() {
    run_log_tests();
    auto& c = rmt::test::ctx();
    std::printf("log_test: %d passed, %d failed\n", c.passed, c.failed);
    return c.ok() ? 0 : 1;
}
