#ifndef __STRING_H__
#define __STRING_H__

/*===========================================================================
 * string.h — 内核字符串与内存操作库
 *
 * 提供裸机环境下的内存和字符串操作函数，不依赖标准 C 库。
 * 内存操作函数针对 x86-64 进行了优化，在对齐条件满足时使用
 * rep stosq（8字节填充）或 rep movsq（8字节复制）指令。
 *
 * 实现参考: xv6 操作系统，适配 64 位架构。
 *===========================================================================*/

#include <types.h>

/*---------------------------------------------------------------------------
 * 内存操作函数
 *---------------------------------------------------------------------------*/

/*
 * memset — 内存填充
 * 将 dst 开始的 n 字节全部设置为 c（取低 8 位）。
 * 64位优化: 8字节对齐时使用 rep stosq，4字节对齐时使用 rep stosl。
 * 返回: dst
 */
void  *memset(void *dst, int c, size_t n);

/*
 * memcpy — 内存复制
 * 将 src 开始的 n 字节复制到 dst。
 * 实现上直接调用 memmove，可安全处理重叠情况。
 * 返回: dst
 */
void  *memcpy(void *dst, const void *src, size_t n);

/*
 * memmove — 内存移动（安全处理重叠）
 * 将 src 开始的 n 字节复制到 dst。
 * 当 src < dst 且区域重叠时，从后向前复制，避免数据损坏。
 * 64位优化: 无重叠且对齐时使用 rep movsq。
 * 返回: dst
 */
void  *memmove(void *dst, const void *src, size_t n);

/*
 * memcmp — 内存比较
 * 逐字节比较 v1 和 v2 的前 n 字节。
 * 返回: 0(相等), 正数(v1>v2), 负数(v1<v2)
 */
int    memcmp(const void *v1, const void *v2, size_t n);

/*
 * memchr — 在内存中查找字符
 * 在 s 开始的 n 字节中查找首个值为 c 的字节。
 * 返回: 找到则返回指向该字节的指针，否则返回 NULL
 */
void  *memchr(const void *s, int c, size_t n);

/*---------------------------------------------------------------------------
 * 字符串操作函数
 *---------------------------------------------------------------------------*/

/*
 * strlen — 字符串长度
 * 返回 s 中 '\0' 之前的字符数（不含 '\0'）。
 */
size_t strlen(const char *s);

/*
 * strcmp — 字符串比较
 * 逐字节比较两个字符串。
 * 返回: 0(相等), 正数(a>b), 负数(a<b)
 */
int    strcmp(const char *a, const char *b);

/*
 * strncmp — 字符串前 n 字节比较
 * 比较 a 和 b 的前 n 个字节（遇到 '\0' 提前结束）。
 * 返回: 0(相等), 正数(a>b), 负数(a<b)
 */
int    strncmp(const char *a, const char *b, size_t n);

/*
 * strcpy — 字符串复制
 * 将 src（含 '\0'）复制到 dst。调用者保证 dst 空间足够。
 * 返回: dst
 */
char  *strcpy(char *dst, const char *src);

/*
 * strncpy — 字符串前 n 字节复制
 * 复制 src 的前 n 个字节到 dst；若 src 不足 n 字节，剩余用 '\0' 填充。
 * 注意: 不保证结果以 '\0' 结尾（src 恰好 n 字节时），建议用 safestrcpy。
 * 返回: dst
 */
char  *strncpy(char *dst, const char *src, size_t n);

/*
 * safestrcpy — 安全字符串复制 (xv6 风格)
 * 复制 src 到 dst，最多写入 n-1 个字符，并确保末尾有 '\0'。
 * 与 strncpy 的区别: 保证结果以 '\0' 结尾，推荐使用此函数。
 * 返回: dst
 */
char  *safestrcpy(char *dst, const char *src, size_t n);

/*
 * strcat — 字符串拼接
 * 将 src 追加到 dst 末尾（dst 必须有足够空间容纳 src）。
 * 返回: dst
 */
char  *strcat(char *dst, const char *src);

/*
 * strchr — 正向查找字符
 * 在字符串 s 中从前向后查找首个值为 c 的字符（包括 '\0'）。
 * 返回: 找到则返回指针，否则返回 NULL
 */
char  *strchr(const char *s, int c);

/*
 * strrchr — 反向查找字符
 * 在字符串 s 中从后向前查找最后一个值为 c 的字符（包括 '\0'）。
 * 返回: 找到则返回指针，否则返回 NULL
 */
char  *strrchr(const char *s, int c);

/*---------------------------------------------------------------------------
 * 数值转字符串函数
 *---------------------------------------------------------------------------*/

/*
 * uitoa — 无符号整数转字符串
 * 将 n 按 base 进制转换为字符串，写入 buf。
 * base 范围: 2~16；超出范围时输出 "0"。
 * 注意: buf 需足够大（base=2 时最多 65 字节含 '\0'）。
 * 返回: buf
 */
char  *uitoa(uint64_t n, char *buf, int base);

/*
 * itoa — 有符号整数转十进制字符串
 * 负数在首位加 '-'，然后转换绝对值。
 * 返回: buf
 */
char  *itoa(int64_t n, char *buf);

/*
 * u64_to_hex — 64 位整数转十六进制字符串
 * 输出格式: "0xXXXXXXXXXXXXXXXX"（固定 18 字节，含末尾 '\0'）。
 * 用于打印指针和地址。
 * 返回: buf
 */
char  *u64_to_hex(uint64_t n, char *buf);

#endif /* __STRING_H__ */
