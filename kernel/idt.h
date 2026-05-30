#ifndef __IDT_KERNEL_H__
#define __IDT_KERNEL_H__

#include <idt.h>

/* 内核内部 IDT 相关定义 */

/* IDT 数组 */
extern struct idt_entry idt[IDT_ENTRIES];

#endif /* __IDT_KERNEL_H__ */