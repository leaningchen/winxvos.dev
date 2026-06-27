#ifndef __PARAM_H__
#define __PARAM_H__

/* 进程与系统配置参数 */
#define NPROC         64      // 最大进程数
#define KSTACKSIZE    4096    // 每进程内核栈大小
#define NCPU          32      // 最大 CPU 数
#define NOFILE        16      // 每进程打开文件数
#define NFILE         100     // 系统全局打开文件数
#define MAXARG        32      // exec 最大参数数

/* 内存参数 */
#define PGSIZE        4096    // 页大小 (4KB)
#define PGSHIFT       12      // 页偏移位数
#define PGMASK        (PGSIZE - 1)  // 页内偏移掩码

#endif /* __PARAM_H__ */