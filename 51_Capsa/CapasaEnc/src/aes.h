// aes.h - AES-256 encryption + GCM (AEAD).
#pragma once

#include "common.h"

struct Aes256
{
    u32 rk[60]; // 15 round keys of 4 words
};

void aes256_init(Aes256 *ctx, const u8 key[32]);
void aes256_encrypt_block(const Aes256 *ctx, const u8 in[16], u8 out[16]);

// Encrypts in-place-safe (in may equal out). Tag is 16 bytes.
void gcm_encrypt(const u8 key[32], const u8 nonce[12], const u8 *in, size_t len, u8 *out, u8 tag[16]);

// Returns false when the tag does not match (out is still written, caller must discard).
bool gcm_decrypt(const u8 key[32], const u8 nonce[12], const u8 *in, size_t len, u8 *out, const u8 tag[16]);
