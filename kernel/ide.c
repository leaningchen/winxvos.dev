/*===========================================================================
 * kernel/ide.c — ATA PIO 磁盘驱动
 *
 * 使用 PIO（Programmed I/O）方式访问第一个 IDE 硬盘（Primary Bus）。
 * 支持中断驱动的异步读写队列。
 *
 * I/O 端口（Primary Bus）：
 *   0x1F0: 数据端口（16位）
 *   0x1F2: 扇区计数
 *   0x1F3-1F5: LBA bits 0-23
 *   0x1F6: 磁盘/磁头选择
 *   0x1F7: 命令/状态端口
 *   0x3F6: 控制端口（写入 0 = 使能中断）
 *
 * 参考 xv6 ide.c，适配64位和 WinixOS 中断框架
 *===========================================================================*/

#include <types.h>
#include <defs.h>
#include <param.h>
#include <x86_64.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <fs.h>
#include <buf.h>

/*---------------------------------------------------------------------------
 * IDE 寄存器和命令常量
 *---------------------------------------------------------------------------*/
#define IDE_DATA        0x1F0   /* 数据端口（16位）*/
#define IDE_NSECT       0x1F2   /* 扇区计数 */
#define IDE_LBAL        0x1F3   /* LBA bits [7:0] */
#define IDE_LBAM        0x1F4   /* LBA bits [15:8] */
#define IDE_LBAH        0x1F5   /* LBA bits [23:16] */
#define IDE_SELECT      0x1F6   /* 驱动器/磁头选择 */
#define IDE_STATUS      0x1F7   /* 状态（读）/ 命令（写）*/
#define IDE_CTRL        0x3F6   /* 控制端口 */

#define IDE_BSY         0x80    /* 磁盘忙 */
#define IDE_DRDY        0x40    /* 磁盘就绪 */
#define IDE_DF          0x20    /* 驱动器故障 */
#define IDE_ERR         0x01    /* 错误 */

#define IDE_CMD_READ    0x20    /* 读扇区（带重试）*/
#define IDE_CMD_WRITE   0x30    /* 写扇区（带重试）*/
#define IDE_CMD_RDMUL   0xC4    /* 读多扇区 */
#define IDE_CMD_WRMUL   0xC5    /* 写多扇区 */

#define SECTOR_SIZE     512

/*---------------------------------------------------------------------------
 * 全局状态
 *---------------------------------------------------------------------------*/
static struct spinlock idelock;
static struct buf     *idequeue;    /* 当前磁盘请求队列头 */
static int             havedisk1;   /* 第二个磁盘是否存在 */

static void idestart(struct buf *b);

/*---------------------------------------------------------------------------
 * idewait — 等待 IDE 磁盘就绪
 * checkerr=1 时检查错误标志，出错返回 -1
 *---------------------------------------------------------------------------*/
static int
idewait(int checkerr)
{
    int r;
    while (((r = inb(IDE_STATUS)) & (IDE_BSY | IDE_DRDY)) != IDE_DRDY)
        ;   /* 轮询等待 */
    if (checkerr && (r & (IDE_DF | IDE_ERR)) != 0)
        return -1;
    return 0;
}

/*---------------------------------------------------------------------------
 * ideinit — 初始化 IDE 控制器
 *---------------------------------------------------------------------------*/
void
ideinit(void)
{
    initlock(&idelock, "ide");
    idewait(0);

    /* 探测第二个磁盘（disk 1）是否存在 */
    outb(IDE_SELECT, 0xE0 | (1 << 4));
    for (int i = 0; i < 1000; i++) {
        if (inb(IDE_STATUS) != 0) {
            havedisk1 = 1;
            break;
        }
    }

    /* 切回 disk 0 */
    outb(IDE_SELECT, 0xE0 | (0 << 4));
}

/*---------------------------------------------------------------------------
 * idestart — 向 IDE 控制器提交一个磁盘请求（内部函数，需持有 idelock）
 *---------------------------------------------------------------------------*/
