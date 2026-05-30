/*===========================================================================
 * kernel/log.c — 文件系统日志层（Write-Ahead Logging）
 *
 * 保证文件系统操作的原子性：即使在写入过程中掉电，
 * 重启后通过重放日志（redo log）恢复一致状态。
 *
 * 使用方式：
 *   begin_op()   — 文件系统操作开始时调用
 *   log_write(b) — 替代 bwrite()，将修改记录到日志
 *   end_op()     — 操作结束时调用，最后一个 end_op 触发提交
 *
 * 磁盘日志布局：
 *   [日志头块] [数据块0] [数据块1] ... [数据块n-1]
 *   日志头块记录本次提交的块数和各块的目标块号。
 *
 * 参考 xv6 log.c
 *===========================================================================*/

#include <types.h>
#include <defs.h>
#include <param.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <fs.h>
#include <buf.h>
#include <libc.h>

/*---------------------------------------------------------------------------
 * 日志头（保存在日志头块中，也在内存中维护一份）
 *---------------------------------------------------------------------------*/
struct logheader {
    int n;               /* 本次事务的日志块数 */
    int block[LOGSIZE];  /* 各日志块对应的目标磁盘块号 */
};

/*---------------------------------------------------------------------------
 * 全局日志状态
 *---------------------------------------------------------------------------*/
static struct {
    struct spinlock lock;
    int             start;        /* 日志起始块号（从 superblock 读取）*/
    int             size;         /* 日志总块数 */
    int             outstanding;  /* 当前正在执行的 FS 系统调用数 */
    int             committing;   /* 是否正在提交（begin_op 需等待）*/
    int             dev;          /* 文件系统设备号 */
    struct logheader lh;          /* 内存中的日志头 */
} log;

/*---------------------------------------------------------------------------
 * 前向声明
 *---------------------------------------------------------------------------*/
static void recover_from_log(void);
static void commit(void);

/*---------------------------------------------------------------------------
 * initlog — 从超级块读取日志配置并执行崩溃恢复
 *---------------------------------------------------------------------------*/
void
initlog(int dev)
{
    if (sizeof(struct logheader) >= BSIZE)
        panic("initlog: logheader too large");

    struct superblock sb;
    initlock(&log.lock, "log");
    readsb(dev, &sb);
    log.start = (int)sb.logstart;
    log.size  = (int)sb.nlog;
    log.dev   = dev;
    recover_from_log();
}

/*---------------------------------------------------------------------------
 * install_trans — 将日志块内容写入目标磁盘块（重放）
 *---------------------------------------------------------------------------*/
static void
install_trans(void)
{
    for (int tail = 0; tail < log.lh.n; tail++) {
        struct buf *lbuf = bread(log.dev, log.start + tail + 1);  /* 读日志块 */
        struct buf *dbuf = bread(log.dev, log.lh.block[tail]);    /* 读目标块 */
        memmove(dbuf->data, lbuf->data, BSIZE);
        bwrite(dbuf);
        brelse(lbuf);
        brelse(dbuf);
    }
}

/*---------------------------------------------------------------------------
 * read_head — 从磁盘读取日志头到内存
 *---------------------------------------------------------------------------*/
static void
read_head(void)
{
    struct buf       *buf = bread(log.dev, log.start);
    struct logheader *lh  = (struct logheader *)buf->data;
    log.lh.n = lh->n;
    for (int i = 0; i < log.lh.n; i++)
        log.lh.block[i] = lh->block[i];
    brelse(buf);
}

/*---------------------------------------------------------------------------
 * write_head — 将内存日志头写入磁盘（真正的提交点）
 *---------------------------------------------------------------------------*/
static void
write_head(void)
{
    struct buf       *buf = bread(log.dev, log.start);
    struct logheader *hb  = (struct logheader *)buf->data;
    hb->n = log.lh.n;
    for (int i = 0; i < log.lh.n; i++)
        hb->block[i] = log.lh.block[i];
    bwrite(buf);
    brelse(buf);
}

