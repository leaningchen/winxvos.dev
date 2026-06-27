#ifndef __PIC_H__
#define __PIC_H__

/*
 * 8259A PIC (Programmable Interrupt Controller)
 *
 * The BIOS remaps the master PIC to INT 0x08-0x0F and the slave PIC
 * to INT 0x70-0x77, which conflicts with CPU exceptions 0-19.
 * We remap to INT 0x20-0x27 (master) and INT 0x28-0x2F (slave)
 * to avoid conflicts, then mask all IRQs.
 */

/* PIC I/O ports */
#define PIC_MASTER_CMD   0x20
#define PIC_MASTER_DATA  0x21
#define PIC_SLAVE_CMD    0xA0
#define PIC_SLAVE_DATA   0xA1

/* PIC ICW1: init + ICW4 needed */
#define PIC_ICW1         0x11

/* Remapped vector bases */
#define PIC_MASTER_BASE  0x20   /* IRQ0-7  -> INT 0x20-0x27 */
#define PIC_SLAVE_BASE   0x28   /* IRQ8-15 -> INT 0x28-0x2F */

/* ICW4: 8086 mode, auto-EOI (not used), buffered master/slave */
#define PIC_ICW4_MASTER  0x01
#define PIC_ICW4_SLAVE   0x01

/* Initialize the 8259A PIC: remap and mask all IRQs */
void pic_init(void);

/* Mask (disable) a specific IRQ (0-15) */
void pic_mask(int irq);

/* Unmask (enable) a specific IRQ (0-15) */
void pic_unmask(int irq);

#endif /* __PIC_H__ */