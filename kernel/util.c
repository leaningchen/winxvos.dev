#include <types.h>
#include "util.h"

/*---------------------------------------------------------------------------
 * uitoa — 无符号整数转字符串
 *---------------------------------------------------------------------------*/
char *uitoa(uint64_t n, char *buf, int base)
{
    static const char digits[] = "0123456789ABCDEF";
    char tmp[21];
    int  i = 0;

    if (base < 2 || base > 16) {
        buf[0] = '0'; buf[1] = '\0';
        return buf;
    }

    if (n == 0) {
        buf[0] = '0'; buf[1] = '\0';
        return buf;
    }

    while (n > 0) {
        tmp[i++] = digits[n % (uint64_t)base];
        n /= (uint64_t)base;
    }

    /* 反转 */
    int j = 0;
    while (i > 0)
        buf[j++] = tmp[--i];
    buf[j] = '\0';

    return buf;
}

/*---------------------------------------------------------------------------
 * itoa — 有符号整数转十进制字符串
 *---------------------------------------------------------------------------*/
char *itoa(int64_t n, char *buf)
{
    if (n < 0) {
        buf[0] = '-';
        uitoa((uint64_t)(-n), buf + 1, 10);
    } else {
        uitoa((uint64_t)n, buf, 10);
    }
    return buf;
}

/*---------------------------------------------------------------------------
 * u64_to_hex — 格式化为 "0xXXXXXXXXXXXXXXXX"（18字节含结尾\0）
 *---------------------------------------------------------------------------*/
char *u64_to_hex(uint64_t n, char *buf)
{
    static const char hex[] = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++)
        buf[2 + i] = hex[(n >> (60 - i * 4)) & 0xF];
    buf[18] = '\0';
    return buf;
}

/*---------------------------------------------------------------------------
 * kmemset
 *---------------------------------------------------------------------------*/
void *kmemset(void *dst, int val, size_t n)
{
    unsigned char *p = (unsigned char *)dst;
    unsigned char  v = (unsigned char)val;
    while (n--) *p++ = v;
    return dst;
}

/*---------------------------------------------------------------------------
 * kmemcpy
 *---------------------------------------------------------------------------*/
void *kmemcpy(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

/*---------------------------------------------------------------------------
 * kstrlen
 *---------------------------------------------------------------------------*/
size_t kstrlen(const char *s)
{
    size_t n = 0;
    while (*s++) n++;
    return n;
}

/*---------------------------------------------------------------------------
 * kstrcpy
 *---------------------------------------------------------------------------*/
char *kstrcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

/*---------------------------------------------------------------------------
 * kstrncmp
 *---------------------------------------------------------------------------*/
int kstrncmp(const char *a, const char *b, size_t n)
{
    while (n-- > 0) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (*a == '\0') return 0;
        a++; b++;
    }
    return 0;
}
