/*===========================================================================
 * kernel/pipe.c — 内核管道实现
 *
 * 管道是内核分配的一块环形缓冲区，提供两个文件描述符（读端/写端）。
 * 参考 xv6 pipe.c，适配 64 位 WinixOS。
 *===========================================================================*/

#include <types.h>
#include <param.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <fs.h>
#include <buf.h>
#include <file.h>
#include <proc.h>
#include <defs.h>

#define PIPESIZE   512   /* 管道缓冲区大小（字节）*/

struct pipe {
    struct spinlock lock;
    char            data[PIPESIZE];
    uint32_t        nread;    /* 总读字节数 */
    uint32_t        nwrite;   /* 总写字节数 */
    int             readopen; /* 读端是否仍打开 */
    int             writeopen;/* 写端是否仍打开 */
};

/*---------------------------------------------------------------------------
 * pipealloc — 分配一个管道，输出读写两个文件描述符
 *             成功返回 0，失败返回 -1
 *---------------------------------------------------------------------------*/
int
pipealloc(struct file **f0, struct file **f1)
{
    struct pipe *p = 0;

    *f0 = *f1 = 0;
    if ((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
        goto bad;
    if ((p = (struct pipe *)kalloc()) == 0)
        goto bad;

    p->readopen  = 1;
    p->writeopen = 1;
    p->nwrite    = 0;
    p->nread     = 0;
    initlock(&p->lock, "pipe");

    (*f0)->type     = FD_PIPE;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pipe     = p;

    (*f1)->type     = FD_PIPE;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pipe     = p;

    return 0;

bad:
    if (p)
        kfree(p);
    if (*f0)
        fileclose(*f0);
    if (*f1)
        fileclose(*f1);
    return -1;
}

/*---------------------------------------------------------------------------
 * pipeclose — 关闭管道的读端（writable=0）或写端（writable=1）
 *             当两端都关闭时释放管道内存
 *---------------------------------------------------------------------------*/
void
pipeclose(struct pipe *p, int writable)
{
    acquire(&p->lock);
    if (writable) {
        p->writeopen = 0;
        wakeup(&p->nread);
    } else {
        p->readopen = 0;
        wakeup(&p->nwrite);
    }
    if (p->readopen == 0 && p->writeopen == 0) {
        release(&p->lock);
        kfree(p);
    } else {
        release(&p->lock);
    }
}

/*---------------------------------------------------------------------------
 * pipewrite — 向管道写 n 字节
 *---------------------------------------------------------------------------*/
int
pipewrite(struct pipe *p, char *addr, int n)
{
    acquire(&p->lock);
    for (int i = 0; i < n; i++) {
        /* 管道满时睡眠，直到读端消费或读端关闭 */
        while (p->nwrite == p->nread + PIPESIZE) {
            if (p->readopen == 0 || myproc()->killed) {
                release(&p->lock);
                return -1;
            }
            wakeup(&p->nread);
            sleep(&p->nwrite, &p->lock);
        }
        p->data[p->nwrite++ % PIPESIZE] = addr[i];
    }
    wakeup(&p->nread);
    release(&p->lock);
    return n;
}

/*---------------------------------------------------------------------------
 * piperead — 从管道读最多 n 字节
 *---------------------------------------------------------------------------*/
int
piperead(struct pipe *p, char *addr, int n)
{
    acquire(&p->lock);

    /* 管道空时睡眠，直到有数据或写端关闭 */
    while (p->nread == p->nwrite && p->writeopen) {
        if (myproc()->killed) {
            release(&p->lock);
            return -1;
        }
        sleep(&p->nread, &p->lock);
    }

    int i;
    for (i = 0; i < n; i++) {
        if (p->nread == p->nwrite)
            break;
        addr[i] = p->data[p->nread++ % PIPESIZE];
    }
    wakeup(&p->nwrite);
    release(&p->lock);
    return i;
}
