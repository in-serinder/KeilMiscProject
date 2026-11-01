// aes.cpp - AES-256 (encrypt only) + GCM. GHASH uses a bit-serial multiply:
// the data volumes we handle are small enough and the code stays tiny.
#include "aes.h"

static const u8 SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static const u8 RCON[11] = { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };

static inline u32 rotl8(u32 w) { return (w << 8) | (w >> 24); }
static inline u32 sub_word(u32 w)
{
    return ((u32)SBOX[(w >> 24) & 0xFF] << 24) | ((u32)SBOX[(w >> 16) & 0xFF] << 16) |
           ((u32)SBOX[(w >> 8) & 0xFF] << 8) | (u32)SBOX[w & 0xFF];
}

void aes256_init(Aes256 *ctx, const u8 key[32])
{
    for (int i = 0; i < 8; ++i)
    {
        ctx->rk[i] = ((u32)key[i * 4] << 24) | ((u32)key[i * 4 + 1] << 16) |
                     ((u32)key[i * 4 + 2] << 8) | (u32)key[i * 4 + 3];
    }

    for (int i = 8; i < 60; ++i)
    {
        u32 t = ctx->rk[i - 1];
        if (i % 8 == 0)
        {
            t = sub_word(rotl8(t)) ^ ((u32)RCON[i / 8] << 24);
        }
        else if (i % 8 == 4)
        {
            t = sub_word(t);
        }
        ctx->rk[i] = ctx->rk[i - 8] ^ t;
    }
}

static inline u8 xtime(u8 x) { return (u8)((x << 1) ^ ((x & 0x80) ? 0x1B : 0x00)); }

static inline u32 mix_column(u32 c)
{
    u8 a0 = (u8)(c >> 24), a1 = (u8)(c >> 16), a2 = (u8)(c >> 8), a3 = (u8)c;
    u8 t = a0 ^ a1 ^ a2 ^ a3;
    u8 r0 = a0 ^ t ^ xtime((u8)(a0 ^ a1));
    u8 r1 = a1 ^ t ^ xtime((u8)(a1 ^ a2));
    u8 r2 = a2 ^ t ^ xtime((u8)(a2 ^ a3));
    u8 r3 = a3 ^ t ^ xtime((u8)(a3 ^ a0));
    return ((u32)r0 << 24) | ((u32)r1 << 16) | ((u32)r2 << 8) | (u32)r3;
}

void aes256_encrypt_block(const Aes256 *ctx, const u8 in[16], u8 out[16])
{
    u32 s[4];
    for (int i = 0; i < 4; ++i)
    {
        s[i] = ((u32)in[i * 4] << 24) | ((u32)in[i * 4 + 1] << 16) |
               ((u32)in[i * 4 + 2] << 8) | (u32)in[i * 4 + 3];
        s[i] ^= ctx->rk[i];
    }

    for (int round = 1; round <= 14; ++round)
    {
        // SubBytes + ShiftRows combined per column layout (state is column-major):
        //   s0 = [0][0] [1][1] [2][2] [3][3]  -> rows 0..3 of column 0
        u32 t0 = sub_word(s[0]);
        u32 t1 = sub_word(s[1]);
        u32 t2 = sub_word(s[2]);
        u32 t3 = sub_word(s[3]);

        u32 a0 = (t0 & 0xFF000000u) | (t1 & 0x00FF0000u) | (t2 & 0x0000FF00u) | (t3 & 0x000000FFu);
        u32 a1 = (t1 & 0xFF000000u) | (t2 & 0x00FF0000u) | (t3 & 0x0000FF00u) | (t0 & 0x000000FFu);
        u32 a2 = (t2 & 0xFF000000u) | (t3 & 0x00FF0000u) | (t0 & 0x0000FF00u) | (t1 & 0x000000FFu);
        u32 a3 = (t3 & 0xFF000000u) | (t0 & 0x00FF0000u) | (t1 & 0x0000FF00u) | (t2 & 0x000000FFu);

        if (round != 14)
        {
            a0 = mix_column(a0);
            a1 = mix_column(a1);
            a2 = mix_column(a2);
            a3 = mix_column(a3);
        }

        s[0] = a0 ^ ctx->rk[round * 4 + 0];
        s[1] = a1 ^ ctx->rk[round * 4 + 1];
        s[2] = a2 ^ ctx->rk[round * 4 + 2];
        s[3] = a3 ^ ctx->rk[round * 4 + 3];
    }

    for (int i = 0; i < 4; ++i)
    {
        out[i * 4] = (u8)(s[i] >> 24);
        out[i * 4 + 1] = (u8)(s[i] >> 16);
        out[i * 4 + 2] = (u8)(s[i] >> 8);
        out[i * 4 + 3] = (u8)s[i];
    }
}

// ---- GCM ---------------------------------------------------------------- //

// y = y * h in GF(2^128) with the GCM bit order (bit 0 = MSB).
static void gf_mul(u8 *y, const u8 *h)
{
    u8 z[16];
    u8 v[16];
    xmemset(z, 0, 16);
    xmemcpy(v, h, 16);

    for (int i = 0; i < 128; ++i)
    {
        if ((y[i >> 3] >> (7 - (i & 7))) & 1)
        {
            for (int k = 0; k < 16; ++k) z[k] ^= v[k];
        }

        const u8 lsb = (u8)(v[15] & 1);
        for (int k = 15; k > 0; --k) v[k] = (u8)((v[k] >> 1) | ((v[k - 1] & 1) << 7));
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xE1;
    }

    xmemcpy(y, z, 16);
}

