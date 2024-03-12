#ifndef STRATUM_SHARED_SHA256_H
#define STRATUM_SHARED_SHA256_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

/* Core SHA-256 API */
void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256_hash(const void *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);

/* HMAC-SHA-256 Authentication */
void hmac_sha256(const void *key, size_t key_len,
                 const void *data, size_t data_len,
                 uint8_t out_mac[SHA256_DIGEST_SIZE]);

/* Merkle Tree Root Computation for array of 32-byte hashes */
void sha256_merkle_root(const uint8_t hashes[][SHA256_DIGEST_SIZE],
                        size_t count,
                        uint8_t out_root[SHA256_DIGEST_SIZE]);

#endif /* STRATUM_SHARED_SHA256_H */
