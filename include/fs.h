#ifndef __FS_H__
#define __FS_H__

/*===========================================================================
 * include/fs.h — 磁盘文件系统格式定义
 *
 * 磁盘布局（由 mkfs.c 构建）：
 *   块0: 引导块（保留）
 *   块1: 超级块（superblock）
 *   块2..2+nlog-1: 日志块
 *   块2+nlog..: inode 块
 *   ..: 位图块（bitmap，跟踪空闲数据块）
 *   ..: 数据块
 *
 * 参考 xv6 fs.h，数据类型适配 64位（使用 uint32_t/uint16_t）
 *===========================================================================*/

#include <types.h>

/* 根 inode 编号（总是 1）*/
#define ROOTINO      1

/* 磁盘块大小（字节）*/
#define BSIZE        512

/* 根设备号 */
#define ROOTDEV      1

/* 日志和文件系统参数 */
#define LOGSIZE      (MAXOPBLOCKS * 3)  /* 日志块数 */
#define MAXOPBLOCKS  10                 /* 单次 FS 操作最大块数 */
#define NBUF         (MAXOPBLOCKS * 3)  /* 块缓冲区数量 */
#define FSSIZE       1000               /* 文件系统总块数 */

/*---------------------------------------------------------------------------
 * 超级块（在磁盘的块1中）
 *---------------------------------------------------------------------------*/
struct superblock {
    uint32_t size;        /* 文件系统镜像总块数 */
    uint32_t nblocks;     /* 数据块数量 */
    uint32_t ninodes;     /* inode 数量 */
    uint32_t nlog;        /* 日志块数量 */
    uint32_t logstart;    /* 第一个日志块编号 */
    uint32_t inodestart;  /* 第一个 inode 块编号 */
    uint32_t bmapstart;   /* 第一个位图块编号 */
};

/* 直接块数和间接块数 */
#define NDIRECT      12
#define NINDIRECT    (BSIZE / sizeof(uint32_t))
#define MAXFILE      (NDIRECT + NINDIRECT)

/*---------------------------------------------------------------------------
 * 磁盘 inode 结构（on-disk inode）
 *---------------------------------------------------------------------------*/
struct dinode {
    int16_t  type;                   /* 文件类型（0=空闲, T_FILE, T_DIR, T_DEV）*/
    int16_t  major;                  /* 主设备号（T_DEV 时有效）*/
    int16_t  minor;                  /* 次设备号 */
    int16_t  nlink;                  /* 链接数 */
    uint32_t size;                   /* 文件大小（字节）*/
    uint32_t addrs[NDIRECT + 1];     /* 数据块地址（最后一个为间接块）*/
};

/* 文件类型 */
#define T_DIR    1   /* 目录 */
#define T_FILE   2   /* 普通文件 */
#define T_DEV    3   /* 设备文件 */

/* 每块 inode 数 */
#define IPB      (BSIZE / sizeof(struct dinode))

/* inode i 所在的块编号 */
#define IBLOCK(i, sb)   ((i) / IPB + (sb).inodestart)

/* 每块位图位数 */
#define BPB      (BSIZE * 8)

/* 数据块 b 所在的位图块编号 */
#define BBLOCK(b, sb)   ((b) / BPB + (sb).bmapstart)

/*---------------------------------------------------------------------------
 * 目录项结构
 *---------------------------------------------------------------------------*/
#define DIRSIZ   14

struct dirent {
    uint16_t inum;          /* inode 编号（0 = 空目录项）*/
    char     name[DIRSIZ];  /* 文件名（不含终止符，若不足则补零）*/
};

/*---------------------------------------------------------------------------
 * stat 结构（fstat 系统调用返回）
 *---------------------------------------------------------------------------*/
struct stat {
    int16_t  type;    /* 文件类型 */
    int32_t  dev;     /* 文件系统设备号 */
    uint32_t ino;     /* inode 编号 */
    int16_t  nlink;   /* 链接数 */
    uint64_t size;    /* 文件大小（字节）*/
};

#endif /* __FS_H__ */
