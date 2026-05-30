/*===========================================================================
 * kernel/swtch.S — 64位内核上下文切换
 *
 * void swtch(struct context **old, struct context *new);
 *
 * 将当前寄存器保存到栈上构成 struct context，把栈指针保存到 *old，
 * 再切换到 new 所指向的 context 并恢复寄存器，最后 ret 跳转到
 * context->rip（即 forkret 或 scheduler）。
 *
 * x86-64 System V ABI：
 *   参数1 (old) = %rdi
 *   参数2 (new) = %rsi
 *
 * struct context 布局（从低地址到高地址，与 push 顺序对应）：
 *   +0x00  r15     <- 最先 push，栈最高
 *   +0x08  r14
 *   +0x10  r13
 *   +0x18  r12
 *   +0x20  rbx
 *   +0x28  rbp
 *   +0x30  rip     <- 由 call 指令隐式 push 或由 forkret 设置
 *===========================================================================*/

    .text
    .globl swtch
    .type  swtch, @function

swtch:
    /* 保存调用方的 callee-saved 寄存器到当前栈，构成 struct context */
    pushq   %r15
    pushq   %r14
    pushq   %r13
    pushq   %r12
    pushq   %rbx
    pushq   %rbp

    /* 保存当前栈指针到 *old（%rdi 为第一个参数：struct context **old）*/
    movq    %rsp, (%rdi)

    /* 切换到新栈（%rsi 为第二个参数：struct context *new）*/
    movq    %rsi, %rsp

    /* 从新 context 恢复 callee-saved 寄存器 */
    popq    %rbp
    popq    %rbx
    popq    %r12
    popq    %r13
    popq    %r14
    popq    %r15

    /* ret 弹出 context->rip，跳转到 forkret 或 scheduler */
    ret
