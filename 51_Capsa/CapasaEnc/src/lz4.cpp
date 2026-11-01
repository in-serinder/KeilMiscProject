// lz4.cpp - compact LZ4 block compressor / decompressor.
#include "lz4.h"

#define LZ4_HASH_BITS 12
#define LZ4_HASH_SIZE (1u << LZ4_HASH_BITS)

// 16 KB of static scratch keeps the stack frames small (no __chkstk needed).
static u32 g_hash[LZ4_HASH_SIZE];

static inline u32 lz4_hash(u32 sequence)
{
    return (sequence * 2654435761u) >> (32 - LZ4_HASH_BITS);
}

static inline u32 read32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

size_t lz4_bound(size_t input)
{
    return input + input / 255 + 16;
}

size_t lz4_compress_block(const u8 *src, size_t srcLen, u8 *dst, size_t dstCap)
{
    if (srcLen == 0 || srcLen > 0x7E000000u) return 0;
    if (dstCap < lz4_bound(srcLen)) return 0;

    xmemset(g_hash, 0, sizeof(g_hash));

    const u8 *ip = src;
    const u8 *anchor = ip;
    const u8 *iend = src + srcLen;

    u8 *op = dst;

    if (srcLen >= 13)
    {
        const u8 *mflimit = iend - 12;   // matches must start at or before this
        const u8 *matchlimit = iend - 5; // and must not cross this

        while (ip < mflimit)
        {
            const u32 seq = read32(ip);
            const u32 h = lz4_hash(seq);
            const u32 ref = g_hash[h];
            g_hash[h] = (u32)(ip - src) + 1;

            if (ref != 0)
            {
                const u8 *match = src + (ref - 1);
                if ((size_t)(ip - match) <= 65535 && read32(match) == seq)
                {
                    // literal run
                    const size_t litLen = (size_t)(ip - anchor);
                    u8 *token = op++;

                    if (litLen >= 15)
                    {
                        *token = (u8)(15 << 4);
                        size_t rest = litLen - 15;
                        while (rest >= 255)
                        {
                            *op++ = 255;
                            rest -= 255;
                        }
                        *op++ = (u8)rest;
                    }
                    else
                    {
                        *token = (u8)(litLen << 4);
                    }

                    xmemcpy(op, anchor, litLen);
                    op += litLen;

                    // extend the match
                    const u8 *matchStart = ip;
                    const u8 *m = match + 4;
                    const u8 *p = ip + 4;
                    while (p < matchlimit && *p == *m)
                    {
                        ++p;
                        ++m;
                    }

                    const size_t offset = (size_t)(matchStart - match);
                    const size_t lenMinus4 = (size_t)(m - match) - 4;
                    ip = p;

                    *op++ = (u8)(offset & 0xFF);
                    *op++ = (u8)(offset >> 8);

                    if (lenMinus4 >= 15)
                    {
                        *token |= 15;
                        size_t rest = lenMinus4 - 15;
                        while (rest >= 255)
                        {
                            *op++ = 255;
                            rest -= 255;
                        }
                        *op++ = (u8)rest;
                    }
                    else
                    {
                        *token |= (u8)lenMinus4;
                    }

                    anchor = ip;
                    continue;
                }
            }

            ++ip;
        }
    }

    // trailing literals
    const size_t litLen = (size_t)(iend - anchor);
    if (litLen >= 15)
    {
        *op++ = (u8)(15 << 4);
        size_t rest = litLen - 15;
        while (rest >= 255)
        {
            *op++ = 255;
            rest -= 255;
        }
        *op++ = (u8)rest;
    }
    else
    {
        *op++ = (u8)(litLen << 4);
    }

    xmemcpy(op, anchor, litLen);
    op += litLen;

    return (size_t)(op - dst);
}

size_t lz4_decompress_block(const u8 *src, size_t srcLen, u8 *dst, size_t dstCap)
{
    const u8 *ip = src;
    const u8 *iend = src + srcLen;
    u8 *op = dst;
    u8 *oend = dst + dstCap;

    while (ip < iend)
    {
        const u8 token = *ip++;

        size_t litLen = (size_t)(token >> 4);
        if (litLen == 15)
        {
            for (;;)
            {
                if (ip >= iend) return 0;
                const u8 add = *ip++;
                litLen += add;
                if (add != 255) break;
            }
        }

        if (ip + litLen > iend || op + litLen > oend) return 0;
        xmemcpy(op, ip, litLen);
        op += litLen;
        ip += litLen;

        if (ip >= iend) break; // last sequence has literals only

        if (ip + 2 > iend) return 0;
        const size_t offset = (size_t)ip[0] | ((size_t)ip[1] << 8);
        ip += 2;
        if (offset == 0 || (size_t)(op - dst) < offset) return 0;

        size_t matchLen = (size_t)(token & 0x0F);
        if (matchLen == 15)
        {
            for (;;)
            {
                if (ip >= iend) return 0;
                const u8 add = *ip++;
                matchLen += add;
                if (add != 255) break;
            }
        }
        matchLen += 4;

        if (op + matchLen > oend) return 0;

        const u8 *match = op - offset;
        if (offset >= matchLen)
        {
            xmemcpy(op, match, matchLen);
            op += matchLen;
        }
        else
        {
            for (size_t i = 0; i < matchLen; ++i) op[i] = match[i];
            op += matchLen;
        }
    }

    return (size_t)(op - dst);
}
