// sha256.cpp - FIPS 180-4 SHA-256 + RFC 2104 HMAC + RFC 8018 PBKDF2.
#include "sha256.h"

static const u32 K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static inline u32 rotr(u32 x, u32 n) { return (x >> n) | (x << (32 - n)); }

static void compress(Sha256 *ctx, const u8 *block)
{
    u32 w[64];
    for (int i = 0; i < 16; ++i)
    {
        w[i] = ((u32)block[i * 4] << 24) | ((u32)block[i * 4 + 1] << 16) |
               ((u32)block[i * 4 + 2] << 8) | (u32)block[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i)
    {
        const u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    u32 a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3];
    u32 e = ctx->h[4], f = ctx->h[5], g = ctx->h[6], h = ctx->h[7];

    for (int i = 0; i < 64; ++i)
    {
        const u32 S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const u32 ch = (e & f) ^ (~e & g);
        const u32 t1 = h + S1 + ch + K[i] + w[i];
        const u32 S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const u32 mj = (a & b) ^ (a & c) ^ (b & c);
        const u32 t2 = S0 + mj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
    ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

void sha256_init(Sha256 *ctx)
{
    ctx->h[0] = 0x6a09e667u; ctx->h[1] = 0xbb67ae85u; ctx->h[2] = 0x3c6ef372u; ctx->h[3] = 0xa54ff53au;
    ctx->h[4] = 0x510e527fu; ctx->h[5] = 0x9b05688cu; ctx->h[6] = 0x1f83d9abu; ctx->h[7] = 0x5be0cd19u;
    ctx->total = 0;
    ctx->buflen = 0;
}

void sha256_update(Sha256 *ctx, const void *data, size_t len)
{
    const u8 *p = (const u8 *)data;
    ctx->total += len;

    if (ctx->buflen)
    {
        while (len && ctx->buflen < 64)
        {
            ctx->buf[ctx->buflen++] = *p++;
            --len;
        }
        if (ctx->buflen == 64)
        {
            compress(ctx, ctx->buf);
            ctx->buflen = 0;
        }
    }

    while (len >= 64)
    {
        compress(ctx, p);
        p += 64;
        len -= 64;
    }

    while (len--)
    {
        ctx->buf[ctx->buflen++] = *p++;
    }
}

void sha256_final(Sha256 *ctx, u8 out[32])
{
    const u64 bits = ctx->total * 8;

    u8 pad[72];
    xmemset(pad, 0, sizeof(pad));
    pad[0] = 0x80;

    u32 padLen = (ctx->buflen < 56) ? (56 - ctx->buflen) : (120 - ctx->buflen);
    sha256_update(ctx, pad, padLen);

    u8 lenBytes[8];
    for (int i = 0; i < 8; ++i) lenBytes[i] = (u8)(bits >> (56 - i * 8));
    sha256_update(ctx, lenBytes, 8);

    for (int i = 0; i < 8; ++i)
    {
        out[i * 4] = (u8)(ctx->h[i] >> 24);
        out[i * 4 + 1] = (u8)(ctx->h[i] >> 16);
        out[i * 4 + 2] = (u8)(ctx->h[i] >> 8);
        out[i * 4 + 3] = (u8)ctx->h[i];
    }
}

void sha256(const void *data, size_t len, u8 out[32])
{
    Sha256 ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

void hmac_sha256(const u8 *key, size_t keyLen, const u8 *msg, size_t msgLen, u8 out[32])
{
    u8 k[64];
    xmemset(k, 0, sizeof(k));

    if (keyLen > 64)
    {
        sha256(key, keyLen, k); // first 32 bytes used, rest stay zero
    }
    else
    {
        xmemcpy(k, key, keyLen);
    }

    u8 ipad[64], opad[64];
    for (int i = 0; i < 64; ++i)
    {
        ipad[i] = (u8)(k[i] ^ 0x36);
        opad[i] = (u8)(k[i] ^ 0x5c);
    }

    Sha256 ctx;
    u8 inner[32];

    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, msg, msgLen);
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}

void pbkdf2_sha256(const char *password, size_t passLen, const u8 *salt, size_t saltLen,
                   u32 iterations, u8 out[32])
{
    // dkLen == 32 -> single block, U1 = HMAC(P, S || INT(1)).
    u8 msg[16 + 256 + 4];
    u32 saltBytes = saltLen > 256 ? 256 : (u32)saltLen;

    xmemcpy(msg, salt, saltBytes);
    msg[saltBytes + 0] = 0;
    msg[saltBytes + 1] = 0;
    msg[saltBytes + 2] = 0;
    msg[saltBytes + 3] = 1;

    u8 u[32], t[32];
    hmac_sha256((const u8 *)password, passLen, msg, (size_t)saltBytes + 4, u);
    xmemcpy(t, u, 32);

    for (u32 i = 1; i < iterations; ++i)
    {
        hmac_sha256((const u8 *)password, passLen, u, 32, u);
        for (int k = 0; k < 32; ++k) t[k] ^= u[k];
    }

    xmemcpy(out, t, 32);
}
