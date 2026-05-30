/*===========================================================================
 * kernel/fs.c — xv6 风格 inode 文件系统（适配 64 位 WinixOS）
 *
 * 磁盘布局（mkfs 构建）：
 *   块0: 引导块（保留）
 *   块1: 超级块
 *   块2..2+nlog-1: 日志块
 *   之后: inode 块
 *   之后: 位图块
 *   之后: 数据块
 *
 * 参考：xv6-public fs.c
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

/* 超级块缓存（由 readsb / iinit 填充）*/
static struct superblock sb;

/*===========================================================================
 * inode 缓存（必须在 iinit 之前定义）
 *===========================================================================*/
static struct {
    struct spinlock lock;
    struct inode    inode[NINODE];
} icache;

/* 内部函数前向声明 */
static uint32_t balloc(uint32_t dev);
static void     bzero(uint32_t dev, uint32_t bno);
static void     bfree(uint32_t dev, uint32_t b);
static uint32_t bmap(struct inode *ip, uint32_t bn);

/*---------------------------------------------------------------------------
 * readsb — 读取磁盘超级块到 *sbp
 *---------------------------------------------------------------------------*/
void
readsb(int dev, struct superblock *sbp)
{
    struct buf *bp = bread(dev, 1);
    memmove(sbp, bp->data, sizeof(*sbp));
    brelse(bp);
}

/*---------------------------------------------------------------------------
 * iinit — 在 main() 中初始化 inode 层（每个 CPU 只需调用一次）
 *---------------------------------------------------------------------------*/
void
iinit(int dev)
{
    initlock(&icache.lock, "icache");
    for (int i = 0; i < NINODE; i++)
        initsleeplock(&icache.inode[i].lock, "inode");
    readsb(dev, &sb);
}

/*---------------------------------------------------------------------------
 * ialloc — 在设备 dev 上分配一个新 inode，类型为 type
 *---------------------------------------------------------------------------*/
struct inode *
ialloc(uint32_t dev, int16_t type)
{
    for (uint32_t inum = 1; inum < sb.ninodes; inum++) {
        struct buf    *bp  = bread(dev, IBLOCK(inum, sb));
        struct dinode *dip = (struct dinode *)bp->data + inum % IPB;

        if (dip->type == 0) {   /* 空闲 inode */
            memset(dip, 0, sizeof(*dip));
            dip->type = type;
            bwrite(bp);         /* 标记已分配 */
            brelse(bp);
            return iget(dev, inum);
        }
        brelse(bp);
    }
    panic("ialloc: no inodes");
    return 0;
}

/*---------------------------------------------------------------------------
 * iupdate — 将内存 inode 的修改写回磁盘 dinode
 *---------------------------------------------------------------------------*/
void
iupdate(struct inode *ip)
{
    struct buf    *bp  = bread(ip->dev, IBLOCK(ip->inum, sb));
    struct dinode *dip = (struct dinode *)bp->data + ip->inum % IPB;

    dip->type  = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size  = ip->size;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
    bwrite(bp);
    brelse(bp);
}

/*---------------------------------------------------------------------------
 * iget — 在 icache 中找到或创建 inode(dev, inum) 的内存副本，引用计数+1
 *        不从磁盘读取（仅在 ilock 时懒加载）
 *---------------------------------------------------------------------------*/
struct inode *
iget(uint32_t dev, uint32_t inum)
{
    acquire(&icache.lock);

    struct inode *empty = 0;

    /* 先查缓存 */
    for (struct inode *ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++) {
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            release(&icache.lock);
            return ip;
        }
        if (empty == 0 && ip->ref == 0)
            empty = ip;
    }

    /* 使用空闲槽 */
    if (empty == 0)
        panic("iget: no inodes");

    empty->dev   = dev;
    empty->inum  = inum;
    empty->ref   = 1;
    empty->valid = 0;
    release(&icache.lock);
    return empty;
}

/*---------------------------------------------------------------------------
 * idup — 对 inode 增加引用计数（用于 dup/fork）
 *---------------------------------------------------------------------------*/
