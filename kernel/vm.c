/*===========================================================================
 * kernel/vm.c — x86-64 虚拟内存管理
 *
 * 实现四级页表（PML4 → PDPT → PD → PT）的建立、内核高地址映射、
 * 用户地址空间的创建/复制/释放，以及内核↔用户数据拷贝。
 *
 * 内存布局约定：
 *   - 内核虚拟地址 = 物理地址 + KERNBASE（直接映射区）
 *   - kalloc() 返回高地址虚拟地址（已加 KERNBASE）
 *   - 页表中存放的是物理地址（通过 V2P 转换）
 *
 * 参考：
 *   - xv6-public vm.c（移植到64位四级页表）
 *   - Linux arch/x86/mm/init_64.c
 *===========================================================================*/

#include <types.h>
#include <string.h>
#include <assert.h>
#include <param.h>
#include <mmu.h>
#include <memlayout.h>
#include <kalloc.h>
#include <spinlock.h>
#include <x86_64.h>
#include <proc.h>
#include <tss.h>
#include <defs.h>

/* 全局内核页表（由 kvminit 建立，kvmswitch 加载到 CR3）*/
static pml4e_t *kpml4;

/*---------------------------------------------------------------------------
 * g_syscall_kstack — 当前进程的内核栈顶（供 syscall_entry.S 读取）
 *
 * 每次进程切换时由 uvmswitch() 更新。
 * 单核下安全；SMP 时需改为 per-CPU 变量（通过 GS 段基址访问）。
 *---------------------------------------------------------------------------*/
uint64_t g_syscall_kstack;

/*---------------------------------------------------------------------------
 * walkp4 — 遍历四级页表，返回虚拟地址 va 对应的 PT 级 PTE 指针
 *
 * 从 PML4 出发，逐级向下走：PML4 → PDPT → PD → PT。
 * 若中间某级页表不存在：
 *   - alloc=1：调用 kalloc() 分配新页并填充中间级表项
 *   - alloc=0：返回 NULL
 *
 * @pml4:  PML4 表的内核虚拟地址
 * @va:    目标虚拟地址
 * @alloc: 是否允许分配缺失的中间级页表
 *
 * 返回：PT 级对应 PTE 的内核虚拟地址指针，失败返回 NULL
 *---------------------------------------------------------------------------*/
pte_t *walkp4(pml4e_t *pml4, uint64_t va, int alloc)
{
    /* 逐级处理 PML4 → PDPT → PD，最后在 PT 返回具体 PTE 指针 */
    uint64_t *table = (uint64_t *)pml4;

    for (int level = 3; level > 0; level--) {
        /* 取当前级索引并定位表项 */
        uint64_t *entry = &table[PX(level, va)];

        if (*entry & PTE_P) {
            /* 表项存在：提取下一级页表的物理地址，转为内核虚拟地址 */
            table = (uint64_t *)P2V(PTE_ADDR(*entry));
        } else {
            /* 表项不存在 */
            if (!alloc)
                return NULL;

            /* 分配一个新的4KB页作为下一级页表，kalloc 已清零 */
            void *pg = kalloc();
            if (!pg)
                return NULL;   /* 内存不足 */

            /* 将新页的物理地址写入当前级表项，设置 P+W+U 允许所有访问
             * （权限控制由叶级 PT 表项决定）*/
            *entry = V2P(pg) | PTE_P | PTE_W | PTE_U;
            table  = (uint64_t *)pg;
        }
    }

    /* table 现在指向 PT 级页表，返回 va 对应的 PTE 指针 */
    return &table[PX(0, va)];
}

/*---------------------------------------------------------------------------
 * mappages — 在页表中建立虚拟地址范围到物理地址的映射
 *
 * 将 [va, va+size) 映射到物理地址 [pa, pa+size)，每次映射一个 4KB 页。
 * size 不必是页对齐的（会向上对齐到 PGSIZE）。
 *
 * @pml4: PML4 表的内核虚拟地址
 * @va:   起始虚拟地址（会向下对齐到页边界）
 * @pa:   起始物理地址（会向下对齐到页边界）
 * @size: 映射大小（字节）
 * @perm: 页权限（PTE_W / PTE_U / PTE_NX 等）
 *
 * 返回：0 成功，-1 失败（内存不足或地址重复映射）
 *---------------------------------------------------------------------------*/
