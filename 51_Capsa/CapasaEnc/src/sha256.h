// sha256.h - SHA-256, HMAC-SHA256 and PBKDF2-HMAC-SHA256.
#pragma once

#include "common.h"

struct Sha256
{
    u32 h[8];
    u64 total;
    u8 buf[64];
    u32 buflen;
};

void sha256_init(Sha256 *ctx);
void sha256_update(Sha256 *ctx, const void *data, size_t len);
void sha256_final(Sha256 *ctx, u8 out[32]);

void sha256(const void *data, size_t len, u8 out[32]);
void hmac_sha256(const u8 *key, size_t keyLen, const u8 *msg, size_t msgLen, u8 out[32]);

// dkLen is fixed to 32 bytes (one SHA-256 block) which is what AES-256 needs.
void pbkdf2_sha256(const char *password, size_t passLen, const u8 *salt, size_t saltLen,
                   u32 iterations, u8 out[32]);
