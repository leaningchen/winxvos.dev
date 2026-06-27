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

/*===========================================================================
 * 一、初始化与基础接口
 *===========================================================================*/

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
 * video_put_pixel — 写入单个像素（含边界检查）
 *---------------------------------------------------------------------------*/
void video_put_pixel(int x, int y, uint32_t rgb)
{
    if ((uint32_t)x >= fb_width || (uint32_t)y >= fb_height)
        return;
    fb_base[(uint32_t)y * fb_stride + (uint32_t)x] = rgb;
}

/*---------------------------------------------------------------------------
 * video_clear — 全屏填充颜色，重置光标到左上角
 *---------------------------------------------------------------------------*/
void video_clear(uint32_t color)
{
    uint32_t total = fb_stride * fb_height;
    for (uint32_t i = 0; i < total; i++)
        fb_base[i] = color;
    cursor_x = 0;
    cursor_y = 0;
}

/*===========================================================================
 * 二、文字输出接口
 *===========================================================================*/

/*---------------------------------------------------------------------------
 * video_draw_char — 绘制单个字符（PSF2 字体）
 *
 * PSF2 字形数据为 MSB-first（最高位 = 最左像素），每行 bytes_per_row 字节。
 * 最多组合 4 字节为 32 位 line，再按位逐列映射为像素颜色。
 *---------------------------------------------------------------------------*/
void video_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg)
{
    unsigned short *glyph = bitmap(c);
    if (!glyph) return;

    /* bytes_per_row = 每字形总字节数 / 字体高度 */
    uint32_t bytes_per_row = font->bytes / font_h;

    const uint8_t *row_ptr = (const uint8_t *)glyph;
    for (uint32_t row = 0; row < font_h; row++) {
        /* 将最多 4 字节组合为 32 位整数，MSB 对应最左像素 */
        uint32_t line = 0;
        for (uint32_t b = 0; b < bytes_per_row && b < 4; b++)
            line = (line << 8) | row_ptr[b];
        /* 将有效位移到 bit31，使得 bit31 = 最左像素 */
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
 * video_print — 打印字符串，维护全局光标
 *
 * 支持控制字符:
 *   '\n' — 光标移到下一行行首，到达底部时从顶部回绕
 *   '\r' — 光标移到当前行行首
 * 每个可见字符绘制后光标右移 font_w，超出屏幕宽度时自动换行。
 *---------------------------------------------------------------------------*/
void video_print(const char *s, uint32_t fg)
{
    if (!s) return;

    while (*s) {
        char c = *s++;

        if (c == '\n') {
            cursor_x = 0;
            cursor_y += (int)font_h;
            /* 到达屏幕底部时从顶部回绕 */
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

        /* 超出屏幕宽度时自动换行 */
        if ((uint32_t)cursor_x + font_w > fb_width) {
            cursor_x = 0;
            cursor_y += (int)font_h;
            if ((uint32_t)cursor_y + font_h > fb_height)
                cursor_y = 0;
        }
    }
}

/*---------------------------------------------------------------------------
 * video_print_at — 在指定字符列行位置打印
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

/* 返回当前光标的像素 Y 坐标（即光标所在行的顶部像素，也是下一行文字将写入的位置）*/
int video_get_cursor_y(void)
{
    return cursor_y;
}

uint32_t video_get_width(void)  { return fb_width;  }
uint32_t video_get_height(void) { return fb_height; }

/*===========================================================================
 * 三、图形绘制实现
 *===========================================================================*/

/*---------------------------------------------------------------------------
 * video_draw_line — Bresenham 直线算法
 *
 * 原理: 用整数加减法近似直线方程，无浮点运算，适合内核环境。
 * 算法步骤:
 *   1. 计算 dx = |x1-x0|, dy = |y1-y0| 和步进方向 sx, sy
 *   2. 初始误差 err = dx - dy
 *   3. 每步: 画当前点，根据误差判断移动 x、y 还是同时移动
 *---------------------------------------------------------------------------*/
void video_draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;

    /* 取绝对值 */
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;

    /* x 和 y 方向的步进（+1 或 -1）*/
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;

    int err = adx - ady;   /* Bresenham 误差项 */

    while (1) {
        video_put_pixel(x0, y0, color);

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = err * 2;

        if (e2 > -ady) {
            err -= ady;
            x0  += sx;
        }
        if (e2 < adx) {
            err += adx;
            y0  += sy;
        }
    }
}

/*---------------------------------------------------------------------------
 * video_draw_rect — 绘制矩形轮廓（空心）
 *
 * 画四条边: 上、下（水平线）和左、右（垂直线）。
 *---------------------------------------------------------------------------*/
void video_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;

    /* 上边和下边 */
    video_draw_line(x,         y,         x + w - 1, y,         color);
    video_draw_line(x,         y + h - 1, x + w - 1, y + h - 1, color);

    /* 左边和右边 */
    video_draw_line(x,         y,         x,         y + h - 1, color);
    video_draw_line(x + w - 1, y,         x + w - 1, y + h - 1, color);
}

/*---------------------------------------------------------------------------
 * video_fill_rect — 绘制填充矩形
 *
 * 双重循环逐行填充。对于较大矩形，内循环每次直接写入帧缓冲地址，
 * 避免 video_put_pixel 的边界检查开销。
 *---------------------------------------------------------------------------*/
void video_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;

    /* 夹紧到屏幕范围 */
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if ((uint32_t)x1 > fb_width)  x1 = (int)fb_width;
    if ((uint32_t)y1 > fb_height) y1 = (int)fb_height;

    for (int row = y0; row < y1; row++) {
        volatile uint32_t *line_ptr = fb_base + (uint32_t)row * fb_stride + (uint32_t)x0;
        for (int col = x0; col < x1; col++)
            *line_ptr++ = color;
    }
}

