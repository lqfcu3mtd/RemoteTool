#include "rmt/security/tls_context.h"

namespace rmt::security {

std::unique_ptr<TlsPskContext> create_tls_psk_context() {
    // Phase 5: mbedTLS 3.6.1 integration pending.
    // Will return a MbedTlsPskContext in the platform layer.
    return nullptr;
}

}  // namespace rmt::security
