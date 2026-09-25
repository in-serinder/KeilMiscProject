#include "BLAKE2s.h"

#if BLAKE2S_FOLD_OP == 0
#define FOLD(a, b) ((a) | (b))
#elif BLAKE2S_FOLD_OP == 2
#define FOLD(a, b) ((a) & (b))
#else
#define FOLD(a, b) ((a) ^ (b))
#endif

#define ROT16(x) (((x) >> 16) | ((x) << 16))
#define ROT12(x) (((x) >> 12) | ((x) << 20))
#define ROT8(x)  (((x) >> 8)  | ((x) << 24))
#define ROT7(x)  (((x) >> 7)  | ((x) << 25))

static uint32_t xdata m[16]; /* 消息字 */
static uint32_t xdata v[16]; /* 工作状态 */

static void B2S_G(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                  uint32_t x, uint32_t y)
{
  uint32_t t;
  v[a] += v[b] + x;
    t = v[d] ^ v[a]; v[d] = ROT16(t);
  v[c] += v[d];
  t = v[b] ^ v[c]; v[b] = ROT12(t);
  v[a] += v[b] + y;
  t = v[d] ^ v[a]; v[d] = ROT8(t);
  v[c] += v[d];
  t = v[b] ^ v[c]; v[b] = ROT7(t);
}

#define GX(a, b, c, d, i, j) B2S_G(a, b, c, d, m[i], m[j])

#define PUT32(o, t) \
  out[o] = (uint8_t)(t); out[o + 1] = (uint8_t)((t) >> 8); \
  out[o + 2] = (uint8_t)((t) >> 16); out[o + 3] = (uint8_t)((t) >> 24)

/* 10轮展开(消息索引直接取自BLAKE2s sigma表, RD避免与C51寄存器名R0-R7冲突) */
#define RD0 \
  GX(0,4,8,12,0,1);  GX(1,5,9,13,2,3);   GX(2,6,10,14,4,5);  GX(3,7,11,15,6,7); \
  GX(0,5,10,15,8,9); GX(1,6,11,12,10,11); GX(2,7,8,13,12,13); GX(3,4,9,14,14,15)
#define RD1 \
  GX(0,4,8,12,14,10); GX(1,5,9,13,4,8);  GX(2,6,10,14,9,15); GX(3,7,11,15,13,6); \
  GX(0,5,10,15,1,12); GX(1,6,11,12,0,2); GX(2,7,8,13,11,7);  GX(3,4,9,14,5,3)
#define RD2 \
  GX(0,4,8,12,11,8); GX(1,5,9,13,12,0);  GX(2,6,10,14,5,2);  GX(3,7,11,15,15,13); \
  GX(0,5,10,15,10,14); GX(1,6,11,12,3,6); GX(2,7,8,13,7,1); GX(3,4,9,14,9,4)
#define RD3 \
  GX(0,4,8,12,7,9); GX(1,5,9,13,3,1);  GX(2,6,10,14,13,12); GX(3,7,11,15,11,14); \
  GX(0,5,10,15,2,6); GX(1,6,11,12,5,10); GX(2,7,8,13,4,0); GX(3,4,9,14,15,8)
#define RD4 \
  GX(0,4,8,12,9,0); GX(1,5,9,13,5,7);  GX(2,6,10,14,2,4);  GX(3,7,11,15,10,15); \
  GX(0,5,10,15,14,1); GX(1,6,11,12,11,12); GX(2,7,8,13,6,8); GX(3,4,9,14,3,13)
#define RD5 \
  GX(0,4,8,12,2,12); GX(1,5,9,13,6,10); GX(2,6,10,14,0,11); GX(3,7,11,15,8,3); \
  GX(0,5,10,15,4,13); GX(1,6,11,12,7,5); GX(2,7,8,13,15,14); GX(3,4,9,14,1,9)
