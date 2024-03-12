#include <shared/sha256.h>
#include <kernel/string.h>

/* SHA-256 Round Constants */
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline uint32_t ror(uint32_t val, uint32_t bits) {
    return (val >> bits) | (val << (32 - bits));
}

static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static inline uint32_t ep0(uint32_t x) {
    return ror(x, 2) ^ ror(x, 13) ^ ror(x, 22);
}

static inline uint32_t ep1(uint32_t x) {
    return ror(x, 6) ^ ror(x, 11) ^ ror(x, 25);
}

static inline uint32_t sig0(uint32_t x) {
    return ror(x, 7) ^ ror(x, 18) ^ (x >> 3);
}

static inline uint32_t sig1(uint32_t x) {
    return ror(x, 17) ^ ror(x, 19) ^ (x >> 10);
}

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t data[64]) {
    uint32_t m[64];
    for (int i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        m[i] = sig1(m[i - 2]) + m[i - 7] + sig0(m[i - 15]) + m[i - 16];
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + ep1(e) + ch(e, f, g) + K[i] + m[i];
        uint32_t t2 = ep0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx) {
    if (!ctx) return;
    ctx->count = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len) {
    if (!ctx || !data || len == 0) return;
    const uint8_t *p = (const uint8_t *)data;
    size_t buffer_idx = (size_t)(ctx->count % SHA256_BLOCK_SIZE);
    ctx->count += len;

    while (len > 0) {
        size_t to_copy = SHA256_BLOCK_SIZE - buffer_idx;
        if (to_copy > len) to_copy = len;

        memcpy(&ctx->buffer[buffer_idx], p, to_copy);
        buffer_idx += to_copy;
        p += to_copy;
        len -= to_copy;

        if (buffer_idx == SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, ctx->buffer);
            buffer_idx = 0;
        }
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]) {
    if (!ctx || !digest) return;
    size_t buffer_idx = (size_t)(ctx->count % SHA256_BLOCK_SIZE);
    ctx->buffer[buffer_idx++] = 0x80;

    if (buffer_idx > 56) {
        memset(&ctx->buffer[buffer_idx], 0, SHA256_BLOCK_SIZE - buffer_idx);
        sha256_transform(ctx, ctx->buffer);
        buffer_idx = 0;
    }

    memset(&ctx->buffer[buffer_idx], 0, 56 - buffer_idx);
    uint64_t bits = ctx->count * 8;
    for (int i = 7; i >= 0; i--) {
        ctx->buffer[56 + i] = (uint8_t)(bits & 0xFF);
        bits >>= 8;
    }

    sha256_transform(ctx, ctx->buffer);

    for (int i = 0; i < 8; i++) {
        digest[i * 4]     = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i] & 0xFF);
    }
}

void sha256_hash(const void *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

void hmac_sha256(const void *key, size_t key_len,
                 const void *data, size_t data_len,
                 uint8_t out_mac[SHA256_DIGEST_SIZE]) {
    uint8_t k[SHA256_BLOCK_SIZE];
    memset(k, 0, sizeof(k));

    if (key_len > SHA256_BLOCK_SIZE) {
        sha256_hash(key, key_len, k);
    } else {
        memcpy(k, key, key_len);
    }

    uint8_t ipad[SHA256_BLOCK_SIZE];
    uint8_t opad[SHA256_BLOCK_SIZE];
    for (size_t i = 0; i < SHA256_BLOCK_SIZE; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    sha256_ctx_t ctx;
    uint8_t inner_hash[SHA256_DIGEST_SIZE];

    sha256_init(&ctx);
    sha256_update(&ctx, ipad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner_hash, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, out_mac);
}

void sha256_merkle_root(const uint8_t hashes[][SHA256_DIGEST_SIZE],
                        size_t count,
                        uint8_t out_root[SHA256_DIGEST_SIZE]) {
    if (!out_root) return;
    if (count == 0) {
        memset(out_root, 0, SHA256_DIGEST_SIZE);
        return;
    }
    if (count == 1) {
        memcpy(out_root, hashes[0], SHA256_DIGEST_SIZE);
        return;
    }

    uint8_t current_level[128][SHA256_DIGEST_SIZE];
    size_t level_count = count > 128 ? 128 : count;
    for (size_t i = 0; i < level_count; i++) {
        memcpy(current_level[i], hashes[i], SHA256_DIGEST_SIZE);
    }

    while (level_count > 1) {
        size_t next_count = (level_count + 1) / 2;
        for (size_t i = 0; i < next_count; i++) {
            size_t left = i * 2;
            size_t right = (left + 1 < level_count) ? left + 1 : left;

            uint8_t concat[SHA256_DIGEST_SIZE * 2];
            memcpy(concat, current_level[left], SHA256_DIGEST_SIZE);
            memcpy(concat + SHA256_DIGEST_SIZE, current_level[right], SHA256_DIGEST_SIZE);

            sha256_hash(concat, sizeof(concat), current_level[i]);
        }
        level_count = next_count;
    }

    memcpy(out_root, current_level[0], SHA256_DIGEST_SIZE);
}