struct inode *
idup(struct inode *ip)
{
    acquire(&icache.lock);
    ip->ref++;
    release(&icache.lock);
    return ip;
}

/*---------------------------------------------------------------------------
 * ilock — 锁定 inode，必要时从磁盘读取
 *---------------------------------------------------------------------------*/
void
ilock(struct inode *ip)
{
    if (ip == 0 || ip->ref < 1)
        panic("ilock");

    acquiresleep(&ip->lock);

    if (!ip->valid) {
        struct buf    *bp  = bread(ip->dev, IBLOCK(ip->inum, sb));
        struct dinode *dip = (struct dinode *)bp->data + ip->inum % IPB;

        ip->type  = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size  = dip->size;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
        brelse(bp);
        ip->valid = 1;

        if (ip->type == 0)
            panic("ilock: no type");
    }
}

/*---------------------------------------------------------------------------
 * iunlock — 解锁 inode
 *---------------------------------------------------------------------------*/
void
iunlock(struct inode *ip)
{
    if (ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
        panic("iunlock");
    releasesleep(&ip->lock);
}

/*---------------------------------------------------------------------------
 * iput — 减少引用计数；若为最后一个引用且 nlink==0 则截断并释放 inode
 *---------------------------------------------------------------------------*/
void
iput(struct inode *ip)
{
    acquiresleep(&ip->lock);

    if (ip->valid && ip->nlink == 0) {
        /* 在持有 sleeplock 的情况下检查 ref */
        acquire(&icache.lock);
        int r = ip->ref;
        release(&icache.lock);

        if (r == 1) {
            /* 最后一个引用且无目录链接：截断并释放 */
            itrunc(ip);
            ip->type  = 0;
            iupdate(ip);
            ip->valid = 0;
        }
    }

    releasesleep(&ip->lock);

    acquire(&icache.lock);
    ip->ref--;
    release(&icache.lock);
}

/*---------------------------------------------------------------------------
 * iunlockput — iunlock + iput（方便调用）
 *---------------------------------------------------------------------------*/
void
iunlockput(struct inode *ip)
{
    iunlock(ip);
    iput(ip);
}

/*===========================================================================
 * 块分配与释放（内部函数，static）
 *===========================================================================*/

/*---------------------------------------------------------------------------
 * bzero — 将设备 dev 的块 bno 清零
 *---------------------------------------------------------------------------*/
static void
bzero(uint32_t dev, uint32_t bno)
{
    struct buf *bp = bread(dev, bno);
    memset(bp->data, 0, BSIZE);
    bwrite(bp);
    brelse(bp);
}

/*---------------------------------------------------------------------------
 * balloc — 在设备 dev 上分配一个空闲数据块（位图操作）
 *---------------------------------------------------------------------------*/
static uint32_t
balloc(uint32_t dev)
{
    for (uint32_t b = 0; b < sb.size; b += BPB) {
        struct buf *bp = bread(dev, BBLOCK(b, sb));
        for (uint32_t bi = 0; bi < BPB && b + bi < sb.size; bi++) {
            int m = 1 << (bi % 8);
            if ((bp->data[bi / 8] & m) == 0) {   /* 该块空闲 */
                bp->data[bi / 8] |= m;
                bwrite(bp);
                brelse(bp);
                bzero(dev, b + bi);
                return b + bi;
            }
        }
        brelse(bp);
    }
    panic("balloc: out of blocks");
    return 0;
}

/*---------------------------------------------------------------------------
 * bfree — 释放设备 dev 上的数据块 b（清除位图）
 *---------------------------------------------------------------------------*/
static void
bfree(uint32_t dev, uint32_t b)
{
    struct buf *bp = bread(dev, BBLOCK(b, sb));
    int         bi = b % BPB;
    int         m  = 1 << (bi % 8);

    if ((bp->data[bi / 8] & m) == 0)
        panic("bfree: freeing free block");

    bp->data[bi / 8] &= ~m;
    bwrite(bp);
    brelse(bp);
}

/*===========================================================================
 * inode 内容操作
 *===========================================================================*/

/*---------------------------------------------------------------------------
 * bmap — 返回 inode ip 的第 bn 个数据块编号
 *        若该块不存在则分配（用于写操作）
 *---------------------------------------------------------------------------*/
static uint32_t
bmap(struct inode *ip, uint32_t bn)
{
    uint32_t addr;

    if (bn < NDIRECT) {
        if ((addr = ip->addrs[bn]) == 0) {
            addr = balloc(ip->dev);
            ip->addrs[bn] = addr;
        }
        return addr;
    }

    bn -= NDIRECT;

    if (bn < NINDIRECT) {
        /* 间接块 */
        if ((addr = ip->addrs[NDIRECT]) == 0) {
            addr = balloc(ip->dev);
            ip->addrs[NDIRECT] = addr;
        }
        struct buf *bp = bread(ip->dev, addr);
        uint32_t   *a  = (uint32_t *)bp->data;

        if ((addr = a[bn]) == 0) {
            addr = balloc(ip->dev);
            a[bn] = addr;
            bwrite(bp);
        }
        brelse(bp);
        return addr;
    }

    panic("bmap: out of range");
    return 0;
}

/*---------------------------------------------------------------------------
 * itrunc — 截断 inode（释放所有数据块），调用时需持有 inode 的 sleeplock
 *---------------------------------------------------------------------------*/
void
itrunc(struct inode *ip)
{
    /* 释放直接块 */
    for (int i = 0; i < NDIRECT; i++) {
        if (ip->addrs[i]) {
            bfree(ip->dev, ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }

    /* 释放间接块 */
    if (ip->addrs[NDIRECT]) {
        struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
        uint32_t   *a  = (uint32_t *)bp->data;
        for (uint32_t j = 0; j < NINDIRECT; j++) {
            if (a[j])
                bfree(ip->dev, a[j]);
        }
        brelse(bp);
        bfree(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }

    ip->size = 0;
    iupdate(ip);
}

/*---------------------------------------------------------------------------
 * stati — 将 inode 信息填入 stat 结构
 *---------------------------------------------------------------------------*/
void
stati(struct inode *ip, struct stat *st)
{
    st->dev   = ip->dev;
    st->ino   = ip->inum;
    st->type  = ip->type;
    st->nlink = ip->nlink;
    st->size  = ip->size;
}

/*---------------------------------------------------------------------------
 * readi — 从 inode ip 的偏移 off 读 n 字节到 dst
 *         若为设备文件则调用 devsw
 *---------------------------------------------------------------------------*/
int
readi(struct inode *ip, char *dst, uint32_t off, uint32_t n)
{
    if (ip->type == T_DEV) {
        if (ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
            return -1;
        return devsw[ip->major].read(ip, dst, n);
    }

    if (off > ip->size || off + n < off)
        return -1;
    if (off + n > ip->size)
        n = ip->size - off;

    uint32_t tot, m;
    for (tot = 0; tot < n; tot += m, off += m, dst += m) {
        struct buf *bp = bread(ip->dev, bmap(ip, off / BSIZE));
        m = BSIZE - off % BSIZE;
        if (m > n - tot)
            m = n - tot;
        memmove(dst, bp->data + off % BSIZE, m);
        brelse(bp);
    }
    return (int)tot;
}

/*---------------------------------------------------------------------------
 * writei — 向 inode ip 的偏移 off 写 n 字节（必要时扩展文件）
 *---------------------------------------------------------------------------*/
int
writei(struct inode *ip, char *src, uint32_t off, uint32_t n)
{
    if (ip->type == T_DEV) {
        if (ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
            return -1;
        return devsw[ip->major].write(ip, src, n);
    }

    if (off > ip->size || off + n < off)
        return -1;
    if (off + n > MAXFILE * BSIZE)
        return -1;

    uint32_t tot, m;
    for (tot = 0; tot < n; tot += m, off += m, src += m) {
        struct buf *bp = bread(ip->dev, bmap(ip, off / BSIZE));
        m = BSIZE - off % BSIZE;
        if (m > n - tot)
            m = n - tot;
        memmove(bp->data + off % BSIZE, src, m);
        bwrite(bp);
        brelse(bp);
    }

    if (n > 0 && off > ip->size) {
        ip->size = off;
        iupdate(ip);
    }
    return (int)tot;
}

/*===========================================================================
 * 目录操作
 *===========================================================================*/

/*---------------------------------------------------------------------------
 * dirlookup — 在目录 dp 中查找 name，返回内存 inode（引用计数已增）
 *             若 poff 非 NULL 则写入目录项在目录中的字节偏移
 *---------------------------------------------------------------------------*/
struct inode *
dirlookup(struct inode *dp, char *name, uint32_t *poff)
{
    if (dp->type != T_DIR)
        panic("dirlookup not DIR");

    struct dirent de;
    for (uint32_t off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char *)&de, off, sizeof(de)) != (int)sizeof(de))
            panic("dirlookup read");
        if (de.inum == 0)
            continue;
        if (strncmp(name, de.name, DIRSIZ) == 0) {
            if (poff)
                *poff = off;
            return iget(dp->dev, de.inum);
        }
    }
    return 0;
}

/*---------------------------------------------------------------------------
 * dirlink — 在目录 dp 中添加一个名为 name、指向 inum 的目录项
 *           成功返回 0，失败返回 -1
 *---------------------------------------------------------------------------*/
int
dirlink(struct inode *dp, char *name, uint32_t inum)
{
    struct inode *ip;

    /* 检查名称是否已存在 */
    if ((ip = dirlookup(dp, name, 0)) != 0) {
        iput(ip);
        return -1;
    }

    /* 查找空槽 */
    struct dirent de;
    uint32_t off;
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char *)&de, off, sizeof(de)) != (int)sizeof(de))
            panic("dirlink read");
        if (de.inum == 0)
            break;
    }

    strncpy(de.name, name, DIRSIZ);
    de.inum = inum;
    if (writei(dp, (char *)&de, off, sizeof(de)) != (int)sizeof(de))
        return -1;
    return 0;
}