static void ghash_block(u8 *y, const u8 *h, const u8 *block)
{
    for (int i = 0; i < 16; ++i) y[i] ^= block[i];
    gf_mul(y, h);
}

static void gcm_ctr(const u8 key[32], const u8 nonce[12], const u8 *in, size_t len, u8 *out, u8 *tag_out)
{
    Aes256 aes;
    aes256_init(&aes, key);

    u8 h[16];
    u8 zero[16];
    xmemset(zero, 0, 16);
    aes256_encrypt_block(&aes, zero, h);

    // J0 = nonce || 0x00000001
    u8 j0[16];
    xmemcpy(j0, nonce, 12);
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;

    u8 counter[16];
    xmemcpy(counter, j0, 16);
    u8 stream[16];

    size_t offset = 0;
    while (offset < len)
    {
        // inc32
        for (int k = 15; k >= 12; --k)
        {
            if (++counter[k] != 0) break;
        }
        aes256_encrypt_block(&aes, counter, stream);

        const size_t n = (len - offset) < 16 ? (len - offset) : 16;
        for (size_t i = 0; i < n; ++i) out[offset + i] = (u8)(in[offset + i] ^ stream[i]);
        offset += n;
    }

    // GHASH over ciphertext, then lengths (AAD length = 0).
    u8 y[16];
    xmemset(y, 0, 16);

    offset = 0;
    while (offset + 16 <= len)
    {
        ghash_block(y, h, out + offset);
        offset += 16;
    }

    if (offset < len)
    {
        u8 tail[16];
        xmemset(tail, 0, 16);
        xmemcpy(tail, out + offset, len - offset);
        ghash_block(y, h, tail);
    }

    if (tag_out)
    {
        u8 lens[16];
        // Length block is big-endian in GCM (AAD bits || ciphertext bits).
        for (int i = 0; i < 8; ++i) lens[i] = 0;   // AAD bits = 0
        u64 bits = (u64)len * 8;
        for (int i = 0; i < 8; ++i) lens[8 + i] = (u8)(bits >> (56 - i * 8));
        ghash_block(y, h, lens);

        u8 ek[16];
        aes256_encrypt_block(&aes, j0, ek);
        for (int i = 0; i < 16; ++i) tag_out[i] = (u8)(y[i] ^ ek[i]);
    }
}

void gcm_encrypt(const u8 key[32], const u8 nonce[12], const u8 *in, size_t len, u8 *out, u8 tag[16])
{
    if (len == 0)
    {
        // Only the tag is produced; GHASH still needs the length block.
        gcm_ctr(key, nonce, in, 0, out, tag);
        return;
    }
    gcm_ctr(key, nonce, in, len, out, tag);
}

bool gcm_decrypt(const u8 key[32], const u8 nonce[12], const u8 *in, size_t len, u8 *out, const u8 tag[16])
{
    u8 computed[16];

    // Decrypt = same CTR operation (XOR), but GHASH must run over the input.
    if (len == 0)
    {
        gcm_ctr(key, nonce, in, 0, out, computed);
    }
    else
    {
        // Compute the tag over the ciphertext first, then decrypt into place.
        Aes256 aes;
        aes256_init(&aes, key);

        u8 h[16], zero[16];
        xmemset(zero, 0, 16);
        aes256_encrypt_block(&aes, zero, h);

        u8 j0[16];
        xmemcpy(j0, nonce, 12);
        j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;

        u8 y[16];
        xmemset(y, 0, 16);

        size_t offset = 0;
        while (offset + 16 <= len)
        {
            ghash_block(y, h, in + offset);
            offset += 16;
        }
        if (offset < len)
        {
            u8 tail[16];
            xmemset(tail, 0, 16);
            xmemcpy(tail, in + offset, len - offset);
            ghash_block(y, h, tail);
        }

        u8 lens[16];
        for (int i = 0; i < 8; ++i) lens[i] = 0;   // AAD bits = 0 (big-endian)
        u64 bits = (u64)len * 8;
        for (int i = 0; i < 8; ++i) lens[8 + i] = (u8)(bits >> (56 - i * 8));
        ghash_block(y, h, lens);

        u8 ek[16];
        aes256_encrypt_block(&aes, j0, ek);
        for (int i = 0; i < 16; ++i) computed[i] = (u8)(y[i] ^ ek[i]);

        // Decrypt with the same keystream.
        u8 counter[16];
        xmemcpy(counter, j0, 16);
        u8 stream[16];
        offset = 0;
        while (offset < len)
        {
            for (int k = 15; k >= 12; --k)
            {
                if (++counter[k] != 0) break;
            }
            aes256_encrypt_block(&aes, counter, stream);
            const size_t n = (len - offset) < 16 ? (len - offset) : 16;
            for (size_t i = 0; i < n; ++i) out[offset + i] = (u8)(in[offset + i] ^ stream[i]);
            offset += n;
        }
    }

    u8 diff = 0;
    for (int i = 0; i < 16; ++i) diff |= (u8)(computed[i] ^ tag[i]);
    return diff == 0;
}