/*---------------------------------------------------------------------------
 * video_draw_circle — 绘制圆形轮廓（空心）
 *
 * Midpoint Circle (Bresenham) 算法:
 *   从点 (r, 0) 出发，沿圆弧顺时针推进，每步决策向左还是左下移动。
 *   利用圆的 8 次对称性，每步同时画 8 个对称点，减少计算量。
 *
 * 误差判断:
 *   初始 d = 1 - r
 *   若 d < 0: 向右移动，d += 2*dx + 3
 *   若 d >= 0: 向右下移动，d += 2*(dx - dy) + 5，dy--
 *---------------------------------------------------------------------------*/
void video_draw_circle(int cx, int cy, int r, uint32_t color)
{
    if (r <= 0) {
        video_put_pixel(cx, cy, color);
        return;
    }

    int dx = 0, dy = r;
    int d  = 1 - r;   /* 初始决策参数 */

    /* 内联宏：画圆的 8 个对称点 */
#define CIRCLE_PLOT8(px, py) do { \
    video_put_pixel(cx + (px), cy + (py), color); \
    video_put_pixel(cx - (px), cy + (py), color); \
    video_put_pixel(cx + (px), cy - (py), color); \
    video_put_pixel(cx - (px), cy - (py), color); \
    video_put_pixel(cx + (py), cy + (px), color); \
    video_put_pixel(cx - (py), cy + (px), color); \
    video_put_pixel(cx + (py), cy - (px), color); \
    video_put_pixel(cx - (py), cy - (px), color); \
} while(0)

    while (dx <= dy) {
        CIRCLE_PLOT8(dx, dy);
        if (d < 0) {
            d += 2 * dx + 3;
        } else {
            d += 2 * (dx - dy) + 5;
            dy--;
        }
        dx++;
    }
#undef CIRCLE_PLOT8
}

