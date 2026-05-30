#ifndef __VIDEO_H__
#define __VIDEO_H__

#include <types.h>

/* 颜色常量（32bpp B8G8R8X8 格式）*/
#define COLOR_BG        0x00001428U   /* 深蓝背景 */
#define COLOR_WHITE     0x00FFFFFFU
#define COLOR_CYAN      0x0000FFFFU
#define COLOR_GREEN     0x0000FF80U
#define COLOR_YELLOW    0x00FFFF00U
#define COLOR_RED       0x00FF4040U
#define COLOR_GRAY      0x00808080U
#define COLOR_LGRAY     0x00C0C0C0U

#define RGB(r, g, b)    ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))

/*
 * video_init - 初始化帧缓冲和 PSF2 字体
 * @addr:  LFB 物理地址
 * @w:     水平分辨率
 * @h:     垂直分辨率
 * @pitch: 每行字节数
 * @bpp:   色深（32）
 */
void video_init(uint64_t addr, uint32_t w, uint32_t h,
                uint32_t pitch, uint8_t bpp);

/* 全屏填充指定颜色 */
void video_clear(uint32_t color);

/* 写入单个像素 */
void video_put_pixel(int x, int y, uint32_t rgb);

/* 绘制单个字符（PSF2 字体，使用 bitmap() 接口）*/
void video_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);

/*
 * video_print - 打印字符串（支持 '\n' 换行）
 * 维护全局光标 cursor_x/cursor_y，自动换行
 */
void video_print(const char *s, uint32_t fg);

/* 在指定行列位置打印（更新光标到该位置后打印）*/
void video_print_at(int col, int row, const char *s, uint32_t fg);

/* 获取当前光标行（以字符为单位）*/
int video_get_row(void);

/* 获取屏幕宽度（像素）*/
uint32_t video_get_width(void);

/* 获取屏幕高度（像素）*/
uint32_t video_get_height(void);

#endif /* __VIDEO_H__ */
