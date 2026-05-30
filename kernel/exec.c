/*===========================================================================
 * kernel/exec.c — ELF64 可执行文件加载器
 *
 * exec(path, argv) 实现：
 *   1. 打开并读取 ELF 文件头和程序头表
 *   2. 为每个 PT_LOAD 段分配用户虚拟内存并拷贝数据
 *   3. 分配用户栈（2 页：guard page + stack page）
 *   4. 将 argv 推入栈
 *   5. 提交：替换当前进程的页表、sz、trapframe rip/rsp
 *
 * 参考：xv6-public exec.c，适配 64 位
 *===========================================================================*/

#include <types.h>
#include <param.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <fs.h>
#include <buf.h>
#include <file.h>
#include <proc.h>
#include <elf.h>
#include <mmu.h>
#include <memlayout.h>
#include <defs.h>
#include <libc.h>

/*---------------------------------------------------------------------------
 * exec — 将当前进程替换为 path 指定的 ELF 可执行文件
 *        argv 是以 NULL 结尾的字符串指针数组（内核虚拟地址）
 *        成功不返回；失败返回 -1
 *---------------------------------------------------------------------------*/
int
exec(char *path, char **argv)
{
    struct inode *ip;
    struct elfhdr elf;
    struct proghdr ph;
    pml4e_t *pgdir   = 0;
    pml4e_t *oldpgdir;
    uint64_t sz      = 0;
    uint64_t sp, stackbase;
    uint64_t ustack[3 + MAXARG + 1];   /* argv 指针 + argc + rip 占位 */

    /* 1. 打开 ELF 文件 */
    begin_op();
    if ((ip = namei(path)) == 0) {
        end_op();
        return -1;
    }
    ilock(ip);

    /* 2. 读取并校验 ELF 头 */
    if (readi(ip, (char *)&elf, 0, sizeof(elf)) != sizeof(elf))
        goto bad;
    if (elf.magic != ELF_MAGIC)
        goto bad;

    /* 3. 创建新页表 */
    if ((pgdir = uvmcreate()) == 0)
        goto bad;

    /* 4. 加载每个 PT_LOAD 段 */
    for (int i = 0, off = (int)elf.phoff;
         i < elf.phnum;
         i++, off += sizeof(ph))
    {
        if (readi(ip, (char *)&ph, off, sizeof(ph)) != sizeof(ph))
            goto bad;
        if (ph.type != PT_LOAD)
            continue;
        if (ph.memsz < ph.filesz)
            goto bad;
        if (ph.vaddr + ph.memsz < ph.vaddr)   /* 溢出检查 */
            goto bad;

        /* 扩展用户地址空间到 vaddr + memsz */
        uint64_t newsz = uvmalloc(pgdir, sz, ph.vaddr + ph.memsz);
        if (newsz == 0)
            goto bad;
        sz = newsz;

        /* vaddr 必须页对齐 */
        if (ph.vaddr % PGSIZE != 0)
            goto bad;

        /* 将段数据从文件拷贝到用户页 */
        if (loadseg(pgdir, ph.vaddr, ip, (uint32_t)ph.off, (uint32_t)ph.filesz) < 0)
            goto bad;
    }

    iunlockput(ip);
    end_op();
    ip = 0;

    /* 5. 分配用户栈（紧接在 sz 之后）
     *    布局（低→高）：guard_page | stack_page（1 页）
     *    guard page 映射但无访问权限，用于检测栈溢出 */
    sz = PGROUNDUP(sz);

    uint64_t newsz = uvmalloc(pgdir, sz, sz + 2 * PGSIZE);
    if (newsz == 0)
        goto bad;
    sz = newsz;

    /* guard page：移除用户访问权限（暂用 uvmclear 标记不可访问）*/
    uvmclear(pgdir, sz - 2 * PGSIZE);

    sp        = sz;
    stackbase = sz - PGSIZE;

    /* 6. 将 argv 字符串推入栈 */
    int argc;
    for (argc = 0; argv[argc]; argc++) {
        if (argc >= MAXARG)
            goto bad;
        int len = strlen(argv[argc]) + 1;
        sp -= len;
        sp &= ~(uint64_t)7;   /* 8 字节对齐 */
        if (sp < stackbase)
            goto bad;
        if (copyout(pgdir, sp, argv[argc], len) < 0)
            goto bad;
        ustack[argc] = sp;
    }
    ustack[argc] = 0;   /* argv[argc] = NULL */

    /* 推入 argv 数组本身（argv 指针数组）*/
    sp -= (argc + 1) * sizeof(uint64_t);
    sp &= ~(uint64_t)7;
    if (sp < stackbase)
        goto bad;
    if (copyout(pgdir, sp, ustack, (argc + 1) * sizeof(uint64_t)) < 0)
        goto bad;

    /* 7. 提交：替换进程地址空间 */
    struct proc *p = myproc();

    /* 将进程名设为可执行文件名（取最后一个分量）*/
    char *last;
    char *s;
    for (last = s = path; *s; s++)
        if (*s == '/')
            last = s + 1;
    strncpy(p->name, last, sizeof(p->name));

    /* 替换页表 */
    oldpgdir = p->pgdir;
    p->pgdir = pgdir;
    p->sz    = sz;

    /* 设置 trapframe：rip 指向 ELF 入口，rsp 指向用户栈顶 */
    p->tf->rip = elf.entry;
    p->tf->rsp = sp;

    /* 设置 rdi=argc, rsi=argv 指针（x86-64 调用约定前两个参数）*/
    p->tf->rdi = argc;
    p->tf->rsi = sp + (argc + 1) * sizeof(uint64_t);   /* 指向 argv[] in stack（已推入）*/

    /* 切换到新页表，释放旧页表 */
    uvmswitch(p);
    uvmfree(oldpgdir, 0);

    return 0;

bad:
    if (pgdir)
        uvmfree(pgdir, sz);
    if (ip) {
        iunlockput(ip);
        end_op();
    }
    return -1;
}

/*---------------------------------------------------------------------------
 * loadseg — 将 inode ip 文件偏移 offset 处的 sz 字节加载到
 *           页表 pgdir 中虚拟地址 va 开始的用户内存
 *---------------------------------------------------------------------------*/
int
loadseg(pml4e_t *pgdir, uint64_t va, struct inode *ip,
        uint32_t offset, uint32_t sz)
{
    for (uint32_t i = 0; i < sz; i += PGSIZE) {
        /* 在页表中找到 pa，直接写入（copyout 需要 walkp4）*/
        pte_t *pte = walkp4(pgdir, va + i, 0);
        if (pte == 0)
            panic("loadseg: address should exist");
        uint64_t pa = PTE_ADDR(*pte);

        uint32_t n = PGSIZE;
        if (sz - i < PGSIZE)
            n = sz - i;

        if (readi(ip, (char *)(pa + KERNBASE), offset + i, n) != (int)n)
            return -1;
    }
    return 0;
}
