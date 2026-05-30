/*
 * trap_entry.S — 64位陷阱/中断入口汇编
 *
 * 每个中断/异常的处理流程:
 *   1. 如果该异常不推送错误码, 则推入假错误码 0
 *   2. 推入中断号 (trapno)
 *   3. 保存所有通用寄存器到 trapframe
 *   4. 调用 C 函数 trap_handler(struct trapframe *tf)
 *   5. 从 trapframe 恢复寄存器
 *   6. 从栈中弹出错误码和中断号
 *   7. iretq 返回
 *
 * 寄存器 push 顺序: rax, rcx, rdx, rbx, rbp, rsi, rdi,
 *                    r8, r9, r10, r11, r12, r13, r14, r15
 * 这样 r15 在栈顶 (RSP+0 = struct offset 0), 与 struct trapframe 对齐。
 *
 * 参考 xv6 的 trapasm.S，适配64位
 */

#define KERN_CODE_SEL  0x08

/* trapframe 结构偏移 (与 include/idt.h 中的 struct trapframe 对应) */
#define TF_R15    0
#define TF_R14    8
#define TF_R13    16
#define TF_R12    24
#define TF_R11    32
#define TF_R10    40
#define TF_R9     48
#define TF_R8     56
#define TF_RDI    64
#define TF_RSI    72
#define TF_RBP    80
#define TF_RBX    88
#define TF_RDX    96
#define TF_RCX    104
#define TF_RAX    112
#define TF_TRAPNO 120
#define TF_ERR    128
#define TF_RIP    136
#define TF_CS     144
#define TF_RFLAGS 152
#define TF_RSP    160
#define TF_SS     168
#define TF_SIZE   176

.text
.globl trap_handler

/*---------------------------------------------------------------------------
 * 宏: TRAP_NOERR — 无错误码的异常处理入口
 * 硬件不推入错误码, 我们手动推入 0 以保持栈结构一致
 *---------------------------------------------------------------------------*/
.macro TRAP_NOERR handler trapno
.globl \handler
\handler:
    pushq $0                 /* 假错误码 */
    pushq $\trapno           /* 中断号 */
    jmp  alltraps
.endm

/*---------------------------------------------------------------------------
 * 宏: TRAP_ERR — 有错误码的异常处理入口
 * 硬件已推入错误码, 不需要额外推入
 *---------------------------------------------------------------------------*/
.macro TRAP_ERR handler trapno
.globl \handler
\handler:
    pushq $\trapno           /* 中断号 */
    jmp  alltraps
.endm

/*---------------------------------------------------------------------------
 * alltraps — 所有陷阱/中断的公共入口
 * 保存所有通用寄存器到 trapframe, 然后调用 trap_handler
 *
 * push 顺序与 struct trapframe 字段顺序一致:
 *   r15 在 offset 0 (栈最低地址) = 最后 push
 *   rax 在 offset 112 (栈最高地址) = 最先 push
 *---------------------------------------------------------------------------*/
.globl alltraps
alltraps:
    /* 保存所有通用寄寄器 — push 顺序匹配 struct trapframe 字段顺序 */
    pushq   %rax             /* offset 112 (最先 push = 最高栈地址) */
    pushq   %rcx             /* offset 104 */
    pushq   %rdx             /* offset 96 */
    pushq   %rbx             /* offset 88 */
    pushq   %rbp             /* offset 80 */
    pushq   %rsi             /* offset 72 */
    pushq   %rdi             /* offset 64 */
    pushq   %r8              /* offset 56 */
    pushq   %r9              /* offset 48 */
    pushq   %r10             /* offset 40 */
    pushq   %r11             /* offset 32 */
    pushq   %r12             /* offset 24 */
    pushq   %r13             /* offset 16 */
    pushq   %r14             /* offset 8 */
    pushq   %r15             /* offset 0 (最后 push = 最低栈地址 = RSP) */

    /* 设置内核数据段 */
    movw    $0x20, %ax        /* 内核数据段选择子 (GDT entry 3) */
    movw    %ax, %ds
    movw    %ax, %es

    /* 将栈指针 (trapframe 地址) 传给 trap_handler */
    movq    %rsp, %rdi

    /* 调用 C 函数 */
    call    trap_handler

/*---------------------------------------------------------------------------
 * trapret — 从陷阱/中断返回
 * 恢复所有通用寄寄器, 弹出错误码和中断号, iretq
 *
 * pop 顺序与 push 顺序逆序:
 *   r15 最先 pop (从最低栈地址)
 *   rax 最后 pop (从最高栈地址)
 *---------------------------------------------------------------------------*/
.globl trapret
trapret:
    /* 恢复通用寄寄器 — pop 顺序与 push 逆序 */
    popq    %r15             /* offset 0 (最低栈地址, 最先 pop) */
    popq    %r14             /* offset 8 */
    popq    %r13             /* offset 16 */
    popq    %r12             /* offset 24 */
    popq    %r11             /* offset 32 */
    popq    %r10             /* offset 40 */
    popq    %r9              /* offset 48 */
    popq    %r8              /* offset 56 */
    popq    %rdi             /* offset 64 */
    popq    %rsi             /* offset 72 */
    popq    %rbp             /* offset 80 */
    popq    %rbx             /* offset 88 */
    popq    %rdx             /* offset 96 */
    popq    %rcx             /* offset 104 */
    popq    %rax             /* offset 112 (最高栈地址, 最后 pop) */

    /* 弹出 trapno 和 err */
    addq    $16, %rsp         /* 跳过 trapno + err (各8字节) */

    /* iretq 恢复: rip, cs, rflags, rsp, ss */
    iretq

/*---------------------------------------------------------------------------
 * CPU 异常定义 (使用 TRAP_NOERR 和 TRAP_ERR)
 *---------------------------------------------------------------------------*/
TRAP_NOERR handler_divide,    0     /* #DE 除法错误 */
TRAP_NOERR handler_debug,     1     /* #DB 调试异常 */
TRAP_ERR   handler_nmi,       2     /* #NM NMI (虽然无err码,但特殊处理) */
TRAP_NOERR handler_brkpt,     3     /* #BP 断点 */
TRAP_NOERR handler_overflow,  4     /* #OF 溢出 */
TRAP_NOERR handler_bound,     5     /* #BR BOUND */
TRAP_NOERR handler_illop,     6     /* #UD 非法操作码 */
TRAP_NOERR handler_device,    7     /* #NM 设备不可用 */
TRAP_ERR   handler_dflt,      8     /* #DF 双重故障 (有错误码) */
TRAP_NOERR handler_coproc,    9     /* #09 协处理器 */
TRAP_ERR   handler_tss,      10     /* #TS 无效TSS */
TRAP_ERR   handler_segnp,    11     /* #NP 段不存在 */
TRAP_ERR   handler_stack,    12     /* #SS 栈段故障 */
TRAP_ERR   handler_gpflt,    13     /* #GP 一般保护 */
TRAP_ERR   handler_pgflt,    14     /* #PF 页故障 */
TRAP_NOERR handler_spurious, 15     /* 假中断 */
TRAP_NOERR handler_fperr,    16     /* #MF 浮点错误 */
TRAP_ERR   handler_align,    17     /* #AC 对齐检查 */
TRAP_NOERR handler_mchk,     18     /* #MC 机器检查 */
TRAP_NOERR handler_simderr,  19     /* #XF SIMD错误 */

/*---------------------------------------------------------------------------
 * IRQ 中断处理入口 (32-255)
 * 由 gen_irq_vectors.py 自动生成到 kernel/irq_vectors.S 中
 *---------------------------------------------------------------------------*/