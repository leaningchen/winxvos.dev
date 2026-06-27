#include <types.h>
#include <x86_64.h>

/*===========================================================================
 * assert.c — 内核断言辅助编译单元
 *
 * assert 宏本身定义在 include/assert.h 中:
 *   #define assert(cond) \
 *       do { if (!(cond)) panic("assertion failed: " #cond); } while(0)
 *
 * assert 调用链:
 *   assert(cond)  →  panic(msg)  →  video_print + hlt
 *                         ↑
 *                   实现在 kernel/panic.c
 *
 * 本文件作为占位编译单元存在，确保 assert.c 参与链接（见 Makefile 的
 * KERNEL_C_SRCS 列表）。未来若需要在此添加调试辅助函数（如 kassert_verbose
 * 打印文件名和行号），可在此处扩展，而无需修改 Makefile。
 *===========================================================================*/

/*
 * 目前无需实现额外函数。
 * 断言相关的所有逻辑:
 *   - 条件判断宏: include/assert.h
 *   - panic 实现: kernel/panic.c
 */
