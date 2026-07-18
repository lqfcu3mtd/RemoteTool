// Atomic write tests (CONFIG_SPEC.md section 1, TEST_PLAN.md section 3.6).
// Pure C++17, no sockets, no Windows APIs -> compiles under MinGW or MSVC.
//
// Covers the acceptance criteria from the task spec:
//   1. Write new file (target does not exist) succeeds with correct content.
//   2. Overwrite existing file succeeds with new content.
//   3. No temp file remains after a successful write.
//   4. Write to a missing directory returns an error; no file is created.
//   5. Rename failure (target is a non-empty directory) returns an error, the
//      original target is untouched and the temp file is cleaned up.
// Plus edge cases: empty content, binary content with NUL bytes, consecutive
// writes to the same path.
//
// All tests run under std::filesystem::temp_directory_path() and clean up
// their own scratch directory; the project tree is never touched.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "rmt/config/atomic_write.h"
#include "rmt/test.h"

using namespace rmt::config;
namespace fs = std::filesystem;

namespace {

// Create a fresh scratch directory under the system temp dir. Removes any
// previous contents so the test starts from a clean state.
fs::path make_test_dir(const std::string& name) {
    fs::path base = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    return base;
}

// Read a file's full contents as a binary string. Returns an empty string if
// the file cannot be opened.
std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::string();
    std::string out;
    char buf[4096];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
        out.append(buf, static_cast<std::size_t>(in.gcount()));
    }
    return out;
}

// Count entries in a directory (non-recursive) matching a predicate.
template <typename Pred>
int count_entries(const fs::path& dir, Pred pred) {
    int count = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (pred(entry)) ++count;
    }
    return count;
}

// True if the entry's filename contains the ".tmp." marker used by the
// implementation for its temp files.
bool is_tmp_marker(const fs::directory_entry& e) {
    return e.path().filename().string().find(".tmp.") != std::string::npos;
}

