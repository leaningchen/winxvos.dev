#ifndef __VIDEO_H__
#define __VIDEO_H__

#include <types.h>

/*===========================================================================
 * video.h — VESA LFB 帧缓冲显示驱动接口
 *
 * 支持 32bpp B8G8R8X8 格式的线性帧缓冲（Linear Frame Buffer）。
 * 提供两类功能:
 *   1. 文字输出: 使用 PSF2 位图字体在屏幕上打印字符/字符串
 *   2. 图形绘制: 直线、矩形、圆形、圆角矩形、任意多边形
 *
 * 坐标系: 原点 (0,0) 在屏幕左上角，x 向右，y 向下。
 * 像素格式: 0x00RRGGBB（高字节忽略，RGB 各 8 位）
 *===========================================================================*/

/*---------------------------------------------------------------------------
 * 预定义颜色常量（32bpp B8G8R8X8 格式）
 *---------------------------------------------------------------------------*/
#define COLOR_BG        0x00001428U   /* 深蓝背景色 */
#define COLOR_WHITE     0x00FFFFFFU   /* 白色 */
#define COLOR_CYAN      0x0000FFFFU   /* 青色 */
#define COLOR_GREEN     0x0000FF80U   /* 亮绿色 */
#define COLOR_YELLOW    0x00FFFF00U   /* 黄色 */
#define COLOR_RED       0x00FF4040U   /* 红色 */
#define COLOR_GRAY      0x00808080U   /* 深灰色 */
#define COLOR_LGRAY     0x00C0C0C0U   /* 浅灰色 */

/*
 * RGB — 从 R/G/B 三个分量合成颜色值
 * @r, @g, @b: 各通道值 0~255
 * 示例: RGB(255, 128, 0) = 橙色
 */
#define RGB(r, g, b)    ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))

/*---------------------------------------------------------------------------
 * 图形绘制限制常量
 *---------------------------------------------------------------------------*/
/* 多边形填充时交点数组的最大容量，限制单次可处理的最大顶点数 */
#define VIDEO_MAX_POLY_VERTS  64

/*===========================================================================
 * 一、初始化与基础接口
 *===========================================================================*/

/*
 * video_init — 初始化帧缓冲和 PSF2 字体
 * @addr:  LFB 物理地址（来自 BootInfo.fb_addr）
 * @w:     水平分辨率（像素）
 * @h:     垂直分辨率（像素）
 * @pitch: 每行字节数（可能大于 w*4，存在行填充）
 * @bpp:   色深（目前仅支持 32）
 */
void video_init(uint64_t addr, uint32_t w, uint32_t h,
                uint32_t pitch, uint8_t bpp);

/* 将整个屏幕填充为指定颜色，并将光标重置到左上角 */
void video_clear(uint32_t color);

/* 写入单个像素（含边界检查，超出屏幕范围时静默忽略）*/
void video_put_pixel(int x, int y, uint32_t rgb);

/*===========================================================================
 * 二、文字输出接口
 *===========================================================================*/

/*
 * video_draw_char — 绘制单个字符（PSF2 字体）
 * @x, @y: 字符左上角像素坐标
 * @c:     ASCII 字符
 * @fg:    前景色
 * @bg:    背景色
 */
void video_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);

/*
 * video_print — 打印字符串（维护全局光标）
 * 支持 '\n'（换行）、'\r'（回车）；到达屏幕底部时从顶部回绕。
 * @s:  待打印的字符串
 * @fg: 前景色
 */
void video_print(const char *s, uint32_t fg);

/*
 * video_print_at — 在指定字符列行位置打印
 * 将光标移动到 (col, row) 后调用 video_print。
 * @col, @row: 字符单位的列、行坐标
 * @s:  字符串
 * @fg: 前景色
 */
void video_print_at(int col, int row, const char *s, uint32_t fg);

/* 获取当前光标所在行（字符单位）*/
int video_get_row(void);

/* 获取当前光标的像素 Y 坐标（即下一行文字的顶部像素位置）*/
int video_get_cursor_y(void);

/* 获取屏幕宽度（像素）*/
uint32_t video_get_width(void);

/* 获取屏幕高度（像素）*/
uint32_t video_get_height(void);

