# cmake/Dependencies.cmake - third-party dependency management
#
# IMPLEMENTATION_PLAN section 2.2:
#   - Versions must be pinned
#   - Default builds must not fetch from the network
#
# Dependencies:
#   - standalone Asio 1.30.2 (header-only, vendored in third_party/asio/) - Phase 1
#   - mbedTLS 2.28.7 (source build, vendored in third_party/mbedtls/) - Phase 5
#
# All dependencies ship with the repo - no network fetch required.

# --- mbedTLS 2.28.7 LTS ---
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)  # suppress -Werror from mbedTLS
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/mbedtls EXCLUDE_FROM_ALL)
unset(CMAKE_POLICY_VERSION_MINIMUM)
message(STATUS "mbedTLS 2.28.7 ready")