void run_atomic_write_tests() {
    // --- Test 1: write a new file (target does not exist) ---
    {
        auto base = make_test_dir("rmt_aw_test_1");
        fs::path target = base / "config.json";
        std::string content = "{\"hello\":\"world\"}\n";

        auto res = atomic_write_file(target.string(), content);

        RMT_CHECK_MSG(res.ok, res.error.reason);
        RMT_CHECK(fs::exists(target));
        RMT_CHECK(read_file(target) == content);
        fs::remove_all(base);
    }

    // --- Test 2: overwrite an existing file ---
    {
        auto base = make_test_dir("rmt_aw_test_2");
        fs::path target = base / "config.json";
        std::string old_content = "{\"version\":1}\n";
        std::string new_content = "{\"version\":2,\"note\":\"updated\"}\n";

        {
            std::ofstream out(target, std::ios::binary);
            out << old_content;
        }
        RMT_CHECK(fs::exists(target));
        RMT_CHECK(read_file(target) == old_content);

        auto res = atomic_write_file(target.string(), new_content);

        RMT_CHECK_MSG(res.ok, res.error.reason);
        RMT_CHECK(fs::exists(target));
        RMT_CHECK(read_file(target) == new_content);
        fs::remove_all(base);
    }

    // --- Test 3: no temp file remains after a successful write ---
    {
        auto base = make_test_dir("rmt_aw_test_3");
        fs::path target = base / "config.json";
        std::string content = "test payload";

        auto res = atomic_write_file(target.string(), content);

        RMT_CHECK_MSG(res.ok, res.error.reason);
        RMT_CHECK(count_entries(base, is_tmp_marker) == 0);
        // Only the target file should be present in base.
        RMT_CHECK(count_entries(base, [](const fs::directory_entry&) { return true; }) == 1);
        fs::remove_all(base);
    }

    // --- Test 4: target directory does not exist -> error, nothing created ---
    {
        auto base = make_test_dir("rmt_aw_test_4");
        fs::path missing_dir = base / "nonexistent_dir";
        fs::path target = missing_dir / "config.json";
        std::string content = "test";

        auto res = atomic_write_file(target.string(), content);

        RMT_CHECK(!res.ok);
        RMT_CHECK(!res.error.reason.empty());
        // Reason must mention the function name and the target path.
        RMT_CHECK(res.error.reason.find("atomic_write_file") != std::string::npos);
        RMT_CHECK(res.error.reason.find(target.string()) != std::string::npos);
        // Target was never created, and no temp file leaked into base.
        RMT_CHECK(!fs::exists(target));
        RMT_CHECK(count_entries(base, is_tmp_marker) == 0);
        fs::remove_all(base);
    }

    // --- Test 5: rename fails (target is a non-empty directory) ---
    // Using a non-empty directory as the target makes std::filesystem::rename
    // fail reliably and portably: rename cannot replace a directory with a
    // file, and remove() cannot delete a non-empty directory. The "original
    // file" here is represented by the directory and its contents.
    {
        auto base = make_test_dir("rmt_aw_test_5");
        fs::path target = base / "target";
        fs::create_directories(target);
        {
            std::ofstream out(target / "blocker.txt", std::ios::binary);
            out << "do not remove";
        }
        std::string content = "new content";

        auto res = atomic_write_file(target.string(), content);

        RMT_CHECK(!res.ok);
        RMT_CHECK(!res.error.reason.empty());
        RMT_CHECK(res.error.reason.find("atomic_write_file") != std::string::npos);
        RMT_CHECK(res.error.reason.find(target.string()) != std::string::npos);

        // Original target (directory + blocker file) is untouched.
        RMT_CHECK(fs::is_directory(target));
        RMT_CHECK(fs::exists(target / "blocker.txt"));
        RMT_CHECK(read_file(target / "blocker.txt") == "do not remove");

        // No temp file leaked into the parent directory.
        RMT_CHECK(count_entries(base, is_tmp_marker) == 0);

        fs::remove_all(base);
    }

    // --- Test 6: empty content ---
    {
        auto base = make_test_dir("rmt_aw_test_6");
        fs::path target = base / "empty.json";
        std::string content;

        auto res = atomic_write_file(target.string(), content);

        RMT_CHECK_MSG(res.ok, res.error.reason);
        RMT_CHECK(fs::exists(target));
        RMT_CHECK(read_file(target).empty());
        // An empty file should have size 0.
        std::error_code ec;
        RMT_CHECK(fs::file_size(target, ec) == 0);
        fs::remove_all(base);
    }

    // --- Test 7: binary content with NUL bytes ---
    {
        auto base = make_test_dir("rmt_aw_test_7");
        fs::path target = base / "binary.dat";
        std::string content;
        content.push_back('\x00');
        content.push_back('\x01');
        content.push_back('\xFF');
        content.push_back('\x00');
        content.append("text");

        auto res = atomic_write_file(target.string(), content);

        RMT_CHECK_MSG(res.ok, res.error.reason);
        RMT_CHECK(fs::exists(target));
        std::string read_back = read_file(target);
        RMT_CHECK(read_back.size() == content.size());
        RMT_CHECK(read_back == content);
        fs::remove_all(base);
    }

    // --- Test 8: two consecutive writes to the same path ---
    {
        auto base = make_test_dir("rmt_aw_test_8");
        fs::path target = base / "config.json";

        auto r1 = atomic_write_file(target.string(), "first");
        RMT_CHECK_MSG(r1.ok, r1.error.reason);
        RMT_CHECK(read_file(target) == "first");

        auto r2 = atomic_write_file(target.string(), "second");
        RMT_CHECK_MSG(r2.ok, r2.error.reason);
        RMT_CHECK(read_file(target) == "second");

        // No leftover temp files after two writes.
        RMT_CHECK(count_entries(base, is_tmp_marker) == 0);
        RMT_CHECK(count_entries(base, [](const fs::directory_entry&) { return true; }) == 1);
        fs::remove_all(base);
    }

    // --- Test 9: empty path is rejected ---
    {
        auto res = atomic_write_file("", "anything");
        RMT_CHECK(!res.ok);
        RMT_CHECK(!res.error.reason.empty());
        RMT_CHECK(res.error.reason.find("atomic_write_file") != std::string::npos);
    }
}

}  // namespace

int main() {
    run_atomic_write_tests();
    auto& c = rmt::test::ctx();
    std::printf("atomic_write_test: %d passed, %d failed\n", c.passed, c.failed);
    return c.ok() ? 0 : 1;
}
