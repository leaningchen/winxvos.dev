#include <types.h>
#include <font.h>
#include "video.h"

/* 帧缓冲状态 */
static volatile uint32_t *fb_base  = (volatile uint32_t *)0;
static uint32_t fb_width   = 0;
static uint32_t fb_height  = 0;
static uint32_t fb_pitch   = 0;    /* 每行字节数 */
static uint32_t fb_stride  = 0;    /* 每行像素数 = pitch / 4 */

/* 光标位置（以像素为单位）*/
static int cursor_x = 0;
static int cursor_y = 0;

/* 字体宽高（初始化后从 font 结构体读取）*/
static uint32_t font_w = 8;
static uint32_t font_h = 16;

/*---------------------------------------------------------------------------
 * video_init — 初始化帧缓冲和 PSF2 字体
 *---------------------------------------------------------------------------*/
void video_init(uint64_t addr, uint32_t w, uint32_t h,
                uint32_t pitch, uint8_t bpp)
{
    (void)bpp;

    fb_base   = (volatile uint32_t *)(uintptr_t)addr;
    fb_width  = w;
    fb_height = h;
    fb_pitch  = pitch;
    fb_stride = pitch / 4;   /* 每行像素数（32bpp）*/

    cursor_x = 0;
    cursor_y = 0;

    /* 初始化 PSF2 字体 */
    __font_initialize__();

    if (font != (PSF_font *)0) {
        font_w = font->width;
        font_h = font->height;
    }
}

/*---------------------------------------------------------------------------
 * video_put_pixel — 写入单个像素
 *---------------------------------------------------------------------------*/
void video_put_pixel(int x, int y, uint32_t rgb)
{
    if ((uint32_t)x >= fb_width || (uint32_t)y >= fb_height)
        return;
    fb_base[(uint32_t)y * fb_stride + (uint32_t)x] = rgb;
}

/*---------------------------------------------------------------------------
 * video_clear — 全屏填充颜色
 *---------------------------------------------------------------------------*/
void video_clear(uint32_t color)
{
    uint32_t total = fb_stride * fb_height;
    for (uint32_t i = 0; i < total; i++)
        fb_base[i] = color;
    cursor_x = 0;
    cursor_y = 0;
}

/*---------------------------------------------------------------------------
 * video_draw_char — 绘制单个字符（PSF2 字体）
 *---------------------------------------------------------------------------*/
void video_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg)
{
    unsigned short *glyph = bitmap(c);
    if (!glyph) return;

    /* PSF2: bytes_per_row = font->bytes / font->height
     * Each row may be 1–4 bytes; we read up to 32 bits and mask to font_w bits.
     * glyph data is stored MSB-first (bit of leftmost pixel is the highest bit). */
    uint32_t bytes_per_row = font->bytes / font_h;

    const uint8_t *row_ptr = (const uint8_t *)glyph;
    for (uint32_t row = 0; row < font_h; row++) {
        /* Assemble up to 4 bytes into a 32-bit word, MSB = leftmost pixel */
        uint32_t line = 0;
        for (uint32_t b = 0; b < bytes_per_row && b < 4; b++)
            line = (line << 8) | row_ptr[b];
        /* Shift so that bit31 is leftmost pixel */
        line <<= (32u - bytes_per_row * 8u);

        for (uint32_t col = 0; col < font_w; col++) {
            uint32_t color = (line & 0x80000000u) ? fg : bg;
            video_put_pixel(x + (int)col, y + (int)row, color);
            line <<= 1;
        }
        row_ptr += bytes_per_row;
    }
}

/*---------------------------------------------------------------------------
 * video_print — 打印字符串，支持 '\n' 换行
 *---------------------------------------------------------------------------*/
void video_print(const char *s, uint32_t fg)
{
    if (!s) return;

    while (*s) {
        char c = *s++;

        if (c == '\n') {
            cursor_x = 0;
            cursor_y += (int)font_h;
            /* 到达屏幕底部回绕 */
            if ((uint32_t)cursor_y + font_h > fb_height)
                cursor_y = 0;
            continue;
        }

        if (c == '\r') {
            cursor_x = 0;
            continue;
        }

        video_draw_char(cursor_x, cursor_y, c, fg, COLOR_BG);
        cursor_x += (int)font_w;

        /* 超出屏幕宽度自动换行 */
        if ((uint32_t)cursor_x + font_w > fb_width) {
            cursor_x = 0;
            cursor_y += (int)font_h;
            if ((uint32_t)cursor_y + font_h > fb_height)
                cursor_y = 0;
        }
    }
}

/*---------------------------------------------------------------------------
 * video_print_at — 在指定字符列行打印
 *---------------------------------------------------------------------------*/
void video_print_at(int col, int row, const char *s, uint32_t fg)
{
    cursor_x = col * (int)font_w;
    cursor_y = row * (int)font_h;
    video_print(s, fg);
}

/*---------------------------------------------------------------------------
 * 工具函数
 *---------------------------------------------------------------------------*/
int video_get_row(void)
{
    return cursor_y / (int)font_h;
}

uint32_t video_get_width(void)  { return fb_width;  }
uint32_t video_get_height(void) { return fb_height; }
