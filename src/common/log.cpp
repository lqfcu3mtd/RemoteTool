#include "rmt/common/log.h"

#include <cstdio>

namespace rmt::common {

namespace {

const char* level_tag(LogLevel l) noexcept {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

}  // namespace

void stderr_sink(LogLevel level, std::string_view message) {
    // %.*s handles non-null-terminated string_view safely.
    std::fprintf(stderr, "[%s] %.*s\n",
                 level_tag(level),
                 static_cast<int>(message.size()),
                 message.data());
}

void Logger::set_level(LogLevel level) noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    level_ = level;
}

void Logger::set_sink(std::function<void(LogLevel, std::string_view)> sink) {
    std::lock_guard<std::mutex> lk(mutex_);
    sink_ = sink ? std::move(sink) : stderr_sink;
}

void Logger::log(LogLevel level, std::string_view message) {
    // Severity ordering relies on enum values Debug<Info<Warn<Error.
    std::function<void(LogLevel, std::string_view)> sink_copy;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (static_cast<int>(level) < static_cast<int>(level_)) {
            return;
        }
        sink_copy = sink_;
    }
    // Call outside the lock so a slow sink never blocks other threads.
    if (sink_copy) {
        try {
            sink_copy(level, message);
        } catch (...) {
            // Discard: logging must not crash the network process.
        }
    }
}

void Logger::debug(std::string_view message) { log(LogLevel::Debug, message); }
void Logger::info(std::string_view message)  { log(LogLevel::Info,  message); }
void Logger::warn(std::string_view message)  { log(LogLevel::Warn,  message); }
void Logger::error(std::string_view message) { log(LogLevel::Error, message); }

}  // namespace rmt::common
