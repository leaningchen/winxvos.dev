#ifndef __ASSERT_H__
#define __ASSERT_H__

#include <types.h>

/* 断言宏 — 条件不满足时调用 panic */
#define assert(cond) \
    do { if (!(cond)) panic("assertion failed: " #cond); } while(0)

/* panic 函数声明 */
void panic(const char *msg);

#endif /* __ASSERT_H__ */