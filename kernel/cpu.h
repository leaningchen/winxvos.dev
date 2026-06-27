#ifndef __CPU_H__
#define __CPU_H__

#include <types.h>

/* CPU 结构体 — 每个 CPU 核心对应一个 */
struct cpu {
    uint8_t      apicid;      /* LAPIC ID */
    uint8_t      started;     /* 是否已启动 */
    int          ncli;        /* pushcli 嵌套深度 */
    int          intena;      /* pushcli 前中断是否开启 */
    struct proc *proc;        /* 当前在此 CPU 上运行的进程 */
};

/* CPU 数组 */
extern struct cpu cpus[NCPU];

/* 获取当前 CPU 编号和指针 */
int        cpuid(void);
struct cpu *mycpu(void);

#endif /* __CPU_H__ */