/*---------------------------------------------------------------------------
 * recover_from_log — 系统启动时检查并重放未完成的事务
 *---------------------------------------------------------------------------*/
static void
recover_from_log(void)
{
    read_head();
    install_trans();  /* 若上次已提交则重放，否则 lh.n==0 跳过 */
    log.lh.n = 0;
    write_head();     /* 清除日志头 */
}

/*---------------------------------------------------------------------------
 * begin_op — 文件系统操作开始（允许并发）
 *
 * 若日志空间不足则等待（当前事务提交后有空间才继续）。
 *---------------------------------------------------------------------------*/
void
begin_op(void)
{
    acquire(&log.lock);
    for (;;) {
        if (log.committing) {
            /* 正在提交，等待 */
            sleep(&log, &log.lock);
        } else if (log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGSIZE) {
            /* 日志空间可能不足，等待提交后释放 */
            sleep(&log, &log.lock);
        } else {
            log.outstanding++;
            release(&log.lock);
            break;
        }
    }
}

/*---------------------------------------------------------------------------
 * end_op — 文件系统操作结束
 *
 * 最后一个 end_op 触发提交；提交后唤醒等待日志空间的操作。
 *---------------------------------------------------------------------------*/
void
end_op(void)
{
    int do_commit = 0;

    acquire(&log.lock);
    log.outstanding--;
    if (log.committing)
        panic("log.committing");
    if (log.outstanding == 0) {
        do_commit       = 1;
        log.committing  = 1;
    } else {
        /* 减少了 outstanding，可能释放了日志空间 */
        wakeup(&log);
    }
    release(&log.lock);

    if (do_commit) {
        commit();
        acquire(&log.lock);
        log.committing = 0;
        wakeup(&log);
        release(&log.lock);
    }
}

/*---------------------------------------------------------------------------
 * write_log — 将缓存中被修改的块写入日志区域
 *---------------------------------------------------------------------------*/
static void
write_log(void)
{
    for (int tail = 0; tail < log.lh.n; tail++) {
        struct buf *to   = bread(log.dev, log.start + tail + 1);  /* 日志块 */
        struct buf *from = bread(log.dev, log.lh.block[tail]);    /* 缓存块 */
        memmove(to->data, from->data, BSIZE);
        bwrite(to);
        brelse(from);
        brelse(to);
    }
}

/*---------------------------------------------------------------------------
 * commit — 执行一次完整提交
 *---------------------------------------------------------------------------*/
static void
commit(void)
{
    if (log.lh.n > 0) {
        write_log();     /* 将修改块写入日志区 */
        write_head();    /* 写日志头 — 真正的提交点 */
        install_trans(); /* 将日志块写回目标位置 */
        log.lh.n = 0;
        write_head();    /* 清除日志（n=0）*/
    }
}

/*---------------------------------------------------------------------------
 * log_write — 记录对缓冲区的写操作到日志（替代 bwrite）
 *
 * 调用者持有 b->lock 且已修改 b->data，调用此函数替代 bwrite。
 * 实际磁盘写入在 end_op/commit 时进行（日志吸收优化：同一块只记录一次）。
 *---------------------------------------------------------------------------*/
void
log_write(struct buf *b)
{
    if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
        panic("log_write: transaction too large");
    if (log.outstanding < 1)
        panic("log_write: outside transaction");

    acquire(&log.lock);
    int i;
    for (i = 0; i < log.lh.n; i++) {
        if (log.lh.block[i] == (int)b->blockno)  /* 日志吸收：已记录则更新 */
            break;
    }
    log.lh.block[i] = (int)b->blockno;
    if (i == log.lh.n)
        log.lh.n++;
    b->flags |= B_DIRTY;  /* 防止缓冲区被淘汰（防止 bget 回收它）*/
    release(&log.lock);
}
