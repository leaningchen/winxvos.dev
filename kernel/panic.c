#include <types.h>
#include <arch/x86_64/x86_64.h>
#include <string.h>
#include <drivers/video/video.h>

/*---------------------------------------------------------------------------
 * panic — 内核致命错误处理
 * 打印错误信息后永久停机
 *---------------------------------------------------------------------------*/
void panic(const char *msg)
{
    cli();
    video_print("\n\nPANIC: ", COLOR_RED);
    video_print(msg, COLOR_RED);
    video_print("\n", COLOR_RED);
    video_print("System halted.\n", COLOR_YELLOW);

    /* 永久停机 */
    while (1)
        hlt();
}