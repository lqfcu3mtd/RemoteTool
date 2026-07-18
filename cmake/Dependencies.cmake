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
# Phase 5: mbedTLS 3.6.1 (TLS-PSK) - pending full CMake integration
# (framework submodule issue, will resolve next session)
