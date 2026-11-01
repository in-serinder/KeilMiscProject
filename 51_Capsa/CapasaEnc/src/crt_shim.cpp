// crt_shim.cpp - the handful of libc symbols MSVC may still emit when the
// CRT is excluded with /NODEFAULTLIB.
#include <stddef.h>

extern "C" void *__cdecl memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

extern "C" void *__cdecl memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s)
    {
        while (n--) *d++ = *s++;
    }
    else
    {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

extern "C" void *__cdecl memset(void *dst, int value, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char v = (unsigned char)value;
    while (n--) *d++ = v;
    return dst;
}

extern "C" int __cdecl memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (size_t i = 0; i < n; ++i)
    {
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

// Referenced by some MSVC code paths that emit floating point literals.
extern "C" int _fltused = 0;
