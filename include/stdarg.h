#ifndef __STDARG_H__
#define __STDARG_H__

/*===========================================================================
 * stdarg.h — 内核可变参数支持
 *
 * 实现参考: musl libc (arch/x86_64/bits/stdarg.h)、
 *           Linux 内核 (arch/x86/include/asm/vargs.h) 及
 *           Minix 3 libc 的 x86-64 实现
 *
 * x86-64 System V ABI 变参调用约定:
 * ─────────────────────────────────
 * 1. 整型/指针参数依次放入寄存器: rdi, rsi, rdx, rcx, r8, r9 (最多 6 个)
 * 2. 超出 6 个的参数从右向左压栈 (overflow_arg_area)
 * 3. 编译器在含可变参数的函数 prologue 中，将上述 6 个整型寄存器
 *    保存到栈上的 reg_save_area，并将 overflow_arg_area 指向第 7 个参数
 * 4. va_arg 通过递增 gp_offset 依次取出各参数:
 *      gp_offset=0  → rdi（第1个参数，即 fmt 之后的首个可变参数）
 *      gp_offset=8  → rsi
 *      ...
 *      gp_offset=40 → r9（第6个可变参数）
 *      gp_offset≥48 → 从 overflow_arg_area 取栈上参数
 *
 * 关于 va_start 为何保留 __builtin_va_start:
 * ─────────────────────────────────────────────
 * va_start 需要编译器在函数 prologue 中生成特殊代码，将 rdi..r9 保存
 * 到 reg_save_area，并填写 overflow_arg_area。这一步骤无法用纯 C 宏或
 * 内联汇编可靠地完成（编译器才知道哪些参数已经放入了哪些寄存器）。
 * musl libc、Linux 内核、Minix 3 等所有实现均保留 __builtin_va_start，
 * 本实现与它们保持一致。
 *
 * va_arg/va_end/va_copy 均使用显式 ABI 结构体，不依赖编译器内建函数。
 *===========================================================================*/

/*---------------------------------------------------------------------------
 * __va_list_tag — x86-64 System V ABI 变参状态结构体
 *
 * 该结构体由编译器在函数入口（prologue）填充。字段含义:
 *   gp_offset:         下一个通用寄存器参数在 reg_save_area 中的字节偏移
 *                      有效范围: 0, 8, 16, 24, 32, 40 (对应 rdi..r9)
 *                      当 gp_offset >= 48 时，参数已全部在栈上
 *   fp_offset:         下一个浮点寄存器参数的偏移（内核不使用浮点，固定为 48）
 *   overflow_arg_area: 指向栈上第 7 个及以后参数的起始地址
 *   reg_save_area:     编译器保存 rdi..r9 等寄存器的区域基地址
 *---------------------------------------------------------------------------*/
typedef struct {
    unsigned int gp_offset;         /* 通用寄存器参数偏移 (0~48) */
    unsigned int fp_offset;         /* 浮点寄存器参数偏移 (内核固定为 48) */
    void        *overflow_arg_area; /* 栈上溢出参数区域指针 */
    void        *reg_save_area;     /* 寄存器保存区基地址 */
} __va_list_tag;

/*
 * va_list 定义为数组类型（长度 1），使得:
 *   1. 函数传参时自动退化为指针，无需手动取地址
 *   2. 与 musl libc / glibc / Clang __builtin_va_list 的行为一致
 */
typedef __va_list_tag va_list[1];

/*---------------------------------------------------------------------------
 * va_start — 初始化 va_list
 *
 * 必须由编译器生成 prologue 代码填充 reg_save_area 和 overflow_arg_area，
 * 纯 C 宏无法实现此功能，故与 musl/Linux 相同，保留 __builtin_va_start。
 *
 * @ap:   待初始化的 va_list 变量
 * @last: 可变参数列表前的最后一个固定参数名
 *---------------------------------------------------------------------------*/
#define va_start(ap, last)   __builtin_va_start(ap, last)

/*---------------------------------------------------------------------------
 * va_end — 结束 va_list 的使用
 *
 * 将 overflow_arg_area 清零，防止后续误用悬空指针。
 * 与 musl 的空宏实现相比，此处额外清零以提高安全性。
 *
 * @ap: 已使用完毕的 va_list 变量
 *---------------------------------------------------------------------------*/
#define va_end(ap)           ((ap)[0].overflow_arg_area = (void *)0)

/*---------------------------------------------------------------------------
 * va_copy — 复制 va_list 状态
 *
 * 直接结构体赋值，复制四个字段（与 musl 的做法相同）。
 * 复制后 dest 和 src 独立，互不影响。
 *
 * @dest: 目标 va_list
 * @src:  源 va_list
 *---------------------------------------------------------------------------*/
#define va_copy(dest, src)   ((dest)[0] = (src)[0])

/*---------------------------------------------------------------------------
 * __va_arg_impl — va_arg 的核心实现宏（内部使用）
 *
 * 基于 x86-64 System V ABI 内存布局手工取参数:
 *
 * 对于 sizeof(type) <= 8 的整型/指针类型:
 *   情况 1: gp_offset < 48 (寄存器区域还有参数)
 *     → 从 reg_save_area + gp_offset 读取参数
 *     → gp_offset += 8
 *   情况 2: gp_offset >= 48 (参数在栈上)
 *     → 从 overflow_arg_area 读取参数
 *     → overflow_arg_area += round_up_8(sizeof(type))
 *
 * 内核不使用浮点，故不处理 fp_offset 路径。
 * 对于 sizeof(type) > 8 的大类型，回退到 __builtin_va_arg（内核极少用到）。
 *
 * 注意: 宏展开为逗号表达式，依赖 C 的序列点保证求值顺序。
 *---------------------------------------------------------------------------*/
#define __va_arg_impl(ap, type)                                              \
    ( sizeof(type) <= 8u                                                     \
      ? ( (ap)[0].gp_offset < 48u                                           \
          ? ( (ap)[0].gp_offset += 8u,                                      \
              *(type *)((char *)(ap)[0].reg_save_area +                      \
                        (ap)[0].gp_offset - 8u) )                           \
          : ( (ap)[0].overflow_arg_area =                                   \
                  (char *)(ap)[0].overflow_arg_area +                       \
                  (((unsigned)(sizeof(type)) + 7u) & ~7u),                  \
              *(type *)((char *)(ap)[0].overflow_arg_area -                 \
                        (((unsigned)(sizeof(type)) + 7u) & ~7u)) ) )       \
      : __builtin_va_arg(ap, type) )

/*---------------------------------------------------------------------------
 * va_arg — 取下一个可变参数
 *
 * 用法示例:
 *   int n    = va_arg(ap, int);
 *   char *s  = va_arg(ap, char *);
 *   long x   = va_arg(ap, long);
 *
 * @ap:   已由 va_start 初始化的 va_list
 * @type: 期望取出的参数类型
 * 返回值: 类型为 type 的参数值
 *---------------------------------------------------------------------------*/
#define va_arg(ap, type)     __va_arg_impl(ap, type)

#endif /* __STDARG_H__ */
