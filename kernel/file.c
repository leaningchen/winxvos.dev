/*===========================================================================
 * kernel/file.c — 文件描述符层
 *
 * 管理全局文件表 ftable，提供 filealloc/filedup/fileclose/fileread/filewrite。
 * 参考 xv6 file.c，适配 64 位 WinixOS。
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
#include <libc.h>

/* 全局文件表 */
struct {
    struct spinlock lock;
    struct file     file[NFILE];
} ftable;

/* 设备驱动函数表（主设备号索引）*/
struct devsw devsw[NDEV];

/*---------------------------------------------------------------------------
 * fileinit — 初始化文件表锁（在 main() 中调用）
 *---------------------------------------------------------------------------*/
void
fileinit(void)
{
    initlock(&ftable.lock, "ftable");
}

/*---------------------------------------------------------------------------
 * filealloc — 分配一个空文件结构，ref=1；若无空闲则返回 0
 *---------------------------------------------------------------------------*/
struct file *
filealloc(void)
{
    acquire(&ftable.lock);
    for (struct file *f = ftable.file; f < &ftable.file[NFILE]; f++) {
        if (f->ref == 0) {
            f->ref = 1;
            release(&ftable.lock);
            return f;
        }
    }
    release(&ftable.lock);
    return 0;
}

/*---------------------------------------------------------------------------
 * filedup — 增加文件引用计数
 *---------------------------------------------------------------------------*/
struct file *
filedup(struct file *f)
{
    acquire(&ftable.lock);
    if (f->ref < 1)
        panic("filedup");
    f->ref++;
    release(&ftable.lock);
    return f;
}

/*---------------------------------------------------------------------------
 * fileclose — 减少引用计数；若降为 0 则真正关闭
 *---------------------------------------------------------------------------*/
void
fileclose(struct file *f)
{
    acquire(&ftable.lock);
    if (f->ref < 1)
        panic("fileclose");
    if (--f->ref > 0) {
        release(&ftable.lock);
        return;
    }

    /* 保存副本，清零结构，再释放锁 */
    struct file ff = *f;
    f->ref  = 0;
    f->type = FD_NONE;
    release(&ftable.lock);

    if (ff.type == FD_PIPE) {
        pipeclose(ff.pipe, ff.writable);
    } else if (ff.type == FD_INODE) {
        begin_op();
        iput(ff.ip);
        end_op();
    }
}

/*---------------------------------------------------------------------------
 * filestat — 填充 stat 结构（fstat 系统调用使用）
 *---------------------------------------------------------------------------*/
int
filestat(struct file *f, struct stat *st)
{
    if (f->type == FD_INODE) {
        ilock(f->ip);
        stati(f->ip, st);
        iunlock(f->ip);
        return 0;
    }
    return -1;
}

/*---------------------------------------------------------------------------
 * fileread — 从文件 f 读 n 字节到 addr
 *---------------------------------------------------------------------------*/
int
fileread(struct file *f, char *addr, int n)
{
    if (!f->readable)
        return -1;

    if (f->type == FD_PIPE)
        return piperead(f->pipe, addr, n);

    if (f->type == FD_INODE) {
        ilock(f->ip);
        int r = readi(f->ip, addr, f->off, n);
        if (r > 0)
            f->off += r;
        iunlock(f->ip);
        return r;
    }

    panic("fileread");
    return -1;
}

/*---------------------------------------------------------------------------
 * filewrite — 将 addr 的 n 字节写入文件 f
 *---------------------------------------------------------------------------*/
int
filewrite(struct file *f, char *addr, int n)
{
    if (!f->writable)
        return -1;

    if (f->type == FD_PIPE)
        return pipewrite(f->pipe, addr, n);

    if (f->type == FD_INODE) {
        /*
         * 将写操作拆分为不超过 MAXOPBLOCKS-2 块的小批次，
         * 以防单次写操作消耗过多日志空间。
         */
        int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
        int i = 0;
        while (i < n) {
            int n1 = n - i;
            if (n1 > max)
                n1 = max;

            begin_op();
            ilock(f->ip);
            int r = writei(f->ip, addr + i, f->off, n1);
            if (r > 0)
                f->off += r;
            iunlock(f->ip);
            end_op();

            if (r < 0)
                break;
            if (r != n1)
                panic("filewrite: short write");
            i += r;
        }
        return i == n ? n : -1;
    }

    panic("filewrite");
    return -1;
}
