#ifndef __DEFS_H__
#define __DEFS_H__

#include <types.h>
#include <boot_info.h>

/* 前置类型声明 (避免头文件循环依赖) */
struct spinlock;
struct trapframe;
struct cpu;
typedef struct ACPI_Header ACPI_Header;

/* kalloc.c */
void  kinit(BootInfo *info);
void *kalloc(void);
void  kfree(void *);

/* spinlock.c */
void initlock(struct spinlock *, char *);
void acquire(struct spinlock *);
void release(struct spinlock *);
int  holding(struct spinlock *);
void pushcli(void);
void popcli(void);

/* cpu.c */
int        cpuid(void);
struct cpu *mycpu(void);

/* idt.c */
void idt_init(void);
void idt_set_gate(int, void *, uint8_t, uint16_t);
void trap_handler(struct trapframe *);

/* panic.c */
void panic(const char *);

/* video.c */
void video_init(uint64_t, uint32_t, uint32_t, uint32_t, uint8_t);
void video_clear(uint32_t);
void video_print(const char *, uint32_t);
void video_print_at(int, int, const char *, uint32_t);

/* smp.c */
int  smp_init(BootInfo *);
int  smp_cpu_count(void);

/* acpi.c */
void         acpi_init(uint64_t);
ACPI_Header *acpi_find_table(const char *);
int          acpi_get_lapic_ids(uint8_t *, int);
uint64_t     acpi_get_lapic_base(void);

#endif /* __DEFS_H__ */