/*===========================================================================
 * 三、图形绘制接口
 *===========================================================================*/

/*---------------------------------------------------------------------------
 * 直线
 *---------------------------------------------------------------------------*/

/*
 * video_draw_line — Bresenham 直线算法
 * 在 (x0,y0) 到 (x1,y1) 之间绘制一条直线，支持任意方向。
 * 使用纯整数加减法，无浮点运算。
 */
void video_draw_line(int x0, int y0, int x1, int y1, uint32_t color);

/*---------------------------------------------------------------------------
 * 矩形
 *---------------------------------------------------------------------------*/

/*
 * video_draw_rect — 绘制矩形轮廓（空心）
 * @x, @y: 左上角坐标
 * @w, @h: 宽度和高度（像素）
 * @color: 线条颜色
 */
void video_draw_rect(int x, int y, int w, int h, uint32_t color);

/*
 * video_fill_rect — 绘制填充矩形
 * @x, @y: 左上角坐标
 * @w, @h: 宽度和高度（像素）
 * @color: 填充颜色
 */
void video_fill_rect(int x, int y, int w, int h, uint32_t color);

/*---------------------------------------------------------------------------
 * 圆形
 *---------------------------------------------------------------------------*/

/*
 * video_draw_circle — 绘制圆形轮廓（空心）
 * 使用 Midpoint Circle (Bresenham) 算法，利用 8 次对称性，每步画 8 个点。
 * @cx, @cy: 圆心坐标
 * @r:       半径（像素）
 * @color:   线条颜色
 */
void video_draw_circle(int cx, int cy, int r, uint32_t color);

/*
 * video_fill_circle — 绘制填充圆形
 * Midpoint Circle 变体，利用 4 次对称性，每步画 4 条水平扫描线。
 * @cx, @cy: 圆心坐标
 * @r:       半径（像素）
 * @color:   填充颜色
 */
void video_fill_circle(int cx, int cy, int r, uint32_t color);

/*---------------------------------------------------------------------------
 * 圆角矩形
 *---------------------------------------------------------------------------*/

/*
 * video_draw_round_rect — 绘制圆角矩形轮廓（空心）
 * 四条直线 + 四个 1/4 Bresenham 圆弧（分别分配到四个角）。
 * 若 r 超出矩形半宽或半高，自动夹紧。
 * @x, @y: 左上角坐标
 * @w, @h: 宽度和高度（像素）
 * @r:     圆角半径（像素）
 * @color: 线条颜色
 */
void video_draw_round_rect(int x, int y, int w, int h, int r, uint32_t color);

/*
 * video_fill_round_rect — 绘制填充圆角矩形
 * 中央矩形区域 + 四个圆角扇形区域（圆弧扫描线填充）。
 * @x, @y: 左上角坐标
 * @w, @h: 宽度和高度（像素）
 * @r:     圆角半径（像素）
 * @color: 填充颜色
 */
void video_fill_round_rect(int x, int y, int w, int h, int r, uint32_t color);

/*---------------------------------------------------------------------------
 * 任意多边形
 *---------------------------------------------------------------------------*/

/*
 * video_draw_polygon — 绘制多边形轮廓（空心）
 * 依次连接各顶点，最后一个顶点与第一个顶点相连，形成封闭多边形。
 * @xs, @ys: 顶点坐标数组（长度均为 n）
 * @n:       顶点数量（至少 2 个）
 * @color:   线条颜色
 */
void video_draw_polygon(const int *xs, const int *ys, int n, uint32_t color);

/*
 * video_fill_polygon — 绘制填充多边形
 * 使用扫描线算法（Scanline Fill）：
 *   1. 遍历每行 y（从 y_min 到 y_max）
 *   2. 计算该行与各边的交点 x 坐标列表
 *   3. 对交点排序后两两配对，填充水平线段
 * 最多支持 VIDEO_MAX_POLY_VERTS 个顶点，超出时静默截断。
 * @xs, @ys: 顶点坐标数组（长度均为 n）
 * @n:       顶点数量（至少 3 个）
 * @color:   填充颜色
 */
void video_fill_polygon(const int *xs, const int *ys, int n, uint32_t color);

#endif /* __VIDEO_H__ */
