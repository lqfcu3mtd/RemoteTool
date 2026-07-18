// MbedTlsPskContext — concrete TLS-PSK implementation using mbedTLS 2.28.7.
// CODING_STANDARDS.md section 4: platform-specific socket calls guarded
// with #ifdef _WIN32.
#include "rmt/security/tls_context.h"

#include <cstring>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rmt::security {
namespace {

// error_str helper
std::string error_str(int ret) {
    char buf[128];
    ::mbedtls_strerror(ret, buf, sizeof(buf));
    return buf;
}

// BIO callbacks
int bio_send(void* ctx, const unsigned char* buf, size_t len) {
    auto fd = static_cast<std::uintptr_t>(reinterpret_cast<std::size_t>(ctx));
#ifdef _WIN32
    int n = ::send(static_cast<SOCKET>(fd), reinterpret_cast<const char*>(buf),
                   static_cast<int>(len), 0);
#else
    int n = static_cast<int>(::send(fd, buf, len, 0));
#endif
    if (n < 0) return MBEDTLS_ERR_NET_SEND_FAILED;
    return n;
}

// BIO callback: receive data from the socket.
int bio_recv(void* ctx, unsigned char* buf, size_t len) {
    auto fd = static_cast<std::uintptr_t>(reinterpret_cast<std::size_t>(ctx));
#ifdef _WIN32
    int n = ::recv(static_cast<SOCKET>(fd), reinterpret_cast<char*>(buf),
                   static_cast<int>(len), 0);
#else
    int n = static_cast<int>(::recv(fd, buf, len, 0));
#endif
    if (n == 0) return MBEDTLS_ERR_SSL_CONN_EOF;
    if (n < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
    return n;
}

}  // namespace

class MbedTlsPskContext final : public TlsPskContext {
public:
    MbedTlsPskContext() {
        mbedtls_ssl_init(&ssl_);
        mbedtls_ssl_config_init(&conf_);
        mbedtls_ctr_drbg_init(&ctr_drbg_);
        mbedtls_entropy_init(&entropy_);
    }

    ~MbedTlsPskContext() override { close(); }

    bool set_psk(const std::string& identity, const std::uint8_t* key,
                 std::size_t key_len) override {
        int ret = mbedtls_ctr_drbg_seed(&ctr_drbg_, mbedtls_entropy_func,
                                         &entropy_, nullptr, 0);
        if (ret != 0) {
            last_err_ = "ctr_drbg_seed: " + error_str(ret);
            return false;
        }

        ret = mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret != 0) {
            last_err_ = "ssl_config_defaults: " + error_str(ret);
            return false;
        }

        mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_OPTIONAL);
        mbedtls_ssl_conf_rng(&conf_, mbedtls_ctr_drbg_random, &ctr_drbg_);

        ret = mbedtls_ssl_conf_psk(&conf_, key, key_len,
                                   reinterpret_cast<const unsigned char*>(
                                       identity.data()), identity.size());
        if (ret != 0) {
            last_err_ = "ssl_conf_psk: " + error_str(ret);
            return false;
        }

        identity_ = identity;
        return true;
    }

    bool handshake(std::uintptr_t socket_fd) override {
        int ret = mbedtls_ssl_setup(&ssl_, &conf_);
        if (ret != 0) {
            last_err_ = "ssl_setup: " + error_str(ret);
            return false;
        }

        mbedtls_ssl_set_bio(&ssl_, reinterpret_cast<void*>(socket_fd),
                            bio_send, bio_recv, nullptr);

        while ((ret = mbedtls_ssl_handshake(&ssl_)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
                ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                last_err_ = "handshake: " + error_str(ret);
                return false;
            }
        }
        return true;
    }

    std::size_t read(std::uint8_t* buf, std::size_t max_len) override {
        int ret = mbedtls_ssl_read(&ssl_, buf, max_len);
        if (ret <= 0) {
            if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                return 0;
            }
            return 0;  // EOF or error
        }
        return static_cast<std::size_t>(ret);
    }

    bool write(const std::uint8_t* data, std::size_t len) override {
        std::size_t written = 0;
        while (written < len) {
            int ret = mbedtls_ssl_write(&ssl_, data + written, len - written);
            if (ret <= 0) {
                if (ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                    return false;
                }
                continue;
            }
            written += static_cast<std::size_t>(ret);
        }
        return true;
    }

    void close() override {
        if (!closed_) {
            mbedtls_ssl_close_notify(&ssl_);
            mbedtls_ssl_free(&ssl_);
            mbedtls_ssl_config_free(&conf_);
            mbedtls_ctr_drbg_free(&ctr_drbg_);
            mbedtls_entropy_free(&entropy_);
            closed_ = true;
        }
    }

    std::string last_error() const override { return last_err_; }

private:
    mbedtls_ssl_context ssl_;
    mbedtls_ssl_config conf_;
    mbedtls_ctr_drbg_context ctr_drbg_;
    mbedtls_entropy_context entropy_;
    std::string identity_;
    std::string last_err_;
    bool closed_ = false;
};

// ---- factory ----

std::unique_ptr<TlsPskContext> create_tls_psk_context() {
    return std::make_unique<MbedTlsPskContext>();
}

}  // namespace rmt::security
