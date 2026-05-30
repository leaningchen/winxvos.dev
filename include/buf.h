#ifndef __BUF_H__
#define __BUF_H__

/*===========================================================================
 * include/buf.h — 块缓冲区结构定义
 *
 * 块缓冲区（buffer cache）是磁盘块在内存中的缓存，
 * 由 bio.c 管理，通过 LRU 链表实现缓存淘汰。
 *
 * 参考 xv6 buf.h
 *===========================================================================*/

#include <types.h>
#include <fs.h>
#include <sleeplock.h>

struct buf {
    int           flags;        /* B_VALID | B_DIRTY */
    uint32_t      dev;          /* 设备号 */
    uint32_t      blockno;      /* 块编号 */
    struct sleeplock lock;      /* 保护 data 及以下字段 */
    uint32_t      refcnt;       /* 引用计数 */
    struct buf   *prev;         /* LRU 链表前向指针 */
    struct buf   *next;         /* LRU 链表后向指针 */
    struct buf   *qnext;        /* 磁盘请求队列指针 */
    uint8_t       data[BSIZE];  /* 块数据（512字节）*/
};

/* flags 位定义 */
#define B_VALID   0x2   /* 缓冲区数据已从磁盘读取（有效）*/
#define B_DIRTY   0x4   /* 缓冲区数据已修改，需要写回磁盘 */

#endif /* __BUF_H__ */
