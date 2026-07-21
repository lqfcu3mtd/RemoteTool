cmake_minimum_required(VERSION 3.24)

project(RemoteTool
    LANGUAGES CXX
    VERSION 0.1.0
    DESCRIPTION "Remote Maintenance Tunnel Tool"
)

# --- C++ 标准 ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- 编译选项 ---
if(MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        # /Os   - size optimization
        # /Gy   - separate functions for linker COMDAT folding
        # /GL   - whole-program optimization (LTCG)
        add_compile_options(/Os /Gy /GL)
        add_compile_definitions(NDEBUG)
        add_link_options(/LTCG /OPT:REF /OPT:ICF /DEBUG:NONE)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        # -Os                 - size optimization
        # -ffunction-sections - one section per function (for --gc-sections)
        # -fdata-sections     - one section per data item
        add_compile_options(-Os -ffunction-sections -fdata-sections)
        add_compile_definitions(NDEBUG)
        # --gc-sections drops unreferenced sections (mbedTLS leftovers)
        # -s strips symbols at link time
        add_link_options(-Wl,--gc-sections -Wl,-s)
    endif()
endif()

add_compile_definitions(ASIO_STANDALONE)
# Target Windows 7+ for the Win32/Asio API surface (silences the standalone
# Asio fallback #warning about _WIN32_WINNT being undefined).
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0601 WINVER=0x0601)
endif()

# --- 输出目录 ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- 第三方依赖（Phase 0 占位，后续固定版本） ---
include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)

# --- rmt_core 静态库 ---
add_library(rmt_core STATIC
    src/protocol/frame.cpp
    src/common/log.cpp
    src/config/strict_json.cpp
    src/config/atomic_write.cpp
    src/config/config_schema.cpp
    src/config/config_loader.cpp
    src/security/target_whitelist.cpp
    src/security/dev_file_secret_store.cpp
    src/protocol/messages.cpp
    src/tunnel/connection.cpp
    src/tunnel/agent_connection.cpp
    src/tunnel/device_manager.cpp
    src/tunnel/acceptor.cpp
    src/tunnel/agent_session.cpp
    src/tunnel/agent_session_manager.cpp
    src/tunnel/session_manager.cpp
    src/tunnel/mapping_listener.cpp
    src/platform/dpapi_secret_store.cpp
    src/security/tls_mbedtls.cpp
)

target_include_directories(rmt_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party/asio/asio/include
)

target_link_libraries(rmt_core PUBLIC ws2_32 mswsock crypt32 mbedtls)

# --- 测试 ---
enable_testing()

# frame_test
add_executable(frame_test tests/unit/frame_test.cpp)
target_link_libraries(frame_test PRIVATE rmt_core)
add_test(NAME frame_test COMMAND frame_test)

# scope_guard_test (header-only module, links rmt_core for include dir)
add_executable(scope_guard_test tests/unit/scope_guard_test.cpp)
target_link_libraries(scope_guard_test PRIVATE rmt_core)
add_test(NAME scope_guard_test COMMAND scope_guard_test)

# log_test
add_executable(log_test tests/unit/log_test.cpp)
target_link_libraries(log_test PRIVATE rmt_core)
add_test(NAME log_test COMMAND log_test)

# strict_json_test
add_executable(strict_json_test tests/unit/strict_json_test.cpp)
target_link_libraries(strict_json_test PRIVATE rmt_core)
add_test(NAME strict_json_test COMMAND strict_json_test)

# atomic_write_test
add_executable(atomic_write_test tests/unit/atomic_write_test.cpp)
target_link_libraries(atomic_write_test PRIVATE rmt_core)
add_test(NAME atomic_write_test COMMAND atomic_write_test)

# target_whitelist_test
add_executable(target_whitelist_test tests/unit/target_whitelist_test.cpp)
target_link_libraries(target_whitelist_test PRIVATE rmt_core)
add_test(NAME target_whitelist_test COMMAND target_whitelist_test)

