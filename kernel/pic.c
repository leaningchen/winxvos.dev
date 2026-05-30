/*
 * pic.c - 8259A PIC remapping and masking
 *
 * The BIOS remaps the PIC to vectors 0x08-0x0F and 0x70-0x77,
 * which conflict with CPU exceptions 0-19. We remap to
 * 0x20-0x27 and 0x28-0x2F, then mask all IRQs since we
 * use LAPIC for interrupt delivery in 64-bit mode.
 */

#include <pic.h>
#include <x86_64.h>

/* Wait for PIC command to complete (approximately) */
static inline void pic_wait(void)
{
    /* A few NOPs / inb from port 0x80 gives the PIC time */
    inb(0x80);
    inb(0x80);
    inb(0x80);
    inb(0x80);
}

void pic_init(void)
{
    /* Start initialization sequence (ICW1) */
    outb(PIC_MASTER_CMD,  PIC_ICW1);   pic_wait();
    outb(PIC_SLAVE_CMD,   PIC_ICW1);   pic_wait();

    /* ICW2: vector bases */
    outb(PIC_MASTER_DATA, PIC_MASTER_BASE);  pic_wait();  /* 0x20 */
    outb(PIC_SLAVE_DATA,  PIC_SLAVE_BASE);   pic_wait();  /* 0x28 */

    /* ICW3: cascade wiring
     * Master: bit 2 = slave on IRQ2 (0x04)
     * Slave:  cascade identity = 2 (0x02) */
    outb(PIC_MASTER_DATA, 0x04);  pic_wait();
    outb(PIC_SLAVE_DATA,  0x02);  pic_wait();

    /* ICW4: 8086 mode */
    outb(PIC_MASTER_DATA, PIC_ICW4_MASTER);  pic_wait();
    outb(PIC_SLAVE_DATA,  PIC_ICW4_SLAVE);   pic_wait();

    /* Restore masks (actually, mask everything) */
    outb(PIC_MASTER_DATA, 0xFF);   /* mask all master IRQs */
    outb(PIC_SLAVE_DATA,  0xFF);   /* mask all slave IRQs */
}

void pic_mask(int irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC_MASTER_DATA;
    } else {
        port = PIC_SLAVE_DATA;
        irq -= 8;
    }
    value = inb(port) | (1 << irq);
    outb(port, value);
}

void pic_unmask(int irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC_MASTER_DATA;
    } else {
        port = PIC_SLAVE_DATA;
        irq -= 8;
    }
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}