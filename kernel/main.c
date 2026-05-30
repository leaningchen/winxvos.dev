#include <types.h>
#include <boot_info.h>
#include <e820.h>
#include <libc.h>
#include <kalloc.h>
#include <defs.h>
#include <param.h>
#include <x86_64.h>
#include <pic.h>
#include <lapic.h>
#include "video.h"
#include "smp.h"

/*===========================================================================
 * kernel_main — C 语言内核主函数
 * 由 entry64.S 调用，参数 info 指向物理地址 0x5000 的 BootInfo
 *===========================================================================*/
void kernel_main(BootInfo *info)
{
    /* 1. 校验 BootInfo magic */
    if (info->magic != BOOT_INFO_MAGIC) {
        while (1) hlt();
    }

    /* 2. 初始化视频 */
    video_init(info->fb_addr, info->fb_width, info->fb_height,
               info->fb_pitch, info->fb_bpp);
    video_clear(COLOR_BG);

    /* 3. Banner */
    kprintf_color(COLOR_CYAN, "  WinixOS v2.0\n");
    kprintf("----------------------------------------\n");

    /* 4. 显示分辨率 */
    kprintf_color(COLOR_WHITE, "Display  : %ux%ux32\n",
                  info->fb_width, info->fb_height);

    /* 5. 统计并显示内存 */
    {
        E820Entry *e820 = (E820Entry *)(uintptr_t)info->e820_addr;
        uint64_t usable = 0;
        for (uint32_t i = 0; i < info->e820_count; i++)
            if (e820[i].type == E820_USABLE)
                usable += e820[i].length;
        kprintf_color(COLOR_GREEN, "Memory   : %lu MB\n",
                      usable / (1024ULL * 1024ULL));
    }

    /* 6. 重映射并屏蔽 8259A PIC（避免与 CPU 异常向量冲突）*/
    pic_init();
    kprintf_color(COLOR_GREEN, "PIC      : remapped (0x20-0x2F), all IRQs masked\n");

    /* 7. 初始化 IDT（在 kinit 之前，确保异常有 handler）*/
    idt_init();
    kprintf_color(COLOR_GREEN, "IDT      : initialized\n");

    /* 8. 初始化 LAPIC（启用、配置定时器、屏蔽 LINT0/LINT1）*/
    lapic_init();

    /* 9. 初始化物理内存分配器 */
    kinit(info);
    kprintf_color(COLOR_GREEN, "Kalloc   : %lu free pages (%lu MB)\n",
                  kmem_free_pages(), kmem_free_pages() * PGSIZE / (1024ULL * 1024ULL));

    /* 10. 显示 E820 内存映射表 */
    kprintf_color(COLOR_YELLOW, "\nE820 Memory Map:\n");
    {
        E820Entry *e820 = (E820Entry *)(uintptr_t)info->e820_addr;
        for (uint32_t i = 0; i < info->e820_count; i++) {
            const char *type_name;
            uint32_t type_color;
            switch (e820[i].type) {
            case E820_USABLE:   type_name = "Usable";           type_color = COLOR_GREEN;  break;
            case E820_RESERVED: type_name = "Reserved";         type_color = COLOR_GRAY;   break;
            case E820_ACPI_RCL: type_name = "ACPI Reclaimable"; type_color = COLOR_LGRAY;  break;
            case E820_ACPI_NVS: type_name = "ACPI NVS";        type_color = COLOR_GRAY;   break;
            case E820_BAD:      type_name = "Bad Memory";       type_color = COLOR_RED;    break;
            default:            type_name = "Unknown";          type_color = COLOR_GRAY;   break;
            }

            uint64_t len = e820[i].length;
            if (len >= 1024ULL * 1024ULL) {
                kprintf("  %p + %lu MB  ", e820[i].base, len / (1024ULL * 1024ULL));
            } else {
                kprintf("  %p + %lu KB  ", e820[i].base, len / 1024ULL);
            }
            kprintf_color(type_color, "%s\n", type_name);
        }
    }

    /* 9. SMP 初始化 */
    kprintf_color(COLOR_YELLOW, "\nInitializing SMP...\n");
    int cpu_count = smp_init(info);
    kprintf_color(COLOR_GREEN, "CPU Cores: %d\n", cpu_count);

    kprintf("----------------------------------------\n");
    kprintf_color(COLOR_CYAN, "System ready. Interrupts enabled.\n");

    /* 启用中断 */
    sti();

    /* 11. 永久等待中断 */
    while (1) {
        hlt();
    }
}