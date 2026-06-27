#ifndef __MEMLAYOUT_H__
#define __MEMLAYOUT_H__

#include <types.h>
#include <param.h>

/*
 * WinixOS 64位内存布局
 * 当前使用 1:1 物理映射（虚拟地址 = 物理地址）
 * 后续可切换到更高位的内核虚拟地址空间
 */

/* 内核物理起始地址 (由 bootloader 加载到此处) */
#define KERN_PHYS_BASE   0x100000ULL

/* 物理内存上限 (默认2GB，实际根据 E820 动态调整) */
#define PHYSTOP          0x80000000ULL

/* 内核镜像结束地址 (由 linker/kernel.ld 导出) */
extern char __kernel_end[];

/* 页对齐宏 (依赖 param.h 的 PGSIZE) */
#define PGROUNDUP(x)     ALIGN_UP((uintptr_t)(x), PGSIZE)
#define PGROUNDDOWN(x)   ALIGN_DOWN((uintptr_t)(x), PGSIZE)

#endif /* __MEMLAYOUT_H__ */