#define RD6 \
  GX(0,4,8,12,12,5); GX(1,5,9,13,1,15); GX(2,6,10,14,14,13); GX(3,7,11,15,4,10); \
  GX(0,5,10,15,0,7); GX(1,6,11,12,6,3); GX(2,7,8,13,9,2); GX(3,4,9,14,8,11)
#define RD7 \
  GX(0,4,8,12,13,11); GX(1,5,9,13,7,14); GX(2,6,10,14,12,1); GX(3,7,11,15,3,9); \
  GX(0,5,10,15,5,0); GX(1,6,11,12,15,4); GX(2,7,8,13,8,6); GX(3,4,9,14,2,10)
#define RD8 \
  GX(0,4,8,12,6,15); GX(1,5,9,13,14,9); GX(2,6,10,14,11,3); GX(3,7,11,15,0,8); \
  GX(0,5,10,15,12,2); GX(1,6,11,12,13,7); GX(2,7,8,13,1,4); GX(3,4,9,14,10,5)
#define RD9 \
  GX(0,4,8,12,10,2); GX(1,5,9,13,8,4);  GX(2,6,10,14,7,6);  GX(3,7,11,15,1,5); \
  GX(0,5,10,15,15,11); GX(1,6,11,12,9,14); GX(2,7,8,13,3,12); GX(3,4,9,14,13,0)

void BLAKE2s_Checksum(const char *in, uint8_t len, uint8_t *out)
{
  uint8_t i;

  if (len > BLAKE2S_MAX_LEN)
    len = BLAKE2S_MAX_LEN;

  /* h=IV, h0^=0x01010000^(kk<<8)^nn, kk=0 nn=32 */
  v[0] = 0x6A09E667UL ^ 0x01010020UL;
  v[1] = 0xBB67AE85UL;
  v[2] = 0x3C6EF372UL;
  v[3] = 0xA54FF53AUL;
  v[4] = 0x510E527FUL;
  v[5] = 0x9B05688CUL;
  v[6] = 0x1F83D9ABUL;
  v[7] = 0x5BE0CD19UL;
  v[8]  = 0x6A09E667UL;
  v[9]  = 0xBB67AE85UL;
  v[10] = 0x3C6EF372UL;
  v[11] = 0xA54FF53AUL;
  v[12] = 0x510E527FUL ^ len;          /* 计数器t0=len */
  v[13] = 0x9B05688CUL;
  v[14] = 0x1F83D9ABUL ^ 0xFFFFFFFFUL; /* 末块标志f0 */
  v[15] = 0x5BE0CD19UL;

  /* 消息字(仅前4个字非零, 其余补0) */
  for (i = 0; i < 16; i++)
    m[i] = 0;
  for (i = 0; i < len; i++)
    m[i >> 2] |= ((uint32_t)(uint8_t)in[i]) << ((i & 3) << 3);

  RD0;
  RD1;
  RD2;
  RD3;
  RD4;
  RD5;
  RD6;
  RD7;
  RD8;
  RD9;

    /* 摘要字 = h0[i] ^ v[i] ^ v[i+8] (h0为压缩前状态), 先就地合成 v[i]^=v[i+8] */
  for (i = 0; i < 8; i++)
    v[i] ^= v[i + 8];

  /* 32字节摘要(h0为常数)折半合成16字节 */
  {
    uint32_t t = FOLD(0x6B08E647UL ^ v[0], 0x510E527FUL ^ v[4]);
    PUT32(0, t);
  }
  {
    uint32_t t = FOLD(0xBB67AE85UL ^ v[1], 0x9B05688CUL ^ v[5]);
    PUT32(4, t);
  }
  {
    uint32_t t = FOLD(0x3C6EF372UL ^ v[2], 0x1F83D9ABUL ^ v[6]);
    PUT32(8, t);
  }
  {
    uint32_t t = FOLD(0xA54FF53AUL ^ v[3], 0x5BE0CD19UL ^ v[7]);
    PUT32(12, t);
  }
}