# secret_store_test
add_executable(secret_store_test tests/unit/secret_store_test.cpp)
target_link_libraries(secret_store_test PRIVATE rmt_core)
add_test(NAME secret_store_test COMMAND secret_store_test)

# config_schema_test (uses strict_json + config_schema via rmt_core)
add_executable(config_schema_test tests/unit/config_schema_test.cpp)
target_link_libraries(config_schema_test PRIVATE rmt_core)
add_test(NAME config_schema_test COMMAND config_schema_test)

# connection_test (uses Asio TCP echo server in test)
add_executable(connection_test tests/unit/connection_test.cpp)
target_link_libraries(connection_test PRIVATE rmt_core)
add_test(NAME connection_test COMMAND connection_test)

# messages_test
add_executable(messages_test tests/unit/messages_test.cpp)
target_link_libraries(messages_test PRIVATE rmt_core)
add_test(NAME messages_test COMMAND messages_test)

# agent_connection_test (uses Asio TCP + timers, tests Agent state machine)
add_executable(agent_connection_test tests/unit/agent_connection_test.cpp)
target_link_libraries(agent_connection_test PRIVATE rmt_core)
add_test(NAME agent_connection_test COMMAND agent_connection_test)

# device_manager_test (tests RemoteTool device management, no real TCP needed)
add_executable(device_manager_test tests/unit/device_manager_test.cpp)
target_link_libraries(device_manager_test PRIVATE rmt_core)
add_test(NAME device_manager_test COMMAND device_manager_test)

# hello_heartbeat_test (integration: Acceptor + Agent end-to-end via loopback)
add_executable(hello_heartbeat_test tests/integration/hello_heartbeat_test.cpp)
target_link_libraries(hello_heartbeat_test PRIVATE rmt_core)
add_test(NAME hello_heartbeat_test COMMAND hello_heartbeat_test)

# multi_session_test (Phase 3: 3 concurrent echo, WIP - async chain hang)
add_executable(multi_session_test tests/integration/multi_session_test.cpp)
target_link_libraries(multi_session_test PRIVATE rmt_core)
add_test(NAME multi_session_test COMMAND multi_session_test)
set_tests_properties(multi_session_test PROPERTIES TIMEOUT 30 DISABLED TRUE)

# agent_session_test (Phase 2: Agent-side session handler, bidirectional test skipped — revisit Phase 3)
add_executable(agent_session_test tests/unit/agent_session_test.cpp)
target_link_libraries(agent_session_test PRIVATE rmt_core)
add_test(NAME agent_session_test COMMAND agent_session_test)
set_tests_properties(agent_session_test PROPERTIES TIMEOUT 15)

# session_manager_test (Phase 2: RemoteTool-side session management)
add_executable(session_manager_test tests/unit/session_manager_test.cpp)
target_link_libraries(session_manager_test PRIVATE rmt_core)
add_test(NAME session_manager_test COMMAND session_manager_test)

# config_loader_test (Phase 4: config file load/save round-trip)
add_executable(config_loader_test tests/unit/config_loader_test.cpp)
target_link_libraries(config_loader_test PRIVATE rmt_core)
add_test(NAME config_loader_test COMMAND config_loader_test)

# --- 应用程序（Phase 4 GUI）---
# When building with MinGW, the resulting .exe depends on the GCC runtime
# DLLs (libgcc_s_seh-1, libstdc++-6, libwinpthread-1). For the green
# distribution (only 2 .exe files in dist/), we statically link the runtime
# so no DLLs are needed at runtime. MSVC keeps the dynamic CRT because it
# already requires vcruntime140.dll system-wide.
function(cbmk_set_link_flags target)
    if(MINGW AND CMAKE_BUILD_TYPE STREQUAL "Release")
        target_link_options(${target} PRIVATE
            -static
            -static-libgcc
            -static-libstdc++)
    endif()