int mappages(pml4e_t *pml4, uint64_t va, uint64_t pa, uint64_t size, uint64_t perm)
{
    uint64_t a   = PGROUNDDOWN(va);
    uint64_t end = PGROUNDDOWN(va + size - 1);

    while (1) {
        pte_t *pte = walkp4(pml4, a, 1);
        if (!pte)
            return -1;  /* 分配中间级页表失败 */

        if (*pte & PTE_P)
            panic("mappages: remap");   /* 不允许重复映射 */

        *pte = pa | perm | PTE_P;

        if (a == end)
            break;

        a  += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

/*---------------------------------------------------------------------------
 * kvminit — 建立内核页表
 *
 * 映射整个物理内存到高地址内核虚拟地址区（KERNBASE + pa）。
 * 使用 4KB 页粒度（没有用 2MB 大页，便于精确权限控制）。
 *
 * 映射范围：物理地址 0 ~ PHYSTOP 全部映射（包含设备区）
 *   [KERNBASE,          KERNBASE+PHYSTOP) → PA[0, PHYSTOP)，可读写，不可执行
 *
 * 调用时机：entry64.S 已建立临时页表并跳转到高地址后，kernel_main 最先调用。
 *
 * 返回：内核 PML4 的内核虚拟地址
 *---------------------------------------------------------------------------*/
pml4e_t *kvminit(void)
{
    /* 分配 PML4 根页表（kalloc 返回高地址，已清零）*/
    pml4e_t *pml4 = (pml4e_t *)kalloc();
    if (!pml4)
        panic("kvminit: out of memory");

    /*
     * 映射：KERNBASE + [0, PHYSTOP) → 物理地址 [0, PHYSTOP)
     * 权限：可读写（PTE_W），内核态（无 PTE_U），禁止用户执行
     *
     * 这包含了：
     *   - 内核代码和数据（KERNBASE+0x100000 起）
     *   - 所有物理内存（kalloc 分配的页都在此范围内）
     *   - 设备 MMIO 区域（0xFE000000 之后，本映射覆盖到 PHYSTOP=2GB）
     */
    if (mappages(pml4, KERNBASE, 0, PHYSTOP, PTE_W) < 0)
        panic("kvminit: mappages failed");

    kpml4 = pml4;
    return pml4;
}

/*---------------------------------------------------------------------------
 * kvmswitch — 切换到内核页表
 *
 * 将内核 PML4 的物理地址写入 CR3，完成到内核页表的切换。
 * 调用前必须已调用 kvminit()。
 *---------------------------------------------------------------------------*/
void kvmswitch(void)
{
    lcr3(V2P(kpml4));
}

/*---------------------------------------------------------------------------
 * uvmcreate — 创建空的用户页表
 *
 * 分配新的 PML4，并将内核高地址区域（KERNBASE 对应的 PML4 entry）
 * 从内核页表中复制到新页表，使用户进程也能访问内核映射。
 *
 * 返回：新 PML4 的内核虚拟地址，失败返回 NULL
 *---------------------------------------------------------------------------*/
pml4e_t *uvmcreate(void)
{
    pml4e_t *pml4 = (pml4e_t *)kalloc();
    if (!pml4)
        return NULL;

    /*
     * 将内核 PML4 中高地址部分的条目复制到用户 PML4。
     * KERNBASE 的 PML4 索引（PX(3, KERNBASE)）以及之后的所有 entry
     * 都对应内核地址空间，直接共享（不复制物理内存，只复制 PML4 entry）。
     *
     * 这样内核代码可以在用户页表下直接访问内核虚拟地址，
     * 避免了每次系统调用时切换页表。
     */
    uint64_t kidx = PX(3, KERNBASE);   /* KERNBASE 的 PML4 slot 索引 */
    for (uint64_t i = kidx; i < PTESZ; i++)
        pml4[i] = kpml4[i];

    return pml4;
}

/*---------------------------------------------------------------------------
 * uvmswitch — 切换到用户进程的页表
 *
 * 将进程的 PML4 物理地址写入 CR3。
 * 由调度器在 sched → scheduler 选出下一进程后调用。
 *
 * @p: 目标进程
 *---------------------------------------------------------------------------*/
void uvmswitch(struct proc *p)
{
    if (!p->pgdir)
        panic("uvmswitch: no pgdir");

    /* 更新 TSS.RSP0 和 syscall 内核栈变量，为下次陷入内核做准备 */
    uint64_t kstacktop = (uint64_t)p->kstack + KSTACKSIZE;
    tss_set_rsp0(kstacktop);
    g_syscall_kstack = kstacktop;

    lcr3(V2P(p->pgdir));
}

/*---------------------------------------------------------------------------
 * uvmalloc — 将用户地址空间从 oldsz 扩展到 newsz
 *
 * 为 [oldsz, newsz) 范围分配物理页并建立映射（用户态可读写）。
 * 用于 sbrk() 扩展堆空间。
 *
 * @pml4:   进程 PML4
 * @oldsz:  当前用户空间大小
 * @newsz:  目标用户空间大小
 *
 * 返回：新的空间大小（成功），0（失败）
 *---------------------------------------------------------------------------*/
uint64_t uvmalloc(pml4e_t *pml4, uint64_t oldsz, uint64_t newsz)
{
    if (newsz <= oldsz)
        return oldsz;

    /* 从下一个页边界开始分配 */
    uint64_t a = PGROUNDUP(oldsz);

    for (; a < newsz; a += PGSIZE) {
        void *mem = kalloc();
        if (!mem) {
            /* 分配失败，回滚已分配的部分 */
            uvmdealloc(pml4, oldsz, a);
            return 0;
        }
        if (mappages(pml4, a, V2P(mem), PGSIZE, PTE_W | PTE_U) < 0) {
            kfree(mem);
            uvmdealloc(pml4, oldsz, a);
            return 0;
        }
    }
    return newsz;
}

/*---------------------------------------------------------------------------
 * uvmdealloc — 将用户地址空间从 oldsz 缩减到 newsz
 *
 * 释放 [newsz, oldsz) 范围内映射的物理页（取消 PTE 映射）。
 * 用于 sbrk() 收缩堆或 exec() 替换地址空间时清理旧映射。
 *
 * @pml4:   进程 PML4
 * @oldsz:  当前用户空间大小
 * @newsz:  目标用户空间大小（更小）
 *
 * 返回：新的空间大小
 *---------------------------------------------------------------------------*/
uint64_t uvmdealloc(pml4e_t *pml4, uint64_t oldsz, uint64_t newsz)
{
    if (newsz >= oldsz)
        return oldsz;

    /* 从 newsz 对齐到页边界开始释放 */
    uint64_t a   = PGROUNDUP(newsz);
    uint64_t end = PGROUNDUP(oldsz);

    for (; a < end; a += PGSIZE) {
        pte_t *pte = walkp4(pml4, a, 0);
        if (!pte)
            /* 该页未映射，跳过 */
            a += (PTESZ - 1 - PX(0, a)) * PGSIZE;  /* 跳到下一个 PT 边界 */
        else if ((*pte & PTE_P) && (*pte & PTE_U)) {
            /* 释放物理页 */
            uint64_t pa = PTE_ADDR(*pte);
            kfree(P2V(pa));
            *pte = 0;
        }
    }
    return newsz;
}

/*---------------------------------------------------------------------------
 * uvmcopy — 复制用户地址空间（用于 fork）
 *
 * 将父进程 [0, sz) 的每个已映射物理页复制一份，
 * 在子进程 pgdir 中建立相同虚拟地址到新物理页的映射。
 *
 * @old:  父进程 PML4
 * @new:  子进程 PML4（已由 uvmcreate 建立内核映射）
 * @sz:   需要复制的大小（字节，通常是父进程的 proc->sz）
 *
 * 返回：0 成功，-1 失败
 *---------------------------------------------------------------------------*/
int uvmcopy(pml4e_t *old, pml4e_t *new, uint64_t sz)
{
    for (uint64_t i = 0; i < sz; i += PGSIZE) {
        /* 在父页表中查找该页的 PTE */
        pte_t *pte = walkp4(old, i, 0);
        if (!pte || !(*pte & PTE_P))
            panic("uvmcopy: page not present");

        uint64_t pa    = PTE_ADDR(*pte);
        uint64_t flags = PTE_FLAGS(*pte);

        /* 分配新物理页并复制内容 */
        void *mem = kalloc();
        if (!mem)
            goto bad;

        memmove(mem, P2V(pa), PGSIZE);

        /* 在子页表中建立映射 */
        if (mappages(new, i, V2P(mem), PGSIZE, flags) < 0) {
            kfree(mem);
            goto bad;
        }
    }
    return 0;

bad:
    /* 回滚：释放已复制的物理页 */
    uvmfree(new, sz);
    return -1;
}

/*---------------------------------------------------------------------------
 * uvmfree — 释放用户地址空间的所有物理页及页表结构
 *
 * 先释放 [0, sz) 内映射的所有物理页（PTE_U 用户页），
 * 再递归释放四级页表本身占用的物理页。
 *
 * @pml4: 进程 PML4（内核虚拟地址）
 * @sz:   用户地址空间大小
 *---------------------------------------------------------------------------*/
void uvmfree(pml4e_t *pml4, uint64_t sz)
{
    /* 1. 先释放用户物理页 */
    if (sz > 0)
        uvmdealloc(pml4, sz, 0);

    /* 2. 释放四级页表结构本身 */
    uint64_t kidx = PX(3, KERNBASE);   /* 内核部分从此 slot 开始，不释放 */

    /* 遍历 PML4 的用户部分（slot 0 ~ kidx-1）*/
    for (uint64_t i4 = 0; i4 < kidx; i4++) {
        if (!(pml4[i4] & PTE_P))
            continue;
        /* 获取 PDPT */
        pdpte_t *pdpt = (pdpte_t *)P2V(PTE_ADDR(pml4[i4]));
        for (uint64_t i3 = 0; i3 < PTESZ; i3++) {
            if (!(pdpt[i3] & PTE_P))
                continue;
            /* 获取 PD */
            pde_t *pd = (pde_t *)P2V(PTE_ADDR(pdpt[i3]));
            for (uint64_t i2 = 0; i2 < PTESZ; i2++) {
                if (!(pd[i2] & PTE_P))
                    continue;
                /* 释放 PT 页 */
                kfree(P2V(PTE_ADDR(pd[i2])));
                pd[i2] = 0;
            }
            /* 释放 PD 页 */
            kfree(pd);
            pdpt[i3] = 0;
        }
        /* 释放 PDPT 页 */
        kfree(pdpt);
        pml4[i4] = 0;
    }

    /* 3. 释放 PML4 本身 */
    kfree(pml4);
}

/*---------------------------------------------------------------------------
 * copyout — 从内核地址复制数据到用户地址空间
 *
 * 在指定进程的页表下，将内核缓冲区 src 的 len 字节复制到
 * 用户虚拟地址 va 处。安全检查：逐页验证目标地址已映射且用户可写。
 *
 * @pml4: 目标进程 PML4
 * @va:   目标用户虚拟地址
 * @src:  内核源缓冲区
 * @len:  字节数
 *
 * 返回：0 成功，-1 目标地址未映射或无写权限
 *---------------------------------------------------------------------------*/
int copyout(pml4e_t *pml4, uint64_t va, void *src, uint64_t len)
{
    char *s = (char *)src;

    while (len > 0) {
        /* 计算当前页内的起始偏移和可复制的字节数 */
        uint64_t pg_base = PGROUNDDOWN(va);
        uint64_t off     = va - pg_base;
        uint64_t n       = PGSIZE - off;
        if (n > len)
            n = len;

        /* 查找目标 PTE */
        pte_t *pte = walkp4(pml4, va, 0);
        if (!pte || !(*pte & PTE_P) || !(*pte & PTE_U) || !(*pte & PTE_W))
            return -1;

        /* 将数据写入目标物理页（通过内核虚拟地址访问）*/
        char *dst = (char *)P2V(PTE_ADDR(*pte)) + off;
        memmove(dst, s, (size_t)n);

        len -= n;
        va  += n;
        s   += n;
    }
    return 0;
}

/*---------------------------------------------------------------------------
 * copyin — 从用户地址空间复制数据到内核缓冲区
 *
 * 在指定进程的页表下，将用户虚拟地址 va 处的 len 字节复制到内核 dst。
 * 安全检查：逐页验证源地址已映射且用户可读。
 *
 * @pml4: 源进程 PML4
 * @va:   源用户虚拟地址
 * @dst:  内核目标缓冲区
 * @len:  字节数
 *
 * 返回：0 成功，-1 源地址未映射或无读权限
 *---------------------------------------------------------------------------*/
int copyin(pml4e_t *pml4, uint64_t va, void *dst, uint64_t len)
{
    char *d = (char *)dst;

    while (len > 0) {
        uint64_t pg_base = PGROUNDDOWN(va);
        uint64_t off     = va - pg_base;
        uint64_t n       = PGSIZE - off;
        if (n > len)
            n = len;

        pte_t *pte = walkp4(pml4, va, 0);
        if (!pte || !(*pte & PTE_P) || !(*pte & PTE_U))
            return -1;

        char *src = (char *)P2V(PTE_ADDR(*pte)) + off;
        memmove(d, src, (size_t)n);

        len -= n;
        va  += n;
        d   += n;
    }
    return 0;
}

/*---------------------------------------------------------------------------
 * copyinstr — 从用户地址空间复制一个以 '\0' 结尾的字符串到内核
 *
 * 读取用户态字符串，写入内核缓冲区 dst，最多写 max 字节（含 '\0'）。
 * 遇到 '\0' 或超出 max 时停止。
 *
 * @pml4: 源进程 PML4
 * @va:   用户态字符串起始地址
 * @dst:  内核目标缓冲区
 * @max:  缓冲区最大字节数
 *
 * 返回：0 成功（字符串完整读取），-1 地址无效，-2 字符串超出 max
 *---------------------------------------------------------------------------*/
int copyinstr(pml4e_t *pml4, uint64_t va, char *dst, uint64_t max)
{
    uint64_t got = 0;
    int      null_seen = 0;

    while (got < max) {
        uint64_t pg_base = PGROUNDDOWN(va);
        uint64_t off     = va - pg_base;
        uint64_t n       = PGSIZE - off;
        if (n > max - got)
            n = max - got;

        pte_t *pte = walkp4(pml4, va, 0);
        if (!pte || !(*pte & PTE_P) || !(*pte & PTE_U))
            return -1;

        char *src = (char *)P2V(PTE_ADDR(*pte)) + off;

        /* 逐字节复制，遇 '\0' 停止 */
        for (uint64_t i = 0; i < n; i++) {
            dst[got++] = src[i];
            if (src[i] == '\0') {
                null_seen = 1;
                goto done;
            }
        }

        va += n;
    }

done:
    if (null_seen)
        return 0;
    return -2;   /* 缓冲区满但未遇到 '\0' */
}

/*---------------------------------------------------------------------------
 * uvmclear — 清除页表项中的 PTE_U 标志，使页面对用户态不可访问
 *            用于将 guard page 标记为不可访问
 *---------------------------------------------------------------------------*/
void
uvmclear(pml4e_t *pml4, uint64_t va)
{
    pte_t *pte = walkp4(pml4, va, 0);
    if (pte == 0)
        panic("uvmclear: pte not found");
    *pte &= ~PTE_U;
}
