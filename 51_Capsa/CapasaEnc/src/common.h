// common.h - freestanding helpers shared by the CLI and the SFX stub.
// No CRT, no STL: everything here is self-contained.
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t i32;
typedef int64_t i64;

// ---- memory / string ---------------------------------------------------- //

void *xalloc(size_t size);            // zeroed block, nullptr on failure
void xfree(void *p);

void xmemcpy(void *dst, const void *src, size_t n);
void xmemset(void *dst, u8 value, size_t n);
int xmemcmp(const void *a, const void *b, size_t n);
size_t xstrlen(const char *s);
int xstrcmp(const char *a, const char *b);
int xstrncmp(const char *a, const char *b, size_t n);
int xstricmp_ascii(const char *a, const char *b);
void xstrncpy(char *dst, const char *src, size_t cap);
int xwcscmp(const wchar_t *a, const wchar_t *b);

// ---- little-endian load/store ------------------------------------------- //

inline u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
inline u64 rd64(const u8 *p) { return (u64)rd32(p) | ((u64)rd32(p + 4) << 32); }

inline void wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
inline void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
inline void wr64(u8 *p, u64 v) { wr32(p, (u32)v); wr32(p + 4, (u32)(v >> 32)); }

// ---- number / text formatting (no printf) ------------------------------- //

// Appends a decimal/hex number to buf at *pos; returns the new position.
size_t fmt_u64(char *buf, size_t pos, u64 value);
size_t fmt_size(char *buf, size_t pos, u64 bytes);   // 1.23 MB
size_t fmt_hex8(char *buf, size_t pos, const u8 *data, size_t len);

inline size_t fmt_str(char *buf, size_t pos, const char *s)
{
    while (*s) buf[pos++] = *s++;
    return pos;
}

inline size_t fmt_ch(char *buf, size_t pos, char c)
{
    buf[pos++] = c;
    return pos;
}

// ---- misc --------------------------------------------------------------- //

// Size string ("120K", "1M", "0") -> bytes. Returns 0 on parse failure.
u64 parse_size(const char *text);

constexpr u32 kMaxNameBytes = 240;
