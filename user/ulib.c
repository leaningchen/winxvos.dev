/*===========================================================================
 * user/ulib.c — 用户态运行库（字符串函数、printf、gets 等）
 *
 * 这些函数在用户空间运行，不依赖内核内存分配器。
 * printf 通过 write(1,...) 输出到 stdout。
 *===========================================================================*/

#include "user.h"

/*---------------------------------------------------------------------------
 * 字符串函数
 *---------------------------------------------------------------------------*/

int
strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

char *
strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != 0)
        ;
    return dst;
}

char *
strncpy(char *dst, const char *src, uint n)
{
    char *d = dst;
    while (n > 0 && (*d++ = *src++) != 0)
        n--;
    while (n-- > 0)
        *d++ = 0;
    return dst;
}

int
strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int
strncmp(const char *a, const char *b, uint n)
{
    while (n > 0 && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *
strchr(const char *s, char c)
{
    while (*s) {
        if (*s == c) return (char *)s;
        s++;
    }
    return 0;
}

void *
memset(void *dst, int c, uint n)
{
    char *d = dst;
    while (n--) *d++ = (char)c;
    return dst;
}

void *
memmove(void *dst, const void *src, uint n)
{
    const char *s = src;
    char       *d = dst;
    if (s < d && s + n > d) {
        s += n; d += n;
        while (n--) *--d = *--s;
    } else {
        while (n--) *d++ = *s++;
    }
    return dst;
}

int
memcmp(const void *a, const void *b, uint n)
{
    const unsigned char *p = a, *q = b;
    while (n--) {
        if (*p != *q) return *p - *q;
        p++; q++;
    }
    return 0;
}

int
atoi(const char *s)
{
    int n = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9')
        n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

void
itoa(int v, char *buf, int base)
{
    char tmp[32];
    int  i = 0, neg = 0;
    if (v < 0 && base == 10) { neg = 1; v = -v; }
    if (v == 0) { tmp[i++] = '0'; }
    while (v > 0) {
        int d = v % base;
        tmp[i++] = d < 10 ? '0' + d : 'a' + d - 10;
        v /= base;
    }
    if (neg) tmp[i++] = '-';
    int j = 0;
    while (i-- > 0) buf[j++] = tmp[i];
    buf[j] = 0;
}

/*---------------------------------------------------------------------------
 * gets — 从 stdin 读一行（最多 max-1 字节）
 *---------------------------------------------------------------------------*/
char *
gets(char *buf, int max)
{
    int i = 0, cc;
    char c;

    while (i + 1 < max) {
        cc = read(0, &c, 1);
        if (cc < 1) break;
        buf[i++] = c;
        if (c == '\n' || c == '\r') break;
    }
    buf[i] = 0;
    return buf;
}

/*---------------------------------------------------------------------------
 * puts — 向 stdout 输出字符串（不换行）
 *---------------------------------------------------------------------------*/
void
puts(const char *s)
{
    write(1, s, strlen(s));
}

/*---------------------------------------------------------------------------
 * printf — 简化版，仅支持 %d %u %x %s %c %%
 *---------------------------------------------------------------------------*/
static void
putchar_fd(int fd, char c)
{
    write(fd, &c, 1);
}

static void
printint(int fd, long long v, int base, int sign)
{
    char buf[32];
    int  i = 0;
    unsigned long long x;

    if (sign && v < 0)
        x = -v;
    else
        x = (unsigned long long)v;

    do {
        int d = (int)(x % base);
        buf[i++] = d < 10 ? '0' + d : 'a' + d - 10;
        x /= base;
    } while (x);

    if (sign && v < 0)
        buf[i++] = '-';

    while (i-- > 0)
        putchar_fd(fd, buf[i]);
}

void
printf(int fd, const char *fmt, ...)
{
    /* 手动访问 va_list（不依赖 stdarg.h）*/
    unsigned long long *ap = (unsigned long long *)(&fmt + 1);

    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            putchar_fd(fd, fmt[i]);
            continue;
        }
        i++;
        switch (fmt[i]) {
        case 'd':
            printint(fd, (long long)*ap++, 10, 1);
            break;
        case 'u':
            printint(fd, (long long)*ap++, 10, 0);
            break;
        case 'x': case 'p':
            printint(fd, (long long)*ap++, 16, 0);
            break;
        case 's': {
            char *s = (char *)*ap++;
            if (!s) s = "(null)";
            while (*s) putchar_fd(fd, *s++);
            break;
        }
        case 'c':
            putchar_fd(fd, (char)*ap++);
            break;
        case '%':
            putchar_fd(fd, '%');
            break;
        default:
            putchar_fd(fd, '%');
            putchar_fd(fd, fmt[i]);
            break;
        }
    }
}
