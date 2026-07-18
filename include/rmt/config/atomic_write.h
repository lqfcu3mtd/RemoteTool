#pragma once
// Atomic file write for configuration persistence (CONFIG_SPEC.md section 1,
// IMPLEMENTATION_PLAN.md section 5 Phase 0).
//
// Writes content to a temporary file in the same directory as the target, then
// renames it over the target. This gives crash-consistent updates: either the
// old content or the new content is visible, never a partial write.
//
// Core-layer constraint (CODING_STANDARDS.md section 4): this module uses only
// the C++17 standard library. No #ifdef _WIN32, no Win32 API. Platform-specific
// durability hardening (fsync, ReplaceFile) is deferred to the platform layer.
//
// Known limitations (to be strengthened by the platform layer in Phase 4+):
//   * No fsync. std::ofstream::flush only pushes user-space buffers to the OS.
//     A power loss after flush but before the OS flushes to disk may lose data.
//   * On filesystems where std::filesystem::rename cannot overwrite an existing
//     target, the implementation falls back to remove(target) + rename(tmp,
//     target). That fallback has a brief window where the target does not
//     exist and is therefore not strictly atomic. In the worst case (remove
//     succeeds, the second rename fails) the original target is gone. This is
//     reported as a known limitation; callers must not assume strict atomicity
//     on such filesystems.
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace rmt::config {

// Error context returned by atomic_write_file on failure.
struct AtomicWriteError {
    // Human-readable reason including the target path, the failed step, and
    // any underlying system error message produced during that step.
    std::string reason;

    // Optional system error code. Default-constructed (no value) when the
    // failure is not attributable to a standard system error. Callers can
    // inspect `sys_code.message()` and `sys_code.value()` for diagnostics.
    std::error_code sys_code;
};

// Result of atomic_write_file. `ok == true` (or `operator bool()` returning
// true) means the write succeeded and `error` is unused.
struct AtomicWriteResult {
    bool ok = true;
    AtomicWriteError error;

    explicit operator bool() const noexcept { return ok; }
};

// Atomically write `content` to `path`.
//
// Steps:
//   1. Reject empty path. Verify the parent directory of `path` exists;
//      directories are managed by the caller, this function does not create
//      them. (CONFIG_SPEC.md section 10: save failure preserves the original
//      file and reports an error.)
//   2. Compose a temp file path `path + ".tmp.<unique-suffix>"` in the same
//      directory so rename is on the same filesystem (atomic where supported).
//   3. Open the temp file in binary mode, write `content`, flush, close.
//   4. Atomically replace the target via std::filesystem::rename(tmp, target).
//      If rename fails (e.g. because the target exists and the toolchain's
//      std::filesystem::rename implementation does not overwrite), fall back to
//      remove(target) + rename(tmp, target). See the header-level note for why
//      the fallback is not strictly atomic.
//
// On any failure, the temp file is removed and an error is returned. The
// original target file is left untouched except in the documented non-atomic
// fallback worst case, which is reported in the error reason.
AtomicWriteResult atomic_write_file(const std::string& path, std::string_view content);

}  // namespace rmt::config