/*===========================================================================
 * 路径名解析
 *===========================================================================*/

/*---------------------------------------------------------------------------
 * skipelem — 从 path 中取出下一个路径分量写入 name，返回剩余路径
 *---------------------------------------------------------------------------*/
static char *
skipelem(char *path, char *name)
{
    /* 跳过前导 '/' */
    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;

    char *s = path;
    while (*path != '/' && *path != 0)
        path++;

    int len = (int)(path - s);
    if (len >= DIRSIZ)
        memmove(name, s, DIRSIZ);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }

    /* 跳过尾部 '/' */
    while (*path == '/')
        path++;
    return path;
}

/*---------------------------------------------------------------------------
 * namex — 路径解析核心函数
 *         nameiparent=1 时返回父目录 inode，并将最后分量写入 *name
 *         nameiparent=0 时返回目标 inode
 *---------------------------------------------------------------------------*/
static struct inode *
namex(char *path, int nameiparent, char *name)
{
    struct inode *ip;

    if (*path == '/')
        ip = iget(ROOTDEV, ROOTINO);
    else
        ip = idup(myproc()->cwd);

    char *next;
    while ((next = skipelem(path, name)) != 0) {
        ilock(ip);
        if (ip->type != T_DIR) {
            iunlockput(ip);
            return 0;
        }
        if (nameiparent && *next == '\0') {
            /* 到达倒数第二个分量 */
            iunlock(ip);
            return ip;
        }
        struct inode *next_ip = dirlookup(ip, name, 0);
        iunlockput(ip);
        if (next_ip == 0)
            return 0;
        ip   = next_ip;
        path = next;
    }

    if (nameiparent) {
        iput(ip);
        return 0;
    }
    return ip;
}

/*---------------------------------------------------------------------------
 * namei — 解析 path，返回对应的内存 inode（引用计数已增）
 *---------------------------------------------------------------------------*/
struct inode *
namei(char *path)
{
    char name[DIRSIZ];
    return namex(path, 0, name);
}

/*---------------------------------------------------------------------------
 * nameiparent — 解析 path，返回父目录 inode，并将最后分量写入 *name
 *---------------------------------------------------------------------------*/
struct inode *
nameiparent(char *path, char *name)
{
    return namex(path, 1, name);
}
