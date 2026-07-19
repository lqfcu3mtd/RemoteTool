/*
 *  Slim mbedTLS configuration for RemoteTool / Agent.
 *
 *  Per DEVELOPMENT_SPEC.md §8, the only required cipher suite is
 *  TLS 1.2 with PSK key exchange and AES-128-GCM. We disable:
 *
 *    - All public-key / certificate / PEM machinery (RSA, X.509, ASN.1, PEM)
 *    - All hash functions other than SHA-256 (no SHA-1, MD5, SHA-512 etc.)
 *    - All block ciphers and stream ciphers other than AES-GCM
 *      (no DES, 3DES, Blowfish, ARIA, Camellia, XTEA, ARC4)
 *    - All cipher modes other than GCM (no CBC, CTR, CFB, OFB, XTS, ECB)
 *    - All PKCS#1 / PKCS#5 / PKCS#12 padding and key derivation
 *    - All ECP curves other than secp256r1 (no secp384/521, no Koblitz, no BP)
 *    - Diffie-Hellman and ECDH (PSK-only; ECDHE-PSK disabled to keep
 *      everything deterministic for the MVP)
 *    - ECDSA, J-PAKE, HKDF, self-test, version-info features
 *
 *  Total feature set kept:
 *    - AES-128 (with AES-NI assembly acceleration on x86_64)
 *    - AES-256-GCM (for handshake AEAD)
 *    - SHA-256 (TLS 1.2 PRF, handshake MAC, HMAC)
 *    - HMAC, CTR_DRBG (for deterministic randomness)
 *    - Entropy (platform sources for seeding)
 *    - SSL/TLS 1.2 with PSK key exchange
 *    - Net (BSD sockets wrapper), Platform, Base64 (PSK identity encoding)
 *    - Bignum (used internally by ECP and GCM)
 */

#ifndef MBEDTLS_CONFIG_RMT_H
#define MBEDTLS_CONFIG_RMT_H

/* System support. */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_NO_UDBL_DIVISION
#define MBEDTLS_NO_64BIT_MULTIPLICATION
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
/* Skip FILESYSTEM / FS_IO — TLS doesn't need file I/O in this build. */

/* Network layer. */
#define MBEDTLS_NET_C

/* mbed TLS feature support. */
#define MBEDTLS_MD_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_BASE64_C

/* Hashes — only SHA-256 is required by TLS 1.2 PRF and AES-GCM. */
#define MBEDTLS_SHA256_C
/* MD5/SHA-1/SHA-512/RIPEMD160 are disabled. */

/* Ciphers — only AES (block) + GCM (mode). */
#define MBEDTLS_AES_C
#define MBEDTLS_AESNI_C
/* No DES / 3DES / Blowfish / ARIA / Camellia / XTEA / ARC4. */

/* Modes — only GCM (AEAD). */
#define MBEDTLS_GCM_C
/* No CBC, CTR, CFB, OFB, XTS. */

/* Random and entropy. */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_HMAC_DRBG_C

/* TLS. */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C   /* Agent = client */
#define MBEDTLS_SSL_SRV_C   /* RemoteTool = server */

/* Key exchange: only PSK. No DHM, no ECDHE, no RSA, no ECDSA. */
#define MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
/* Explicitly disabled:
 *   MBEDTLS_KEY_EXCHANGE_DHE_PSK_ENABLED
 *   MBEDTLS_KEY_EXCHANGE_ECDHE_PSK_ENABLED
 *   MBEDTLS_KEY_EXCHANGE_RSA_PSK_ENABLED
 *   MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
 *   MBEDTLS_KEY_EXCHANGE_DHE_RSA_ENABLED
 *   MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
 *   MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
 *   MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA_ENABLED
 *   MBEDTLS_KEY_EXCHANGE_ECDH_RSA_ENABLED
 */

/* Protocol versions. */
#define MBEDTLS_SSL_PROTO_TLS1_2

