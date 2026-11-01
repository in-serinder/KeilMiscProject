// common.cpp - freestanding memory / string / formatting helpers.
#include "common.h"

void xmemcpy(void *dst, const void *src, size_t n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--) *d++ = *s++;
}

void xmemset(void *dst, u8 value, size_t n)
{
    u8 *d = (u8 *)dst;
    while (n--) *d++ = value;
}

int xmemcmp(const void *a, const void *b, size_t n)
{
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;
    for (size_t i = 0; i < n; ++i)
    {
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

size_t xstrlen(const char *s)
{
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

int xstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return (int)(u8)*a - (int)(u8)*b;
}

int xstrncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        if (a[i] != b[i]) return (int)(u8)a[i] - (int)(u8)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

int xstricmp_ascii(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return (int)(u8)ca - (int)(u8)cb;
        if (!ca) return 0;
    }
}

void xstrncpy(char *dst, const char *src, size_t cap)
{
    if (cap == 0) return;
    size_t i = 0;
    for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

int xwcscmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return (int)*a - (int)*b;
}

size_t fmt_u64(char *buf, size_t pos, u64 value)
{
    char tmp[24];
    size_t n = 0;
    if (value == 0)
    {
        buf[pos++] = '0';
        return pos;
    }
    while (value && n < sizeof(tmp))
    {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n) buf[pos++] = tmp[--n];
    return pos;
}

size_t fmt_size(char *buf, size_t pos, u64 bytes)
{
    // < 1024 -> "512 B", otherwise one decimal in KB/MB/GB.
    if (bytes < 1024)
    {
        pos = fmt_u64(buf, pos, bytes);
        buf[pos++] = ' ';
        buf[pos++] = 'B';
        return pos;
    }

    static const char *units[] = { "KB", "MB", "GB", "TB" };
    u64 scale = 1024;
    int unit = 0;
    while (unit < 3 && bytes / scale >= 1024)
    {
        scale *= 1024;
        ++unit;
    }

    u64 whole = bytes / scale;
    u64 frac = (bytes % scale) * 10 / scale;

    pos = fmt_u64(buf, pos, whole);
    if (frac)
    {
        buf[pos++] = '.';
        buf[pos++] = (char)('0' + frac);
    }
    buf[pos++] = ' ';
    buf[pos++] = units[unit][0];
    buf[pos++] = units[unit][1];
    return pos;
}

size_t fmt_hex8(char *buf, size_t pos, const u8 *data, size_t len)
{
    static const char *digits = "0123456789ABCDEF";
    for (size_t i = 0; i < len; ++i)
    {
        buf[pos++] = digits[data[i] >> 4];
        buf[pos++] = digits[data[i] & 0x0F];
    }
    return pos;
}

u64 parse_size(const char *text)
{
    if (!text || !*text) return 0;

    u64 value = 0;
    size_t i = 0;
    while (text[i] >= '0' && text[i] <= '9')
    {
        value = value * 10 + (u64)(text[i] - '0');
        ++i;
    }

    char suffix = text[i];
    if (suffix >= 'a' && suffix <= 'z') suffix = (char)(suffix - 'a' + 'A');

    if (suffix == 'K') return value * 1024;
    if (suffix == 'M') return value * 1024 * 1024;
    if (suffix == 'G') return value * 1024 * 1024 * 1024;
    return value;
}
