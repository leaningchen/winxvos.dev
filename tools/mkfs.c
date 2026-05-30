#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
/* ---- 与内核 include/fs.h 保持一致的常量 ---- */
#define BSIZE       512
#define NDIRECT     12
#define NINDIRECT   (BSIZE / sizeof(uint32_t))
#define MAXFILE     (NDIRECT + NINDIRECT)
#define DIRSIZ      14
#define IPB         (BSIZE / sizeof(struct dinode))
#define BPB         (BSIZE * 8)
#define MAXOPBLOCKS 10
#define LOGSIZE     (MAXOPBLOCKS * 3)
#define FSSIZE      1000    /* 总块数 */
#define NINODES     200     /* inode 数 */
#define T_DIR       1
#define T_FILE      2
#define T_DEV       3

/* 超级块 */
struct superblock {
    uint32_t size;
    uint32_t nblocks;
    uint32_t ninodes;
    uint32_t nlog;
    uint32_t logstart;
    uint32_t inodestart;
    uint32_t bmapstart;
};

/* 磁盘 inode */
struct dinode {
    int16_t  type;
    int16_t  major;
    int16_t  minor;
    int16_t  nlink;
    uint32_t size;
    uint32_t addrs[NDIRECT + 1];
};

/* 目录项 */
struct dirent {
    uint16_t inum;
    char     name[DIRSIZ];
};

/* ---- 全局状态 ---- */
static int   fsfd;
static struct superblock sb;
static char  zeroes[BSIZE];

/* 下一个空闲块（从数据区开始）*/
static uint32_t freeblock;
/* 下一个空闲 inode 号 */
static uint32_t freeinode = 1;

/* ---- 工具函数 ---- */

static void
wsect(uint32_t sec, void *buf)
{
    if (lseek(fsfd, sec * BSIZE, 0) != (off_t)(sec * BSIZE)) {
        perror("lseek");
        exit(1);
    }
    if (write(fsfd, buf, BSIZE) != BSIZE) {
        perror("write");
        exit(1);
    }
}

static void
rsect(uint32_t sec, void *buf)
{
    if (lseek(fsfd, sec * BSIZE, 0) != (off_t)(sec * BSIZE)) {
        perror("lseek");
        exit(1);
    }
    if (read(fsfd, buf, BSIZE) != BSIZE) {
        perror("read");
        exit(1);
    }
}

static uint32_t
iblock(uint32_t inum)
{
    return inum / IPB + sb.inodestart;
}

static void
rinode(uint32_t inum, struct dinode *dip)
{
    char buf[BSIZE];
    rsect(iblock(inum), buf);
    *dip = ((struct dinode *)buf)[inum % IPB];
}

static void
winode(uint32_t inum, struct dinode *dip)
{
    char buf[BSIZE];
    rsect(iblock(inum), buf);
    ((struct dinode *)buf)[inum % IPB] = *dip;
    wsect(iblock(inum), buf);
}

/* 分配一个 inode，类型 type */
static uint32_t
ialloc(uint16_t type)
{
    uint32_t inum = freeinode++;
    struct dinode din;
    memset(&din, 0, sizeof(din));
    din.type  = type;
    din.nlink = 1;
    din.size  = 0;
    winode(inum, &din);
    return inum;
}

/* 分配一个空闲数据块，返回块号，并在位图中标记 */
static uint32_t
balloc(void)
{
    uint32_t b = freeblock++;
    assert(b < sb.size);

    /* 更新位图 */
    char buf[BSIZE];
    rsect(sb.bmapstart + b / BPB, buf);
    buf[(b % BPB) / 8] |= (1 << (b % 8));
    wsect(sb.bmapstart + b / BPB, buf);
    return b;
}

/* 向 inode inum 追加 n 字节数据 xp */
static void
iappend(uint32_t inum, void *xp, int n)
{
    char buf[BSIZE];
    char *p = xp;
    struct dinode din;

    rinode(inum, &din);
    uint32_t off = din.size;

    while (n > 0) {
        uint32_t fbn = off / BSIZE;   /* 文件内第几块 */
        uint32_t bn;

        if (fbn < NDIRECT) {
            if (din.addrs[fbn] == 0) {
                din.addrs[fbn] = balloc();
                /* 清零新块 */
                wsect(din.addrs[fbn], zeroes);
            }
            bn = din.addrs[fbn];
        } else {
            /* 间接块 */
            uint32_t iidx = fbn - NDIRECT;
            assert(iidx < NINDIRECT);
            if (din.addrs[NDIRECT] == 0) {
                din.addrs[NDIRECT] = balloc();
                wsect(din.addrs[NDIRECT], zeroes);
            }
            uint32_t iblock_buf[BSIZE / sizeof(uint32_t)];
            rsect(din.addrs[NDIRECT], (char *)iblock_buf);
            if (iblock_buf[iidx] == 0) {
                iblock_buf[iidx] = balloc();
                wsect(din.addrs[NDIRECT], (char *)iblock_buf);
                wsect(iblock_buf[iidx], zeroes);
            }
            bn = iblock_buf[iidx];
        }

        int x = BSIZE - off % BSIZE;
        if (x > n) x = n;

        rsect(bn, buf);
        memmove(buf + off % BSIZE, p, x);
        wsect(bn, buf);

        p   += x;
        n   -= x;
        off += x;
    }
    din.size = off;
    winode(inum, &din);
}

