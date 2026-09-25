#ifndef __BLAKE2S_H__
#define __BLAKE2S_H__

#include "AI8G.h"

#define BLAKE2S_MIN_LEN 6     /* 输入字符串最小长度 */
#define BLAKE2S_MAX_LEN 16    /* 输入字符串最大长度 */
#define BLAKE2S_OUT_LEN 16    /* 输出校验码字节数 */
#define BLAKE2S_FOLD_OP 1     /* 32字节摘要折半合成运算: 0=OR 1=XOR 2=AND */

/* BLAKE2s-256摘要折半: 输入6~16字节字符串, 输出16字节校验码 */
void BLAKE2s_Checksum(const char *in, uint8_t len, uint8_t *out);

#endif /* __BLAKE2S_H__ */