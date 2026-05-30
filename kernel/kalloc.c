#include <types.h>
#include <param.h>
#include <string.h>
#include <kalloc.h>
#include <spinlock.h>
#include <memlayout.h>
#include <boot_info.h>
#include <e820.h>
#include <assert.h>
#include <stdio.h>
#include <x86_64.h>

/*---------------------------------------------------------------------------
 * 物理内存分配器 — 参考 xv6 kalloc.c，适配64位
 *
 * 使用空闲链表管理物理页 (4096字节/页)
 * 从 E820 可用内存区域初始化空闲链表
 *---------------------------------------------------------------------------*/

/* 空闲页链表节点 (嵌入在空闲页的前8字节) */
struct run {
    struct run *next;
};

/* 内存分配器全局状态 */
struct {
    struct spinlock lock;
    struct run     *freelist;
    uint64_t        free_pages;  /* 空闲页计数 */
} kmem;

/*---------------------------------------------------------------------------
 * kinit — 初始化物理内存分配器
 * 遍历 E820 usable 区域，将内核结束到 PHYSTOP 之间的可用页加入 freelist
 *
 * 参考 Linux: 初始化阶段直接构建 freelist，不做 per-page memset 填充，
 * 不使用 spinlock（此时只有 BSP 单核运行，无需锁保护）。
 * kalloc() 分配时会清零页面，确保分配的内存是干净的。
 *---------------------------------------------------------------------------*/
void kinit(BootInfo *info)
{
    initlock(&kmem.lock, "kmem");
    kmem.freelist   = NULL;
    kmem.free_pages = 0;

    /* 获取 E820 内存映射 */
    E820Entry *e820 = (E820Entry *)(uintptr_t)info->e820_addr;

    /* __kernel_end 是高地址 VMA，转为物理地址再对齐 */
    uint64_t kend_pa = V2P(__kernel_end);

    for (uint32_t i = 0; i < info->e820_count; i++) {
        if (e820[i].type != E820_USABLE)
            continue;

        uint64_t start = e820[i].base;
        uint64_t end   = start + e820[i].length;

        /* 只使用内核结束之后的内存（物理地址比较）*/
        if (start < kend_pa)
            start = PGROUNDUP(kend_pa);

        /* 不超过 PHYSTOP */
        if (end > PHYSTOP)
            end = PHYSTOP;

        /* 页对齐 */
        start = PGROUNDUP(start);
        end   = PGROUNDDOWN(end);

        if (start >= end)
            continue;

        /* 以高地址虚拟地址（P2V）加入 freelist
         * kalloc() 返回高地址，vm.c 中通过 V2P() 得到物理地址 */
        for (uint64_t p = end - PGSIZE; p >= start; p -= PGSIZE) {
            struct run *r = (struct run *)P2V(p);
            r->next = kmem.freelist;
            kmem.freelist = r;
            kmem.free_pages++;
        }
    }
}

/*---------------------------------------------------------------------------
 * kfree — 释放一个物理页
 * 填充垃圾值检测悬挂引用，然后加入 freelist 头部
 *---------------------------------------------------------------------------*/
void kfree(void *v)
{
    struct run *r;

    if ((uintptr_t)v % PGSIZE != 0)
        panic("kfree: not page aligned");

    /* 填充垃圾值以捕获悬挂引用 */
    memset(v, 1, PGSIZE);

    acquire(&kmem.lock);
    r = (struct run *)v;
    r->next = kmem.freelist;
    kmem.freelist = r;
    kmem.free_pages++;
    release(&kmem.lock);
}

/*---------------------------------------------------------------------------
 * kalloc — 分配一个 4096 字节物理页
 * 从 freelist 头部取出一页，返回其地址
 * 返回 NULL 表示无可用内存
 *---------------------------------------------------------------------------*/
void *kalloc(void)
{
    struct run *r;

    acquire(&kmem.lock);
    r = kmem.freelist;
    if (r) {
        kmem.freelist = r->next;
        kmem.free_pages--;
    }
    release(&kmem.lock);

    if (r)
        memset((void *)r, 0, PGSIZE);  /* 清零分配的页 */

    return (void *)r;
}

/*---------------------------------------------------------------------------
 * kmem_free_pages — 返回当前空闲页数
 *---------------------------------------------------------------------------*/
uint64_t kmem_free_pages(void)
{
    acquire(&kmem.lock);
    uint64_t n = kmem.free_pages;
    release(&kmem.lock);
    return n;
}