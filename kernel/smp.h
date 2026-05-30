#ifndef __SMP_H__
#define __SMP_H__

#include <types.h>
#include <boot_info.h>

/* SMP 初始化：发现并唤醒所有 AP
 * 返回值：最终在线的 CPU 核心总数（含 BSP）
 */
int smp_init(BootInfo *info);

/* 获取当前在线的 CPU 数量（读取原子计数器）*/
int smp_cpu_count(void);

#endif /* __SMP_H__ */
