#ifndef __STRING_H__
#define __STRING_H__

#include <types.h>

/* 内存操作 */
void  *memset(void *dst, int c, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *v1, const void *v2, size_t n);
void  *memchr(const void *s, int c, size_t n);

/* 字符串操作 */
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *safestrcpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);

/* 数值转换 */
char  *uitoa(uint64_t n, char *buf, int base);
char  *itoa(int64_t n, char *buf);
char  *u64_to_hex(uint64_t n, char *buf);

#endif /* __STRING_H__ */