/* ECP — kept available for internal GCM uses, but with only one curve. */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
/* No ECP_DP_SECP384/521, no Koblitz, no Brainpool, no Curve25519/448. */

/* Disabled features (no X.509, no PEM, no RSA, no DHM, no ECDSA, no PKCS). */
/* The default config.h enables many of these; we just don't define them. */

/* Error reporting — keep strerror for nicer logs. */
#define MBEDTLS_ERROR_C

/* Excluded deliberately for size:
 *   - MBEDTLS_HKDF_C             (TLS 1.3 only; we use 1.2)
 *   - MBEDTLS_PKCS5_C            (only needed for PKCS#5 v1/v2 key derivation)
 *   - MBEDTLS_PKCS12_C           (X.509 PKCS#12 bundle; not used)
 *   - MBEDTLS_GENPRIME           (no RSA/DH keygen)
 *   - MBEDTLS_RSA_C              (no RSA)
 *   - MBEDTLS_X509_C             (no cert chain)
 *   - MBEDTLS_PEM_C              (no PEM files)
 *   - MBEDTLS_DHM_C              (no DHM)
 *   - MBEDTLS_ECDH_C             (no ECDHE)
 *   - MBEDTLS_ECDSA_C            (no cert auth)
 *   - MBEDTLS_ECJPAKE_C          (not used)
 *   - MBEDTLS_HAVEGE_C           (legacy entropy)
 *   - MBEDTLS_TIMING_C           (no performance test)
 *   - MBEDTLS_SELF_TEST          (no in-process self-test)
 *   - MBEDTLS_VERSION_C          (feature not used in production)
 *   - MBEDTLS_VERSION_FEATURES_C
 *   - MBEDTLS_DEBUG_C            (no runtime debug log)
 *   - MBEDTLS_PADLOCK_C          (legacy VIA padlock)
 *   - MBEDTLS_AES_ROM_TABLES     (smaller code, ~5% slower; trade-off for size)
 *   - MBEDTLS_CAMELLIA_SMALL_MEMORY
 *   - MBEDTLS_CIPHER_PADDING_PKCS7 (GCM needs no padding)
 *   - MBEDTLS_CIPHER_PADDING_ONE_AND_ZEROS
 *   - MBEDTLS_CIPHER_PADDING_ZEROS_AND_LEN
 *   - MBEDTLS_CIPHER_PADDING_ZEROS
 *   - MBEDTLS_REMOVE_3DES_CIPHERSUITES
 *   - MBEDTLS_REMOVE_ARC4_CIPHERSUITES
 *   - MBEDTLS_SSL_CBC_RECORD_SPLITTING
 *   - MBEDTLS_SSL_RENEGOTIATION
 *   - MBEDTLS_SSL_SESSION_TICKETS
 *   - MBEDTLS_SSL_SERVER_NAME_INDICATION
 *   - MBEDTLS_SSL_TRUNCATED_HMAC
 *   - MBEDTLS_SSL_SET_CURVES
 *   - MBEDTLS_SSL_SET_CURVES_PASSWORD
 *   - MBEDTLS_SSL_DTLS_SRTP
 *   - MBEDTLS_SSL_EXPORT_KEYS
 *   - MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH
 *   - MBEDTLS_TEST_NULL_ENTROPY
 *   - MBEDTLS_NO_PLATFORM_ENTROPY
 *   - MBEDTLS_PSA_CRYPTO_C       (PSA not used in this build)
 *   - MBEDTLS_LMS_C              (stateful hash-based sigs; not used)
 *   - MBEDTLS_SSL_KEEP_PEER_CERTIFICATE
 *   - MBEDTLS_SSL_CONTEXT_SERIALIZATION
 */

/* Include the consistency checks last. */
#include "mbedtls/check_config.h"

#endif /* MBEDTLS_CONFIG_RMT_H */
