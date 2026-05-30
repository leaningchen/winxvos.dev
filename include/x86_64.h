#ifndef __X86_64_H__
#define __X86_64_H__

#include <types.h>

/*---------------------------------------------------------------------------
 * I/O 端口操作
 *---------------------------------------------------------------------------*/

static inline uint8_t inb(uint16_t port)
{
    uint8_t data;
    __asm__ volatile("inb %1, %0" : "=a" (data) : "Nd" (port));
    return data;
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t data;
    __asm__ volatile("inw %1, %0" : "=a" (data) : "Nd" (port));
    return data;
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t data;
    __asm__ volatile("inl %1, %0" : "=a" (data) : "Nd" (port));
    return data;
}

static inline void outb(uint16_t port, uint8_t data)
{
    __asm__ volatile("outb %0, %1" : : "a" (data), "Nd" (port));
}

static inline void outw(uint16_t port, uint16_t data)
{
    __asm__ volatile("outw %0, %1" : : "a" (data), "Nd" (port));
}

static inline void outl(uint16_t port, uint32_t data)
{
    __asm__ volatile("outl %0, %1" : : "a" (data), "Nd" (port));
}

static inline void insl(uint16_t port, void *addr, int cnt)
{
    __asm__ volatile("cld; rep insl" :
                 "=D" (addr), "=c" (cnt) :
                 "d" (port), "0" (addr), "1" (cnt) :
                 "memory", "cc");
}

static inline void outsl(uint16_t port, const void *addr, int cnt)
{
    __asm__ volatile("cld; rep outsl" :
                 "=S" (addr), "=c" (cnt) :
                 "d" (port), "0" (addr), "1" (cnt) :
                 "cc");
}

/*---------------------------------------------------------------------------
 * 内存块操作 (64位优化)
 *---------------------------------------------------------------------------*/

/* 按字节填充 */
static inline void stosb(void *addr, int data, size_t cnt)
{
    __asm__ volatile("cld; rep stosb" :
                 "=D" (addr), "=c" (cnt) :
                 "0" (addr), "1" (cnt), "a" (data) :
                 "memory", "cc");
}

/* 按4字节填充 */
static inline void stosl(void *addr, uint32_t data, size_t cnt)
{
    __asm__ volatile("cld; rep stosl" :
                 "=D" (addr), "=c" (cnt) :
                 "0" (addr), "1" (cnt), "a" (data) :
                 "memory", "cc");
}

/* 按8字节填充 (64位优化) */
static inline void stosq(void *addr, uint64_t data, size_t cnt)
{
    __asm__ volatile("cld; rep stosq" :
                 "=D" (addr), "=c" (cnt) :
                 "0" (addr), "1" (cnt), "a" (data) :
                 "memory", "cc");
}

/* 按4字节复制 */
static inline void movsl(void *dst, const void *src, size_t cnt)
{
    __asm__ volatile("cld; rep movsl" :
                 "=D" (dst), "=S" (src), "=c" (cnt) :
                 "0" (dst), "1" (src), "2" (cnt) :
                 "memory", "cc");
}

/* 按8字节复制 (64位优化) */
static inline void movsq(void *dst, const void *src, size_t cnt)
{
    __asm__ volatile("cld; rep movsq" :
                 "=D" (dst), "=S" (src), "=c" (cnt) :
                 "0" (dst), "1" (src), "2" (cnt) :
                 "memory", "cc");
}

/*---------------------------------------------------------------------------
 * 控制寄存器操作 (64位)
 *---------------------------------------------------------------------------*/

static inline uint64_t rcr0(void)
{
    uint64_t val;
    __asm__ volatile("movq %%cr0, %0" : "=r" (val));
    return val;
}

static inline uint64_t rcr2(void)
{
    uint64_t val;
    __asm__ volatile("movq %%cr2, %0" : "=r" (val));
    return val;
}

static inline uint64_t rcr3(void)
{
    uint64_t val;
    __asm__ volatile("movq %%cr3, %0" : "=r" (val));
    return val;
}

static inline uint64_t rcr4(void)
{
    uint64_t val;
    __asm__ volatile("movq %%cr4, %0" : "=r" (val));
    return val;
}

static inline void lcr3(uint64_t val)
{
    __asm__ volatile("movq %0, %%cr3" : : "r" (val));
}

/*---------------------------------------------------------------------------
 * 中断控制
 *---------------------------------------------------------------------------*/

static inline void cli(void)
{
    __asm__ volatile("cli");
}

static inline void sti(void)
{
    __asm__ volatile("sti");
}

static inline void hlt(void)
{
    __asm__ volatile("hlt");
}

static inline uint64_t read_rflags(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r" (flags));
    return flags;
}

static inline void write_rflags(uint64_t flags)
{
    __asm__ volatile("pushq %0; popfq" : : "r" (flags));
}

/* RFLAGS 中断使能位 */
#define FL_IF    0x200

/*---------------------------------------------------------------------------
 * 原子操作
 *---------------------------------------------------------------------------*/

/* 32位原子交换 (用于 spinlock.locked) */
static inline uint32_t xchg32(volatile uint32_t *addr, uint32_t newval)
{
    uint32_t result;
    __asm__ volatile("lock; xchgl %0, %1" :
                 "+m" (*addr), "=a" (result) :
                 "1" (newval) :
                 "cc");
    return result;
}

/* 64位原子交换 */
static inline uint64_t xchg64(volatile uint64_t *addr, uint64_t newval)
{
    uint64_t result;
    __asm__ volatile("lock; xchgq %0, %1" :
                 "+m" (*addr), "=a" (result) :
                 "1" (newval) :
                 "cc");
    return result;
}

/* 32位原子比较交换 */
static inline uint32_t cmpxchg32(volatile uint32_t *addr, uint32_t expected, uint32_t newval)
{
    uint32_t result;
    __asm__ volatile("lock; cmpxchgl %2, %1" :
                 "=a" (result), "+m" (*addr) :
                 "r" (newval), "0" (expected) :
                 "cc");
    return result;
}

/*---------------------------------------------------------------------------
 * GDT / IDT 加载 (64位)
 *---------------------------------------------------------------------------*/

/* 64位伪描述符: 2字节 limit + 8字节 base */
struct desc_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static inline void lgdt64(struct desc_ptr *p)
{
    __asm__ volatile("lgdt %0" : : "m" (*p));
}

static inline void lidt64(struct desc_ptr *p)
{
    __asm__ volatile("lidt %0" : : "m" (*p));
}

static inline void ltr(uint16_t sel)
{
    __asm__ volatile("ltr %0" : : "r" (sel));
}

/*---------------------------------------------------------------------------
 * 其他辅助
 *---------------------------------------------------------------------------*/

/* 读取 MSR */
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
    return ((uint64_t)high << 32) | low;
}

/* 写入 MSR */
static inline void wrmsr(uint32_t msr, uint64_t val)
{
    uint32_t low = (uint32_t)val;
    uint32_t high = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" : : "a" (low), "d" (high), "c" (msr));
}

/* 内存屏障 */
static inline void mfence(void)
{
    __asm__ volatile("mfence" ::: "memory");
}

/* 读取时间戳计数器 */
static inline uint64_t rdtsc(void)
{
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a" (low), "=d" (high));
    return ((uint64_t)high << 32) | low;
}

#endif /* __X86_64_H__ */