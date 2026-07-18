#include "rmt/config/atomic_write.h"

#include <atomic>
#include <fstream>
#include <random>
#include <sstream>

namespace rmt::config {
namespace {

// Build a process-unique suffix so concurrent writes (within or across
// processes) do not collide on the same temp file name. Combines a
// monotonic in-process counter with non-deterministic random bits; both are
// needed because std::random_device alone is not guaranteed unique across
// calls and the counter alone is not unique across processes.
std::string make_tmp_suffix() {
    static std::atomic<std::uint64_t> counter{0};
    const std::uint64_t c = counter.fetch_add(1, std::memory_order_relaxed);

    std::random_device rd;
    const std::uint64_t hi = static_cast<std::uint64_t>(rd());
    const std::uint64_t lo = static_cast<std::uint64_t>(rd());
    const std::uint64_t r = (hi << 32) | lo;

    std::ostringstream ss;
    ss << std::hex << c << '-' << r;
    return ss.str();
}

// Best-effort removal of a temp file. Ignores all errors; used during cleanup
// paths where reporting the cleanup failure would mask the original error.
void remove_quiet(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::remove(p, ec);
}

}  // namespace

AtomicWriteResult atomic_write_file(const std::string& path, std::string_view content) {
    AtomicWriteResult result;  // ok == true by default

    // Step 1a: reject empty path (CODING_STANDARDS.md section 9 input validation).
    if (path.empty()) {
        result.ok = false;
        result.error.reason = "atomic_write_file: path is empty";
        result.error.sys_code = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    const std::filesystem::path target(path);
    const std::filesystem::path parent = target.parent_path();
    const std::string filename = target.filename().string();

    // Step 1b: verify parent directory exists. We do not auto-create it;
    // directory management is the caller's responsibility (CONFIG_SPEC.md
    // section 2 first-launch creates config/ at a higher layer).
    if (!parent.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(parent, ec)) {
            result.ok = false;
            result.error.reason =
                "atomic_write_file: target directory does not exist: " + parent.string() +
                " (target=" + target.string() + ")";
            result.error.sys_code =
                std::make_error_code(std::errc::no_such_file_or_directory);
            return result;
        }
    }

    // Step 2: compose the temp file path in the same directory (same filesystem
    // is required for std::filesystem::rename to be atomic).
    const std::string tmp_name = filename + ".tmp." + make_tmp_suffix();
    const std::filesystem::path tmp =
        parent.empty() ? std::filesystem::path(tmp_name) : (parent / tmp_name);

    // Step 3: open (binary), write, flush, close. Each failure path removes the
    // temp file so no partial temp file is left behind.
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.ok = false;
            result.error.reason =
                "atomic_write_file: failed to open temp file for writing: " + tmp.string();
            result.error.sys_code = std::make_error_code(std::errc::io_error);
            return result;
        }
        if (!content.empty()) {
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
        if (!out) {
            result.ok = false;
            result.error.reason =
                "atomic_write_file: failed to write content to temp file: " + tmp.string();
            result.error.sys_code = std::make_error_code(std::errc::io_error);
            remove_quiet(tmp);
            return result;
        }
        out.flush();
        if (!out) {
            result.ok = false;
            result.error.reason =
                "atomic_write_file: failed to flush temp file: " + tmp.string();
            result.error.sys_code = std::make_error_code(std::errc::io_error);
            remove_quiet(tmp);
            return result;
        }
        // Note: fsync is intentionally not called here. std::ofstream::flush
        // only pushes the user-space buffer to the OS; a power loss after flush
        // may still lose data. True durability requires fsync on the underlying
        // file descriptor, which needs platform-specific access (fileno/_fileno
        // + fsync/FlushFileBuffers). That is deferred to the platform layer
        // (Phase 4+) to keep this core layer free of #ifdef _WIN32.
        out.close();
        if (!out) {
            result.ok = false;
            result.error.reason =
                "atomic_write_file: failed to close temp file: " + tmp.string();
            result.error.sys_code = std::make_error_code(std::errc::io_error);
            remove_quiet(tmp);
            return result;
        }
    }

    // Step 4: atomic replace via std::filesystem::rename.
    //
    // C++17 [fs.op.rename]/3: if new_p resolves to an existing non-directory
    // file, new_p is removed first. In practice, Windows implementations need
    // MoveFileEx with MOVEFILE_REPLACE_EXISTING to overwrite; modern MinGW and
    // MSVC std::filesystem::rename implementations set this flag. The fallback
    // below covers older toolchains where rename cannot overwrite.
    std::error_code rename_ec;
    std::filesystem::rename(tmp, target, rename_ec);
    if (rename_ec) {
        // Only attempt the remove+rename fallback when the target actually
        // exists; that is the documented case where rename may fail due to the
        // target being present. If the target does not exist, rename failed for
        // another reason and removing the target would be wrong.
        std::error_code exists_ec;
        const bool target_exists = std::filesystem::exists(target, exists_ec);

        if (!target_exists) {
            result.ok = false;
            result.error.reason =
                "atomic_write_file: rename failed (" + rename_ec.message() +
                "); target=" + target.string() + " tmp=" + tmp.string();
            result.error.sys_code = rename_ec;
            remove_quiet(tmp);
            return result;
        }

        // Fallback: remove target then rename. Not strictly atomic on Windows
        // (brief window where target is missing). If the second rename fails
        // after remove succeeded, the original target is gone; this is the
        // documented non-atomic worst case and is reported in the reason.
        std::error_code remove_ec;
        const bool removed = std::filesystem::remove(target, remove_ec);
        if (!removed && remove_ec) {
            result.ok = false;
            result.error.reason =
                "atomic_write_file: rename failed (" + rename_ec.message() +
                ") and fallback remove also failed (" + remove_ec.message() +
                "); target=" + target.string() + " tmp=" + tmp.string();
            result.error.sys_code = remove_ec;
            remove_quiet(tmp);
            return result;
        }

        std::error_code rename2_ec;
        std::filesystem::rename(tmp, target, rename2_ec);
        if (rename2_ec) {
            result.ok = false;
            result.error.reason =
                "atomic_write_file: rename failed (" + rename_ec.message() +
                ") and fallback rename also failed (" + rename2_ec.message() +
                "); target=" + target.string() + " tmp=" + tmp.string() +
                " (note: fallback may have removed the original target; this "
                "is the documented non-atomic worst case)";
            result.error.sys_code = rename2_ec;
            remove_quiet(tmp);
            return result;
        }
    }

    return result;  // ok == true
}

}  // namespace rmt::config
