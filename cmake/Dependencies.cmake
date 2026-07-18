# cmake/Dependencies.cmake - third-party dependency management
#
# IMPLEMENTATION_PLAN section 2.2:
#   - Versions must be pinned; main/master/latest are forbidden
#   - Default builds must not fetch floating latest versions from the network
#   - Generate THIRD_PARTY_NOTICES.md with license records
#
# Dependencies:
#   - standalone Asio 1.30.2 (header-only, vendored in third_party/asio/) - Phase 1
#   - mbedTLS (source build) - Phase 4+
#
# All dependencies ship with the repo in third_party/ - no network fetch
# required for a cmake --preset ... build.

# --- mbedTLS 2.28.7 LTS (source build, Phase 5 TLS-PSK) ---
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
# mbedTLS uses MS-specific %I64u format; suppress with GCC/Clang.
set(_prev_cflags "${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-format -Wno-error=format")
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/mbedtls EXCLUDE_FROM_ALL)
set(CMAKE_C_FLAGS "${_prev_cflags}")
unset(CMAKE_POLICY_VERSION_MINIMUM)
message(STATUS "mbedTLS 2.28.7 ready")