/*---------------------------------------------------------------------------
 * video_fill_circle — 绘制填充圆形
 *
 * Midpoint Circle 变体，利用 4 次对称性：
 * 每步 (dx, dy) 绘制 4 条水平扫描线，覆盖圆内区域：
 *   row = cy ± dy, 从 cx - dx 到 cx + dx
 *   row = cy ± dx, 从 cx - dy 到 cx + dy
 * 相邻步骤的扫描线会重叠，但对帧缓冲写入无害。
 *---------------------------------------------------------------------------*/
void video_fill_circle(int cx, int cy, int r, uint32_t color)
{
    if (r <= 0) {
        video_put_pixel(cx, cy, color);
        return;
    }

    int dx = 0, dy = r;
    int d  = 1 - r;

    while (dx <= dy) {
        /* 利用 4 次对称性画 4 条水平扫描线 */
        video_fill_rect(cx - dx, cy - dy, 2 * dx + 1, 1, color);
        video_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
        video_fill_rect(cx - dy, cy - dx, 2 * dy + 1, 1, color);
        video_fill_rect(cx - dy, cy + dx, 2 * dy + 1, 1, color);

        if (d < 0) {
            d += 2 * dx + 3;
        } else {
            d += 2 * (dx - dy) + 5;
            dy--;
        }
        dx++;
    }
}

/*---------------------------------------------------------------------------
 * 圆角矩形内部辅助：用 Midpoint Circle 算法画单象限圆弧并输出像素
 *
 * 仅画 dx >= 0 && dy >= 0 的第一象限部分，
 * 由调用者通过镜像坐标分配到四个角落。
 *
 * 参数 cx, cy: 该角落圆弧的圆心坐标
 * octant:  控制画哪个 1/4 弧:
 *   0 = 左上角 (-dx, -dy)
 *   1 = 右上角 (+dx, -dy)
 *   2 = 右下角 (+dx, +dy)
 *   3 = 左下角 (-dx, +dy)
 *---------------------------------------------------------------------------*/

/*
 * __arc_points — 将圆弧一步的对称点映射到四个角，仅画边框
 * cx0/cy0: 左上角圆心, cx1/cy1: 右上角圆心,
 * cx2/cy2: 右下角圆心, cx3/cy3: 左下角圆心
 */
static void __round_rect_outline_step(
    int cx0, int cy0, int cx1, int cy1,
    int cx2, int cy2, int cx3, int cy3,
    int dx, int dy, uint32_t color)
{
    /* 左上: (-dx, -dy) */
    video_put_pixel(cx0 - dx, cy0 - dy, color);
    video_put_pixel(cx0 - dy, cy0 - dx, color);
    /* 右上: (+dx, -dy) */
    video_put_pixel(cx1 + dx, cy1 - dy, color);
    video_put_pixel(cx1 + dy, cy1 - dx, color);
    /* 右下: (+dx, +dy) */
    video_put_pixel(cx2 + dx, cy2 + dy, color);
    video_put_pixel(cx2 + dy, cy2 + dx, color);
    /* 左下: (-dx, +dy) */
    video_put_pixel(cx3 - dx, cy3 + dy, color);
    video_put_pixel(cx3 - dy, cy3 + dx, color);
}

/*---------------------------------------------------------------------------
 * video_draw_round_rect — 绘制圆角矩形轮廓
 *
 * 绘制方式:
 *   1. 四条直线（上/下/左/右，去掉圆角占据的部分）
 *   2. 四个角落各一个 1/4 Bresenham 圆弧
 *
 * 圆角半径 r 自动夹紧: r <= min(w,h)/2
 *---------------------------------------------------------------------------*/
