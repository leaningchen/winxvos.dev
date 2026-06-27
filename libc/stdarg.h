#ifndef __STDARG_H__
#define __STDARG_H__

/*===========================================================================
 * stdarg.h — 内核可变参数支持
 *
 * 使用 Clang/GCC 编译器内建函数实现，这是在裸机内核中正确处理
 * x86-64 System V ABI 可变参数的标准方式。
 *
 * 设计原则：
 *   - va_start/va_arg/va_end/va_copy 全部通过编译器内建函数实现
 *   - 编译器负责生成正确的 prologue 代码（保存寄存器参数到 reg_save_area）
 *   - 不依赖任何 libc 运行时，适合裸机内核使用
 *
 * 参考：
 *   - musl libc include/stdarg.h（同样使用 __builtin_va_*）
 *   - Linux kernel include/linux/stdarg.h
 *   - Clang/GCC documentation: Built-in Functions for Variable Arguments
 *
 * 关于为何保留 __builtin_va_*：
 *   va_start 需要编译器在函数 prologue 中生成特殊代码，将 rdi..r9
 *   保存到栈上的 reg_save_area，并设置 overflow_arg_area 指向栈参数。
 *   va_arg 需要编译器理解 x86-64 ABI 的寄存器/栈参数传递规则。
 *   这些逻辑无法用纯 C 宏或内联汇编可靠替代——musl/Linux/Minix 均
 *   直接使用 __builtin_va_*，本内核同样采用此方案。
 *===========================================================================*/

/*
 * va_list — 可变参数列表类型
 * 使用编译器内建的 __builtin_va_list，该类型在 x86-64 上等价于
 * struct __va_list_tag[1]（包含 gp_offset/fp_offset/overflow_arg_area/
 * reg_save_area 四个字段），由编译器在函数 prologue 中正确填充。
 */
typedef __builtin_va_list va_list;

/*
 * va_start(ap, last) — 初始化可变参数列表
 *
 * 必须在访问任何可变参数之前调用。
 * @ap:   va_list 变量（输出）
 * @last: 可变参数列表前的最后一个具名参数
 */
#define va_start(ap, last)  __builtin_va_start(ap, last)

/*
 * va_arg(ap, type) — 获取下一个可变参数
 *
 * 按 x86-64 System V ABI 顺序取出参数（先寄存器区，再栈区）。
 * @ap:   已初始化的 va_list
 * @type: 期望的参数类型
 * 返回值: 下一个参数，类型为 type
 */
#define va_arg(ap, type)    __builtin_va_arg(ap, type)

/*
 * va_end(ap) — 结束可变参数列表访问
 *
 * 必须在函数返回前调用，与 va_start 成对出现。
 * @ap: 已使用完毕的 va_list
 */
#define va_end(ap)          __builtin_va_end(ap)

/*
 * va_copy(dest, src) — 复制可变参数列表状态
 *
 * 复制后 dest 和 src 独立，各自可以独立遍历参数。
 * 使用完 dest 后需调用 va_end(dest)。
 * @dest: 目标 va_list（输出）
 * @src:  源 va_list
 */
#define va_copy(dest, src)  __builtin_va_copy(dest, src)

#endif /* __STDARG_H__ */
