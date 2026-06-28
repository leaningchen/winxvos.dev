#include <types.h>
#include <bootinfo.h>
#include <arch/x86_64/e820.h>
#include <libc.h>
#include <arch/x86_64/kalloc.h>
#include <defs.h>
#include <param.h>
#include <arch/x86_64/x86_64.h>
#include <arch/x86_64/pic.h>
#include <arch/x86_64/lapic.h>
#include <defs.h>
#include <message.h>
#include <drivers/video/video.h>
#include <arch/x86_64/smp.h>

/*===========================================================================
 * kernel_main — C 语言内核主函数
 * 由 entry64.S 调用，参数 info 指向物理地址 0x5000 的 zenith_boot_info
 *===========================================================================*/
void kernel_main(zenith_boot_info *info)
{
    /* 1. 校验 zenith_boot_info magic */
    if (info->magic != BOOT_INFO_MAGIC) {
        while (1) hlt();
    }

    /* 2. 初始化视频 */
    video_init(info->fb_addr, info->fb_width, info->fb_height,
               info->fb_pitch, info->fb_bpp);
    video_clear(COLOR_BG);

    /* 3. Banner */
    kprintf_color(COLOR_CYAN, "  WinixOS v2.0\n");
    show_banner_message(default_banner_text_o);
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

            void* e820_start_address = 0;
            uint64_t len = e820[i].length;
            if (len >= 1024ULL * 1024ULL) {
                kprintf("  %p, LENGTH = %p  ", e820[i].base, e820_start_address + len);
            } else {
                kprintf("  %p, LENGTH = %p  ", e820[i].base, e820_start_address + len);
            }
            kprintf_color(type_color, "%s\n", type_name);
        }
    }

    ACPI_RSDP* _ACPI_RSDP = (ACPI_RSDP*)(info->acpi_rsdp_addr);
    kprintf_color(COLOR_YELLOW, "\nRSDP SIGN == %s\n", _ACPI_RSDP->signature);
    kprintf_color(COLOR_YELLOW, "\nRSDP SIGN == %d\n", _ACPI_RSDP->revision);

    /* 9. SMP 初始化 */
    kprintf_color(COLOR_YELLOW, "\nInitializing SMP...\n");
    int cpu_count = smp_init(info);
    kprintf_color(COLOR_GREEN, "CPU Cores: %d\n", cpu_count);

    kprintf("----------------------------------------\n");
    kprintf_color(COLOR_CYAN, "System ready. Interrupts enabled.\n");

    /* 12. 图形功能演示（展示 VESA 图形绘制 API）*/
    /*
     * 策略：先把所有文字一次性打印完，再绘制图形。
     * 文字打印结束后调用 video_get_cursor_y() 获取光标像素 Y，
     * 图形区域从该位置下方开始，彻底避免文字与图形重叠。
     */
    kprintf_color(COLOR_YELLOW, "\nGraphics Demo:\n");
    kprintf_color(COLOR_WHITE,  "  rect  circle  round-rect  triangle  pentagon  line\n");
    kprintf_color(COLOR_GREEN,  "Graphics demo complete.\n");

    /* 所有文字已打印完毕，此刻读取光标 Y 像素坐标 */
    int gfx_top = video_get_cursor_y() + 16; /* 额外留 16px 间距 */
    int gfx_cy  = gfx_top + 60;             /* 圆心/图形中线 Y */

    /* --- 蓝色填充矩形 + 白色描边 --- */
    video_fill_rect(50, gfx_top, 200, 120, RGB(0, 80, 200));
    video_draw_rect(50, gfx_top, 200, 120, COLOR_WHITE);

    /* --- 绿色填充圆形 + 白色轮廓 --- */
    video_fill_circle(360, gfx_cy, 60, RGB(0, 180, 80));
    video_draw_circle(360, gfx_cy, 60, COLOR_WHITE);

    /* --- 黄色填充圆角矩形 + 白色轮廓 --- */
    video_fill_round_rect(490, gfx_top, 180, 120, 20, COLOR_YELLOW);
    video_draw_round_rect(490, gfx_top, 180, 120, 20, COLOR_WHITE);

    /* --- 红色填充三角形 + 白色轮廓（顶点向下）--- */
    {
        int tx[] = {760,             840,       680      };
        int ty[] = {gfx_top + 120,   gfx_top,   gfx_top  };
        video_fill_polygon(tx, ty, 3, COLOR_RED);
        video_draw_polygon(tx, ty, 3, COLOR_WHITE);
    }

    /* --- 青色填充五边形 + 白色轮廓 --- */
    {
        int px[] = {960,          1010,            990,             930,             910          };
        int py[] = {gfx_top + 10, gfx_top + 50,    gfx_top + 110,   gfx_top + 110,   gfx_top + 50 };
        video_fill_polygon(px, py, 5, COLOR_CYAN);
        video_draw_polygon(px, py, 5, COLOR_WHITE);
    }

    /* --- 灰色斜线，贯穿图形区域下方 --- */
    video_draw_line(30, gfx_top + 140, 1100, gfx_top + 160, COLOR_LGRAY);

    /* 启用中断 */
    sti();

    /* 13. 永久等待中断 */
    while (1) {
        hlt();
    }
}