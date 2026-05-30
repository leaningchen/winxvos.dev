/*===========================================================================
 * kernel/main.c — C 语言内核主函数
 *
 * 由 entry64.S 调用，运行在高地址虚拟地址（KERNBASE + 0x100000）。
 * 参数 info 指向 BootInfo 结构，其物理地址 0x5000 通过高地址
 * KERNBASE+0x5000 传入（由 entry64.S 转换）。
 *
 * 初始化顺序：
 *   1.  kinit        — 物理内存分配器（最先，后续均依赖 kalloc）
 *   2.  kvminit      — 建立内核四级页表
 *   3.  kvmswitch    — 切换到正式内核页表（替换 entry64.S 临时页表）
 *   4.  video_init   — 初始化 VESA LFB 显示
 *   5.  pic_init     — 重映射 8259A PIC（避免与异常向量冲突）
 *   6.  idt_init     — 建立 IDT（使异常处理就绪）
 *   7.  tss_init     — 重建 GDT（添加用户段+TSS），ltr 加载 TSS
 *   8.  syscall_init — 配置 SYSCALL/SYSRET MSR
 *   9.  pinit        — 初始化进程表锁和 ticks 锁
 *   10. lapic_init   — 初始化 LAPIC 定时器
 *   11. smp_init     — 启动其他 CPU 核心
 *===========================================================================*/

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
#include <mmu.h>
#include <memlayout.h>
#include <tss.h>
#include <syscall.h>
#include <spinlock.h>
#include <fs.h>
#include "video.h"
#include "smp.h"

