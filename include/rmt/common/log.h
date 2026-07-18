#pragma once
// Logger interface for RemoteTool (CODING_STANDARDS.md section 8).
//
// SECURITY (CODING_STANDARDS.md section 8, CONFIG_SPEC.md section 9):
//   Callers MUST NOT pass PSK, pairing keys, or SESSION_DATA payload content
//   into any log method. The Logger does not inspect or filter content; the
//   no-secret-leak rule is enforced entirely by the caller. Only metadata
//   (session id, byte counts, timestamps, error codes) should appear in log
//   messages.
//
// Scope (Phase 0):
//   This module provides the Logger API plus a default stderr sink. File-based
//   rotation (CONFIG_SPEC.md section 9: max_file_bytes / remote_tool.log.1 /
//   retained_files) is implemented by an upper-layer FileSink in a later phase.
#include <functional>
#include <mutex>
#include <string_view>

namespace rmt::common {

// Log severity. Ordering matters: Debug < Info < Warn < Error. Values align
// with CONFIG_SPEC.md section 3 logging.level ("debug","info","warn","error").
enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

// Default sink: writes "[LEVEL] message\n" to stderr (no timestamp).
void stderr_sink(LogLevel level, std::string_view message);

// Thread-safe logger with a pluggable sink. The default sink is stderr_sink;
// replace it via set_sink(). A sink that throws or fails must not crash the
// network process: log() swallows any exception emitted by the sink.
class Logger {
public:
    Logger() = default;

    // Set minimum output threshold. Messages below this level are dropped.
    void set_level(LogLevel level) noexcept;

    // Register a custom sink. Replaces the previous sink. Passing nullptr
    // restores the default stderr_sink.
    void set_sink(std::function<void(LogLevel, std::string_view)> sink);

    // Emit one log entry. Exceptions thrown by the sink are caught and
    // discarded; a logging failure must never propagate to the caller.
    void log(LogLevel level, std::string_view message);

    // Convenience methods.
    void debug(std::string_view message);
    void info(std::string_view message);
    void warn(std::string_view message);
    void error(std::string_view message);

private:
    std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
    std::function<void(LogLevel, std::string_view)> sink_ = stderr_sink;
};

}  // namespace rmt::common
