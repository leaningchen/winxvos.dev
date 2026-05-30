#include <types.h>
#include <boot_info.h>
#include <e820.h>
#include "video.h"
#include "util.h"
#include "smp.h"

/* 打印分隔线 */
static void print_line(void)
{
    video_print("----------------------------------------\n", COLOR_GRAY);
}

/* 打印键值对：key: value */
static void print_kv(const char *key, const char *val, uint32_t vc)
{
    video_print(key,  COLOR_LGRAY);
    video_print(val,  vc);
    video_print("\n", COLOR_WHITE);
}

/* 格式化内存大小 (字节) 为 "XXXX MB" 字符串 */
static void fmt_mem_mb(uint64_t bytes, char *buf)
{
    uint64_t mb = bytes / (1024ULL * 1024ULL);
    uitoa(mb, buf, 10);
    size_t len = kstrlen(buf);
    buf[len++] = ' '; buf[len++] = 'M'; buf[len++] = 'B'; buf[len] = '\0';
}

/* E820 类型名称 */
static const char *e820_type_name(uint32_t type)
{
    switch (type) {
    case 1:  return "Usable";
    case 2:  return "Reserved";
    case 3:  return "ACPI Reclaimable";
    case 4:  return "ACPI NVS";
    case 5:  return "Bad Memory";
    default: return "Unknown";
    }
}

/*===========================================================================
 * kernel_main — C 语言内核主函数
 * 由 entry64.S 调用，参数 info 指向物理地址 0x5000 的 BootInfo
 *===========================================================================*/
void kernel_main(BootInfo *info)
{
    /* 1. 校验 BootInfo magic */
    if (info->magic != BOOT_INFO_MAGIC) {
        /* 无法初始化视频，只能挂起 */
        while (1) __asm__ volatile ("hlt");
    }

    /* 2. 初始化视频 */
    video_init(info->fb_addr, info->fb_width, info->fb_height,
               info->fb_pitch, info->fb_bpp);
    video_clear(COLOR_BG);

    /* 3. Banner */
    video_print("  WinixOS Bootloader  v1.0\n", COLOR_CYAN);
    print_line();

    /* 4. 显示分辨率 */
    {
        char wbuf[8], hbuf[8], buf[32];
        uitoa(info->fb_width,  wbuf, 10);
        uitoa(info->fb_height, hbuf, 10);
        kstrcpy(buf, wbuf);
        size_t l = kstrlen(buf);
        buf[l++] = 'x';
        kstrcpy(buf + l, hbuf);
        l = kstrlen(buf);
        buf[l++] = 'x'; buf[l++] = '3'; buf[l++] = '2'; buf[l] = '\0';
        print_kv("Display  : ", buf, COLOR_WHITE);
    }

    /* 5. 统计并显示内存 */
    {
        E820Entry *e820 = (E820Entry *)(uintptr_t)info->e820_addr;
        uint64_t usable = 0;
        for (uint32_t i = 0; i < info->e820_count; i++)
            if (e820[i].type == E820_USABLE)
                usable += e820[i].length;

        char mbuf[24];
        fmt_mem_mb(usable, mbuf);
        print_kv("Memory   : ", mbuf, COLOR_GREEN);
    }

    /* 6. 显示 E820 内存映射表 */
    video_print("\n", COLOR_WHITE);
    video_print("E820 Memory Map:\n", COLOR_YELLOW);
    {
        E820Entry *e820 = (E820Entry *)(uintptr_t)info->e820_addr;
        char hbuf[20], sbuf[20];

        for (uint32_t i = 0; i < info->e820_count; i++) {
            /* 地址 */
            u64_to_hex(e820[i].base, hbuf);
            video_print("  ", COLOR_WHITE);
            video_print(hbuf, COLOR_LGRAY);
            video_print(" + ", COLOR_GRAY);

            /* 长度（MB，若 < 1MB 则显示 KB）*/
            if (e820[i].length >= 1024ULL * 1024ULL) {
                fmt_mem_mb(e820[i].length, sbuf);
            } else {
                uitoa(e820[i].length / 1024ULL, sbuf, 10);
                size_t l = kstrlen(sbuf);
                sbuf[l++] = ' '; sbuf[l++] = 'K'; sbuf[l++] = 'B';
                sbuf[l] = '\0';
            }
            video_print(sbuf, COLOR_LGRAY);
            video_print("  ", COLOR_GRAY);
            video_print(e820_type_name(e820[i].type),
                        e820[i].type == 1 ? COLOR_GREEN : COLOR_GRAY);
            video_print("\n", COLOR_WHITE);
        }
    }

    /* 7. SMP 初始化 */
    video_print("\n", COLOR_WHITE);
    video_print("Initializing SMP...\n", COLOR_YELLOW);
    int cpus = smp_init(info);

    {
        char cbuf[8];
        uitoa((uint64_t)cpus, cbuf, 10);
        print_kv("CPU Cores: ", cbuf, COLOR_GREEN);
    }

    print_line();
    video_print("System ready.\n", COLOR_CYAN);

    /* 8. 永久挂起 */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