void video_draw_round_rect(int x, int y, int w, int h, int r, uint32_t color)
{
    if (w <= 0 || h <= 0) return;

    /* 夹紧圆角半径 */
    int max_r = (w < h ? w : h) / 2;
    if (r > max_r) r = max_r;
    if (r < 0)     r = 0;

    if (r == 0) {
        /* 无圆角，直接画矩形 */
        video_draw_rect(x, y, w, h, color);
        return;
    }

    /* 四个角圆心坐标 */
    int cx0 = x + r;         int cy0 = y + r;         /* 左上 */
    int cx1 = x + w - 1 - r; int cy1 = y + r;         /* 右上 */
    int cx2 = x + w - 1 - r; int cy2 = y + h - 1 - r; /* 右下 */
    int cx3 = x + r;         int cy3 = y + h - 1 - r; /* 左下 */

    /* 四条直线（跳过圆角部分）*/
    video_draw_line(cx0, y,         cx1, y,         color); /* 上 */
    video_draw_line(cx2, y + h - 1, cx3, y + h - 1, color); /* 下 */
    video_draw_line(x,         cy0, x,         cy3, color); /* 左 */
    video_draw_line(x + w - 1, cy1, x + w - 1, cy2, color); /* 右 */

    /* 四角圆弧（Midpoint Circle）*/
    int dx = 0, dy = r;
    int d  = 1 - r;

    while (dx <= dy) {
        __round_rect_outline_step(cx0, cy0, cx1, cy1, cx2, cy2, cx3, cy3,
                                  dx, dy, color);
        if (d < 0) {
            d += 2 * dx + 3;
        } else {
            d += 2 * (dx - dy) + 5;
            dy--;
        }
        dx++;
    }
}

/*---------------------------------------------------------------------------
 * video_fill_round_rect — 绘制填充圆角矩形
 *
 * 填充策略:
 *   1. 填充中央矩形区域（去掉四角圆弧区域）
 *   2. 用 Midpoint Circle 每步画 4 条水平线，覆盖四个圆角扇形
 *---------------------------------------------------------------------------*/
void video_fill_round_rect(int x, int y, int w, int h, int r, uint32_t color)
{
    if (w <= 0 || h <= 0) return;

    /* 夹紧圆角半径 */
    int max_r = (w < h ? w : h) / 2;
    if (r > max_r) r = max_r;
    if (r < 0)     r = 0;

    if (r == 0) {
        video_fill_rect(x, y, w, h, color);
        return;
    }

    /* 四个角圆心坐标 */
    int cx0 = x + r;         int cy0 = y + r;         /* 左上 */
    int cx1 = x + w - 1 - r;                          /* 右上（x 坐标）*/
    int cy2 = y + h - 1 - r;                          /* 右下/左下（y 坐标）*/

    /* 填充中央三条水平矩形带:
     *   上圆弧区域:  y ~ y+r-1  (高度 r, 宽度 w - 2r)
     *   中间矩形:   y+r ~ y+h-r-1 (高度 h-2r, 完整宽度 w)
     *   下圆弧区域:  y+h-r ~ y+h-1 (高度 r, 宽度 w - 2r)
     * 注意圆弧行由下面的 Midpoint Circle 步骤补全左右圆角
     */
    /* 中间完整宽度区域 */
    video_fill_rect(x, y + r, w, h - 2 * r, color);

    /* 用 Midpoint Circle 填充上下两段圆角扇形 */
    int dx = 0, dy = r;
    int d  = 1 - r;

    while (dx <= dy) {
        /* 上方两条扫描线（覆盖左上角和右上角的同一行）*/
        video_fill_rect(cx0 - dy, cy0 - dx,  /* 左上角该步左端 */
                        cx1 + dy - (cx0 - dy) + 1, 1, color);
        video_fill_rect(cx0 - dx, cy0 - dy,
                        cx1 + dx - (cx0 - dx) + 1, 1, color);

        /* 下方两条扫描线（左下角和右下角）*/
        video_fill_rect(cx0 - dy, cy2 + dx,
                        cx1 + dy - (cx0 - dy) + 1, 1, color);
        video_fill_rect(cx0 - dx, cy2 + dy,
                        cx1 + dx - (cx0 - dx) + 1, 1, color);

        if (d < 0) {
            d += 2 * dx + 3;
        } else {
            d += 2 * (dx - dy) + 5;
            dy--;
        }
        dx++;
    }
}

