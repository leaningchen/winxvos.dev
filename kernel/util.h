#ifndef __UTIL_H__
#define __UTIL_H__

#include <types.h>

/* 无符号整数转字符串
 * @n:    待转换的数值
 * @buf:  输出缓冲区（调用者保证足够大，最少 21 字节）
 * @base: 进制（10 或 16）
 * 返回: buf 指针
 */
char *uitoa(uint64_t n, char *buf, int base);

/* 有符号整数转字符串（十进制）*/
char *itoa(int64_t n, char *buf);

/* 设置内存 */
void *kmemset(void *dst, int val, size_t n);

/* 复制内存 */
void *kmemcpy(void *dst, const void *src, size_t n);

/* 字符串长度 */
size_t kstrlen(const char *s);

/* 字符串复制 */
char *kstrcpy(char *dst, const char *src);

/* 字符串比较（前 n 字节）*/
int kstrncmp(const char *a, const char *b, size_t n);

/* 将 uint64_t 格式化为 "0x%016X" 十六进制字符串（17 字节）*/
char *u64_to_hex(uint64_t n, char *buf);

#endif /* __UTIL_H__ */
