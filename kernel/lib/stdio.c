#include <types.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <x86_64.h>
#include "video.h"

/* QEMU debug I/O port: writing a byte to port 0xE9 outputs it to the console */
#define QEMU_DEBUG_PORT  0xE9

static inline void debug_putchar(char c)
{
    outb(QEMU_DEBUG_PORT, (uint8_t)c);
}

static void debug_puts(const char *s)
{
    while (*s)
        debug_putchar(*s++);
}

/*---------------------------------------------------------------------------
 * kprintf — 内核格式化输出
 * 参考 xv6 的 console.c/cprintf，适配64位
 *
 * 支持格式:
 *   %d   — 有符号十进制 (int)
 *   %u   — 无符号十进制 (unsigned int)
 *   %ld  — 有符号十进制 (long / int64_t)
 *   %lu  — 无符号十进制 (unsigned long / uint64_t)
 *   %x   — 十六进制 (小写)
 *   %X   — 十六进制 (大写)
 *   %p   — 指针 (0xXXXXXXXX 格式)
 *   %s   — 字符串
 *   %c   — 字符
 *   %%   — 百分号
 *---------------------------------------------------------------------------*/

/* 辅助函数: 将 tmp 字符串写入输出缓冲区 */
static int put_tmp(char *out, char *end, const char *tmp)
{
    char *start = out;
    while (*tmp && out < end)
        *out++ = *tmp++;
    return (int)(out - start);
}

/*---------------------------------------------------------------------------
 * vsnprintf — 格式化字符串到缓冲区
 * 返回写入的字符数 (不含末尾 \0)
 *---------------------------------------------------------------------------*/
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    char    *out = buf;
    char    *end = buf + size - 1;
    char    tmp[32];

    if (size <= 0)
        return 0;

    while (*fmt && out < end) {
        if (*fmt != '%') {
            *out++ = *fmt++;
            continue;
        }

        fmt++;  /* 跳过 '%' */

        /* 处理长度修饰符 */
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        switch (*fmt) {
        case 'd': {
            if (is_long) {
                int64_t val = va_arg(ap, int64_t);
                itoa(val, tmp);
            } else {
                int val = va_arg(ap, int);
                itoa((int64_t)val, tmp);
            }
            out += put_tmp(out, end, tmp);
            fmt++;
            break;
        }

        case 'u': {
            if (is_long) {
                uint64_t val = va_arg(ap, uint64_t);
                uitoa(val, tmp, 10);
            } else {
                unsigned int val = va_arg(ap, unsigned int);
                uitoa((uint64_t)val, tmp, 10);
            }
            out += put_tmp(out, end, tmp);
            fmt++;
            break;
        }

        case 'x': {
            if (is_long) {
                uint64_t val = va_arg(ap, uint64_t);
                uitoa(val, tmp, 16);
            } else {
                unsigned int val = va_arg(ap, unsigned int);
                uitoa((uint64_t)val, tmp, 16);
            }
            /* 小写十六进制 */
            for (int i = 0; tmp[i]; i++)
                tmp[i] = tolower(tmp[i]);
            out += put_tmp(out, end, tmp);
            fmt++;
            break;
        }

        case 'X': {
            if (is_long) {
                uint64_t val = va_arg(ap, uint64_t);
                uitoa(val, tmp, 16);
            } else {
                unsigned int val = va_arg(ap, unsigned int);
                uitoa((uint64_t)val, tmp, 16);
            }
            out += put_tmp(out, end, tmp);
            fmt++;
            break;
        }

        case 'p': {
            uint64_t val = va_arg(ap, uint64_t);
            u64_to_hex(val, tmp);
            out += put_tmp(out, end, tmp);
            fmt++;
            break;
        }

        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == NULL)
                s = "(null)";
            out += put_tmp(out, end, s);
            fmt++;
            break;
        }

        case 'c': {
            int c = va_arg(ap, int);
            if (out < end)
                *out++ = (char)c;
            fmt++;
            break;
        }

        case '%': {
            if (out < end)
                *out++ = '%';
            fmt++;
            break;
        }

        default: {
            if (out < end)
                *out++ = '%';
            if (out < end)
                *out++ = *fmt;
            fmt++;
            break;
        }
        }
    }

    *out = '\0';
    return (int)(out - buf);
}

/*---------------------------------------------------------------------------
 * sprintf — 格式化到字符串缓冲区
 *---------------------------------------------------------------------------*/
int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return n;
}

/*---------------------------------------------------------------------------
 * snprintf — 安全格式化到缓冲区
 *---------------------------------------------------------------------------*/
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/*---------------------------------------------------------------------------
 * kprintf — 内核格式化输出到屏幕 (白色文字)
 *---------------------------------------------------------------------------*/
void kprintf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    video_print(buf, COLOR_WHITE);
    debug_puts(buf);
}

/*---------------------------------------------------------------------------
 * kprintf_color — 带颜色的格式化输出
 *---------------------------------------------------------------------------*/
void kprintf_color(uint32_t fg, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    video_print(buf, fg);
    debug_puts(buf);
}