/*---------------------------------------------------------------------------
 * video_draw_polygon — 绘制多边形轮廓（依次连线，首尾相连）
 *---------------------------------------------------------------------------*/
void video_draw_polygon(const int *xs, const int *ys, int n, uint32_t color)
{
    if (n < 2) return;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;   /* 最后一个顶点连回第一个顶点 */
        video_draw_line(xs[i], ys[i], xs[j], ys[j], color);
    }
}

/*---------------------------------------------------------------------------
 * __isort — 对整数数组进行插入排序（小数组，n <= VIDEO_MAX_POLY_VERTS）
 *---------------------------------------------------------------------------*/
static void __isort(int *arr, int n)
{
    for (int i = 1; i < n; i++) {
        int key = arr[i];
        int j   = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

/*---------------------------------------------------------------------------
 * video_fill_polygon — 扫描线填充多边形
 *
 * 算法步骤:
 *   1. 求所有顶点的 y 范围 [y_min, y_max]
 *   2. 遍历每一行 y:
 *      a. 枚举所有边，若边跨越该 y 行（y_min_edge < y <= y_max_edge），
 *         用线性插值求交点 x：
 *           x = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
 *      b. 将所有交点 x 排序
 *      c. 两两配对：[x[0],x[1]], [x[2],x[3]]... 各填充水平线段
 *
 * 奇偶规则（Even-Odd Rule）: 对于凸多边形每行恰好 2 个交点，凹多边形可能更多。
 * 交点数上限: VIDEO_MAX_POLY_VERTS（超出时截断）。
 *---------------------------------------------------------------------------*/
void video_fill_polygon(const int *xs, const int *ys, int n, uint32_t color)
{
    if (n < 3) return;
    if (n > VIDEO_MAX_POLY_VERTS) n = VIDEO_MAX_POLY_VERTS;

    /* 求 y 范围 */
    int y_min = ys[0], y_max = ys[0];
    for (int i = 1; i < n; i++) {
        if (ys[i] < y_min) y_min = ys[i];
        if (ys[i] > y_max) y_max = ys[i];
    }

    /* 夹紧到屏幕 */
    if (y_min < 0)              y_min = 0;
    if ((uint32_t)y_max >= fb_height) y_max = (int)fb_height - 1;

    /* 交点 x 坐标缓冲区（栈上，最多 VIDEO_MAX_POLY_VERTS 个）*/
    int node_x[VIDEO_MAX_POLY_VERTS];

    for (int y = y_min; y <= y_max; y++) {
        int cnt = 0;   /* 该行交点数 */

        /* 遍历所有边，求与 y 行的交点 */
        for (int i = 0; i < n; i++) {
            int j  = (i + 1) % n;
            int y0 = ys[i], y1 = ys[j];
            int x0 = xs[i], x1 = xs[j];

            /* 跳过水平边（y0 == y1）*/
            if (y0 == y1) continue;

            /* 边跨越 y 行的条件（使用半开区间避免顶点被计数两次）*/
            if ((y0 <= y && y < y1) || (y1 <= y && y < y0)) {
                /* 线性插值求交点 x（整数运算，四舍五入）*/
                int x = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
                if (cnt < VIDEO_MAX_POLY_VERTS)
                    node_x[cnt++] = x;
            }
        }

        /* 对交点排序（通常只有 2~4 个，插入排序足够）*/
        __isort(node_x, cnt);

        /* 两两配对，填充水平线段 */
        for (int k = 0; k + 1 < cnt; k += 2) {
            int left  = node_x[k];
            int right = node_x[k + 1];
            if (left > right) { int tmp = left; left = right; right = tmp; }
            /* 夹紧到屏幕宽度 */
            if (left  < 0)              left  = 0;
            if ((uint32_t)right >= fb_width) right = (int)fb_width - 1;
            if (left <= right)
                video_fill_rect(left, y, right - left + 1, 1, color);
        }
    }
}
