#ifndef __FILE_H__
#define __FILE_H__

/*===========================================================================
 * include/file.h — 内存文件/inode 结构定义
 *
 * 参考 xv6 file.h，类型适配 64位
 *===========================================================================*/

#include <types.h>
#include <fs.h>
#include <sleeplock.h>

/*---------------------------------------------------------------------------
 * 内存 inode（in-memory inode）
 * 内核为每个被打开/引用的磁盘 inode 维护一个内存副本。
 *---------------------------------------------------------------------------*/
struct inode {
    uint32_t        dev;          /* 设备号 */
    uint32_t        inum;         /* inode 编号 */
    int             ref;          /* 引用计数（iget/iput）*/
    struct sleeplock lock;        /* 保护下面的字段 */
    int             valid;        /* 是否已从磁盘读取 */

    /* 从磁盘 dinode 复制来的字段 */
    int16_t         type;         /* 文件类型（T_FILE / T_DIR / T_DEV）*/
    int16_t         major;        /* 主设备号 */
    int16_t         minor;        /* 次设备号 */
    int16_t         nlink;        /* 磁盘链接数 */
    uint32_t        size;         /* 文件大小（字节）*/
    uint32_t        addrs[NDIRECT + 1]; /* 数据块地址 */
};

/*---------------------------------------------------------------------------
 * 设备驱动函数表（字符设备 read/write）
 *---------------------------------------------------------------------------*/
struct devsw {
    int (*read)(struct inode *, char *, int);
    int (*write)(struct inode *, char *, int);
};

extern struct devsw devsw[];

#define CONSOLE    1   /* 控制台设备号 */
#define NDEV       10  /* 最大设备数 */

/*---------------------------------------------------------------------------
 * 内核文件结构（每个打开的文件描述符对应一个 struct file）
 *---------------------------------------------------------------------------*/
struct file {
    enum { FD_NONE = 0, FD_PIPE, FD_INODE } type;
    int             ref;        /* 引用计数（通过 filedup/fileclose 管理）*/
    char            readable;   /* 是否可读 */
    char            writable;   /* 是否可写 */
    struct pipe    *pipe;       /* FD_PIPE: 管道指针 */
    struct inode   *ip;         /* FD_INODE: inode 指针 */
    uint32_t        off;        /* FD_INODE: 当前读写偏移 */
};

/* open 标志 */
#define O_RDONLY   0x000   /* 只读 */
#define O_WRONLY   0x001   /* 只写 */
#define O_RDWR     0x002   /* 读写 */
#define O_CREATE   0x200   /* 若不存在则创建 */
#define O_TRUNC    0x400   /* 打开时截断为 0 长度 */

#endif /* __FILE_H__ */
