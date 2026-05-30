#ifndef __KALLOC_H__
#define __KALLOC_H__

#include <types.h>
#include <boot_info.h>

void kinit(BootInfo *info);     /* 初始化分配器 (从 E820 可用区域) */
void *kalloc(void);             /* 分配一个 4096 字节物理页 */
void kfree(void *v);            /* 释放一个物理页 */

#endif /* __KALLOC_H__ */