/* 在目录 dir_inum 中添加 (name, inum) 条目 */
static void
dirlink(uint32_t dir_inum, const char *name, uint32_t child_inum)
{
    struct dirent de;
    memset(&de, 0, sizeof(de));
    strncpy(de.name, name, DIRSIZ);
    de.inum = (uint16_t)child_inum;
    iappend(dir_inum, &de, sizeof(de));
}

/* ---- 主函数 ---- */
int
main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: mkfs fs.img [files...]\n");
        exit(1);
    }

    /* 创建 / 清空镜像文件 */
    fsfd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (fsfd < 0) {
        perror(argv[1]);
        exit(1);
    }

    /* 写零块占位 */
    for (int i = 0; i < FSSIZE; i++)
        wsect(i, zeroes);

    /* 计算布局 */
    uint32_t ninodes    = NINODES;
    uint32_t nlog       = LOGSIZE;
    uint32_t logstart   = 2;
    uint32_t inodestart = logstart + nlog;
    uint32_t inode_sects = (ninodes / IPB) + 1;
    uint32_t bmapstart  = inodestart + inode_sects;
    uint32_t bmap_sects = (FSSIZE / BPB) + 1;
    uint32_t datastart  = bmapstart + bmap_sects;

    /* 数据块数量 */
    uint32_t nblocks = FSSIZE - datastart;

    /* 填写超级块 */
    memset(&sb, 0, sizeof(sb));
    sb.size       = FSSIZE;
    sb.nblocks    = nblocks;
    sb.ninodes    = ninodes;
    sb.nlog       = nlog;
    sb.logstart   = logstart;
    sb.inodestart = inodestart;
    sb.bmapstart  = bmapstart;

    printf("mkfs: size=%u nblocks=%u ninodes=%u nlog=%u\n",
           sb.size, sb.nblocks, sb.ninodes, sb.nlog);
    printf("      logstart=%u inodestart=%u bmapstart=%u datastart=%u\n",
           sb.logstart, sb.inodestart, sb.bmapstart, datastart);

    /* 写超级块 */
    char buf[BSIZE];
    memset(buf, 0, sizeof(buf));
    memmove(buf, &sb, sizeof(sb));
    wsect(1, buf);

    /* 初始化 freeblock（从数据区开始）*/
    freeblock = datastart;

    /* 创建根目录 inode（inum=1）*/
    uint32_t rootino = ialloc(T_DIR);
    assert(rootino == 1);

    /* 根目录的 . 和 .. */
    dirlink(rootino, ".", rootino);
    dirlink(rootino, "..", rootino);

    /* 将命令行中指定的文件加入根目录 */
    for (int i = 2; i < argc; i++) {
        /* 去掉路径，只取文件名 */
        char *shortname = strrchr(argv[i], '/');
        if (shortname == NULL)
            shortname = argv[i];
        else
            shortname++;

        /* 去掉可能的 "build/" 或 "user/" 前缀 */
        char *p = shortname;
        /* 跳过如 "user_" 开头的 build 产物名（如 build/user/sh → sh）*/

        int fd = open(argv[i], O_RDONLY | O_BINARY);
        if (fd < 0) {
            fprintf(stderr, "mkfs: cannot open %s\n", argv[i]);
            exit(1);
        }

        uint32_t inum = ialloc(T_FILE);
        dirlink(rootino, p, inum);

        /* 按块读入并追加 */
        char fbuf[BSIZE];
        int n;
        while ((n = read(fd, fbuf, sizeof(fbuf))) > 0)
            iappend(inum, fbuf, n);

        close(fd);
        printf("mkfs: added %s (inum %u)\n", p, inum);
    }

    /* 更新根目录的 inode nlink（. 和 .. 引用自身，nlink=2）*/
    struct dinode din;
    rinode(rootino, &din);
    din.nlink = 2;
    winode(rootino, &din);

    /* 验证位图覆盖了已分配的块 */
    if (freeblock > FSSIZE) {
        fprintf(stderr, "mkfs: out of blocks (used %u, max %u)\n",
                freeblock, FSSIZE);
        exit(1);
    }

    close(fsfd);
    return 0;
}
