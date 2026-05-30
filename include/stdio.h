#ifndef __STDIO_H__
#define __STDIO_H__

#include <types.h>
#include <stdarg.h>

/* 内核格式化输出到屏幕 */
void kprintf(const char *fmt, ...);

/* 带颜色的格式化输出 */
void kprintf_color(uint32_t fg, const char *fmt, ...);

/* 格式化到字符串缓冲区 */
int sprintf(char *buf, const char *fmt, ...);

/* 安全版本，限制长度 */
int snprintf(char *buf, size_t size, const char *fmt, ...);

/* 内部: 格式化输出核心 (用于 kprintf 和 sprintf) */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

#endif /* __STDIO_H__ */