endfunction()

# For non-Release MinGW builds, the .exe still depends on the GCC runtime
# DLLs (libgcc_s_seh-1, libstdc++-6, libwinpthread-1). Windows looks for
# them in the .exe's directory first, so copy them next to the binary as a
# post-build step. Release uses static linking above, so this is a no-op then.
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(_mingw_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_mingw_runtime_dlls
        "${_mingw_bin_dir}/libgcc_s_seh-1.dll"
        "${_mingw_bin_dir}/libstdc++-6.dll"
        "${_mingw_bin_dir}/libwinpthread-1.dll")
endif()

function(cbmk_copy_mingw_runtime target)
    if(NOT MINGW OR CMAKE_BUILD_TYPE STREQUAL "Release")
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_mingw_runtime_dlls}
            $<TARGET_FILE_DIR:${target}>
        COMMENT "Copying MinGW runtime DLLs next to ${target}.exe")
endfunction()

# MinGW's endfile spec unconditionally links its default-manifest.o, which
# collides with our own RT_MANIFEST resource at link time (".rsrc merge
# failure: multiple non-default manifests"). Shadow it with an empty object
# placed earlier in the library search path.
if(MINGW)
    set(RMT_EMPTY_MANIFEST_DIR ${CMAKE_BINARY_DIR}/empty_manifest)
    file(MAKE_DIRECTORY ${RMT_EMPTY_MANIFEST_DIR})
    file(WRITE ${CMAKE_BINARY_DIR}/empty_manifest.cpp
        "// Shadow object: wins the search for default-manifest.o so MinGW's\n"
        "// built-in manifest is not linked (we embed our own via .rc).\n")
    add_custom_command(
        OUTPUT ${RMT_EMPTY_MANIFEST_DIR}/default-manifest.o
        COMMAND ${CMAKE_CXX_COMPILER} -c ${CMAKE_BINARY_DIR}/empty_manifest.cpp
                -o ${RMT_EMPTY_MANIFEST_DIR}/default-manifest.o
        DEPENDS ${CMAKE_BINARY_DIR}/empty_manifest.cpp
        VERBATIM)
    add_custom_target(rmt_empty_manifest
        DEPENDS ${RMT_EMPTY_MANIFEST_DIR}/default-manifest.o)
endif()

function(cbmk_shadow_default_manifest target)
    if(MINGW)
        add_dependencies(${target} rmt_empty_manifest)
        # The spec's "%s" suffix search uses the -B prefix path, not -L.
        target_link_options(${target} PRIVATE
            "-B${RMT_EMPTY_MANIFEST_DIR}")
    endif()
endfunction()

add_executable(remote_tool WIN32
    apps/remote_tool/main.cpp
    apps/remote_tool/gui/main_window.cpp
    apps/remote_tool/gui/dialogs.cpp
    apps/remote_tool/resources/remote_tool.rc
    apps/shared/theme.cpp
)
target_include_directories(remote_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/remote_tool
    ${CMAKE_SOURCE_DIR}/apps/shared
)
target_link_libraries(remote_tool PRIVATE rmt_core comctl32 dwmapi uxtheme)
cbmk_set_link_flags(remote_tool)
cbmk_copy_mingw_runtime(remote_tool)
cbmk_shadow_default_manifest(remote_tool)

add_executable(agent_windows WIN32
    apps/agent_windows/main.cpp
    apps/agent_windows/gui/agent_window.cpp
    apps/agent_windows/resources/agent.rc
    apps/shared/theme.cpp
)
target_include_directories(agent_windows PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/agent_windows
    ${CMAKE_SOURCE_DIR}/apps/shared
)
target_link_libraries(agent_windows PRIVATE rmt_core comctl32 dwmapi uxtheme)
cbmk_set_link_flags(agent_windows)
cbmk_copy_mingw_runtime(agent_windows)
cbmk_shadow_default_manifest(agent_windows)