void kernel_main(BootInfo *info)
{
    /* 1. 校验 BootInfo magic（在任何输出之前，防止野指针）*/
    if (info->magic != BOOT_INFO_MAGIC) {
        while (1) hlt();
    }

    /* 2. 初始化物理内存分配器
     * kinit 遍历 E820 表将可用内存加入 freelist；
     * 必须在 kvminit 之前，因为 kvminit 内部调用 kalloc */
    kinit(info);

    /* 3. 建立内核四级页表并切换
     * kvminit 将 [KERNBASE, KERNBASE+PHYSTOP) → [0, PHYSTOP) 全部映射；
     * kvmswitch 写入 CR3，替换 entry64.S 建立的临时页表 */
    kvminit();
    kvmswitch();

    /* 4. 初始化视频显示 */
    video_init(info->fb_addr, info->fb_width, info->fb_height,
               info->fb_pitch, info->fb_bpp);
    video_clear(COLOR_BG);

    /* 5. Banner */
    kprintf_color(COLOR_CYAN, "  WinixOS v2.0\n");
    kprintf("----------------------------------------\n");

    /* 6. 显示分辨率 */
    kprintf_color(COLOR_WHITE, "Display  : %ux%ux32\n",
                  info->fb_width, info->fb_height);

    /* 7. 显示内存统计信息 */
    {
        E820Entry *e820 = (E820Entry *)(uintptr_t)info->e820_addr;
        uint64_t usable = 0;
        for (uint32_t i = 0; i < info->e820_count; i++)
            if (e820[i].type == E820_USABLE)
                usable += e820[i].length;
        kprintf_color(COLOR_GREEN, "Memory   : %lu MB\n",
                      usable / (1024ULL * 1024ULL));
    }
    kprintf_color(COLOR_GREEN, "Kalloc   : %lu free pages (%lu MB)\n",
                  kmem_free_pages(),
                  kmem_free_pages() * PGSIZE / (1024ULL * 1024ULL));
    kprintf_color(COLOR_GREEN, "VM       : kernel @ KERNBASE+0x100000\n");

    /* 8. 重映射并屏蔽 8259A PIC（避免与 CPU 异常向量 0-31 冲突）*/
    pic_init();
    kprintf_color(COLOR_GREEN, "PIC      : remapped (0x20-0x2F), masked\n");

    /* 9. 初始化 IDT（建立所有异常/中断门）*/
    idt_init();
    kprintf_color(COLOR_GREEN, "IDT      : initialized\n");

    /* 10. 初始化 TSS 和 GDT */
    tss_init();
    kprintf_color(COLOR_GREEN, "TSS/GDT  : initialized\n");

    /* 11. 配置 SYSCALL/SYSRET MSR（需要 GDT 已重建）*/
    syscall_init();
    kprintf_color(COLOR_GREEN, "SYSCALL  : MSR configured\n");

    /* 12. 初始化进程表锁和 ticks 计数器锁 */
    pinit();
    initlock(&tickslock, "ticks");
    kprintf_color(COLOR_GREEN, "PROC     : ptable initialized\n");

    /* 13. 初始化块缓冲区、文件表、文件系统日志和 inode 层 */
    binit();
    fileinit();
    iinit(ROOTDEV);
    initlog(ROOTDEV);
    kprintf_color(COLOR_GREEN, "FS       : bio/file/inode/log initialized\n");

    /* 11. 初始化 LAPIC（启用、配置定时器、屏蔽 LINT0/LINT1）*/
    lapic_init();

    /* 12. 显示 E820 内存映射表 */
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
            if (len >= 1024ULL * 1024ULL)
                kprintf("  %p + %lu MB  ", e820[i].base, len / (1024ULL * 1024ULL));
            else
                kprintf("  %p + %lu KB  ", e820[i].base, len / 1024ULL);
            kprintf_color(type_color, "%s\n", type_name);
        }
    }

    /* 13. SMP 初始化（启动 AP 核心）*/
    kprintf_color(COLOR_YELLOW, "\nInitializing SMP...\n");
    int cpu_count = smp_init(info);
    kprintf_color(COLOR_GREEN, "CPU Cores: %d\n", cpu_count);

    kprintf("----------------------------------------\n");
    kprintf_color(COLOR_CYAN, "System ready. Interrupts enabled.\n");

    /* 14. 图形功能演示
     * 策略：先打印所有文字，再读取光标 Y，在文字下方绘制图形 */
    kprintf_color(COLOR_YELLOW, "\nGraphics Demo:\n");
    kprintf_color(COLOR_WHITE,  "  rect  circle  round-rect  triangle  pentagon  line\n");
    kprintf_color(COLOR_GREEN,  "Graphics demo complete.\n");

    int gfx_top = video_get_cursor_y() + 16;   /* 留出 16px 间距 */
    int gfx_cy  = gfx_top + 60;                /* 圆心 Y */

    /* 蓝色填充矩形 + 白色描边 */
    video_fill_rect(50, gfx_top, 200, 120, RGB(0, 80, 200));
    video_draw_rect(50, gfx_top, 200, 120, COLOR_WHITE);

    /* 绿色填充圆形 + 白色轮廓 */
    video_fill_circle(360, gfx_cy, 60, RGB(0, 180, 80));
    video_draw_circle(360, gfx_cy, 60, COLOR_WHITE);

    /* 黄色填充圆角矩形 + 白色轮廓 */
    video_fill_round_rect(490, gfx_top, 180, 120, 20, COLOR_YELLOW);
    video_draw_round_rect(490, gfx_top, 180, 120, 20, COLOR_WHITE);

    /* 红色填充三角形 + 白色轮廓 */
    {
        int tx[] = {760, 840, 680};
        int ty[] = {gfx_top + 120, gfx_top, gfx_top};
        video_fill_polygon(tx, ty, 3, COLOR_RED);
        video_draw_polygon(tx, ty, 3, COLOR_WHITE);
    }

    /* 青色填充五边形 + 白色轮廓 */
    {
        int px[] = {960, 1010, 990, 930, 910};
        int py[] = {gfx_top + 10, gfx_top + 50, gfx_top + 110,
                    gfx_top + 110, gfx_top + 50};
        video_fill_polygon(px, py, 5, COLOR_CYAN);
        video_draw_polygon(px, py, 5, COLOR_WHITE);
    }

    /* 灰色斜线 */
    video_draw_line(30, gfx_top + 140, 1100, gfx_top + 160, COLOR_LGRAY);

    /* 15. 创建第一个用户进程（init）并启动调度器 */
    userinit();
    kprintf_color(COLOR_GREEN, "INIT     : first user process created\n");

    /* scheduler() 内部启用中断，永不返回（包含 hlt 空闲循环 + 调度）*/
    scheduler();

    /* 不应到达此处 */
    panic("kernel_main: returned from scheduler");
}