static void
idestart(struct buf *b)
{
    if (b == 0)
        panic("idestart: null buf");
    if (b->blockno >= FSSIZE)
        panic("idestart: blockno out of range");

    int sector_per_block = BSIZE / SECTOR_SIZE;
    int sector           = (int)(b->blockno) * sector_per_block;
    int read_cmd  = (sector_per_block == 1) ? IDE_CMD_READ  : IDE_CMD_RDMUL;
    int write_cmd = (sector_per_block == 1) ? IDE_CMD_WRITE : IDE_CMD_WRMUL;

    if (sector_per_block > 7)
        panic("idestart: sector_per_block too large");

    idewait(0);
    outb(IDE_CTRL, 0);                               /* 使能中断 */
    outb(IDE_NSECT, (uint8_t)sector_per_block);      /* 扇区数 */
    outb(IDE_LBAL,  (uint8_t)(sector & 0xFF));
    outb(IDE_LBAM,  (uint8_t)((sector >> 8) & 0xFF));
    outb(IDE_LBAH,  (uint8_t)((sector >> 16) & 0xFF));
    outb(IDE_SELECT, (uint8_t)(0xE0 | ((b->dev & 1) << 4) | ((sector >> 24) & 0x0F)));

    if (b->flags & B_DIRTY) {
        outb(IDE_STATUS, (uint8_t)write_cmd);
        outsl(IDE_DATA, b->data, BSIZE / 4);         /* 写数据 */
    } else {
        outb(IDE_STATUS, (uint8_t)read_cmd);          /* 发读命令（数据由中断读取）*/
    }
}

/*---------------------------------------------------------------------------
 * ideintr — IDE 中断处理函数（由 trap_handler 在 IRQ_IDE 时调用）
 *---------------------------------------------------------------------------*/
void
ideintr(void)
{
    struct buf *b;

    acquire(&idelock);

    if ((b = idequeue) == 0) {
        release(&idelock);
        return;
    }
    idequeue = b->qnext;

    /* 读请求：中断触发后从数据端口读取数据 */
    if (!(b->flags & B_DIRTY) && idewait(1) >= 0)
        insl(IDE_DATA, b->data, BSIZE / 4);

    /* 标记缓冲区有效 */
    b->flags |= B_VALID;
    b->flags &= ~B_DIRTY;
    wakeup(b);   /* 唤醒等待此缓冲区的进程 */

    /* 启动队列中下一个请求 */
    if (idequeue != 0)
        idestart(idequeue);

    release(&idelock);
}

/*---------------------------------------------------------------------------
 * iderw — 同步读写磁盘块
 *
 * 若 B_DIRTY 置位：将缓冲区写入磁盘，清除 B_DIRTY，置位 B_VALID。
 * 若 B_VALID 未置位：从磁盘读取块，置位 B_VALID。
 *
 * 调用者必须持有 b->lock（sleeplock）。
 *---------------------------------------------------------------------------*/
void
iderw(struct buf *b)
{
    struct buf **pp;

    if (!holdingsleep(&b->lock))
        panic("iderw: buf not locked");
    if ((b->flags & (B_VALID | B_DIRTY)) == B_VALID)
        panic("iderw: nothing to do");
    if (b->dev != 0 && !havedisk1)
        panic("iderw: disk 1 not present");

    acquire(&idelock);

    /* 将请求追加到队列末尾 */
    b->qnext = 0;
    for (pp = &idequeue; *pp; pp = &(*pp)->qnext)
        ;
    *pp = b;

    /* 若队列中只有这一个请求，立即启动 */
    if (idequeue == b)
        idestart(b);

    /* 睡眠等待中断处理完成 */
    while ((b->flags & (B_VALID | B_DIRTY)) != B_VALID)
        sleep(b, &idelock);

    release(&idelock);
}
