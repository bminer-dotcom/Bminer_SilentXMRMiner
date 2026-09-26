/* Shim for memset_explicit (C23). The MSYS2-built libsodium.a references it,
 * but older mingw-w64 CRTs do not export it. This definition satisfies the
 * link; the volatile write cannot be optimised away, matching the intent. */
#include <stddef.h>

void *memset_explicit(void *s, int c, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *)s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}
