#include <types.h>
#include <x86_64.h>

/* assert.c — 断言辅助实现 (宏定义在 include/assert.h) */

/* 当前仅作为占位编译单元，assert 宏直接调用 panic() */

/* 以下是 panic 需要的输出接口声明 (实际实现见 panic.c) */