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
 *---------------------------------------------------------------------------*/
void kinit(BootInfo *info)
{
    initlock(&kmem.lock, "kmem");
    kmem.freelist   = NULL;
    kmem.free_pages = 0;

    /* 获取 E820 内存映射 */
    E820Entry *e820 = (E820Entry *)(uintptr_t)info->e820_addr;
    char *kernel_end = (char *)__kernel_end;

    for (uint32_t i = 0; i < info->e820_count; i++) {
        if (e820[i].type != E820_USABLE)
            continue;

        uint64_t start = e820[i].base;
        uint64_t end   = start + e820[i].length;

        /* 只使用内核结束之后的内存 */
        if (start < (uint64_t)kernel_end)
            start = (uint64_t)PGROUNDUP(kernel_end);

        /* 不超过 PHYSTOP */
        if (end > PHYSTOP)
            end = PHYSTOP;

        /* 页对齐 */
        start = PGROUNDUP(start);
        end   = PGROUNDDOWN(end);

        if (start >= end)
            continue;

        /* 将该区域的每一页加入 freelist */
        for (uint64_t p = start; p < end; p += PGSIZE)
            kfree((void *)(uintptr_t)p);
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