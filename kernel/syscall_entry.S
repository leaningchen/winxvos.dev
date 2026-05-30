/*===========================================================================
 * kernel/syscall_entry.S — SYSCALL/SYSRET 系统调用入口
 *
 * x86-64 SYSCALL 机制：
 *   - 用户态执行 syscall 指令
 *   - CPU 自动保存：RCX ← 用户 RIP，R11 ← 用户 RFLAGS
 *   - CPU 自动切换：CS/SS ← STAR MSR，RIP ← LSTAR MSR
 *   - CPU 不切换 RSP，必须手动切换到内核栈
 *
 * 内核栈切换方案（使用 swapgs）：
 *   per-CPU 结构的 GS base 存储内核栈顶（syscall_init 写入 GS_BASE MSR）。
 *   进入 syscall 后：swapgs → %gs:0 = 内核 rsp0 → 切换栈 → swapgs 还原。
 *
 * 简化方案（当前实现）：
 *   使用全局变量 g_syscall_kstack 存储当前进程的内核栈顶，
 *   syscall_entry 直接读取它。单核下安全；SMP 后需改为 per-CPU。
 *
 * trapframe 布局（与 struct trapframe 字段顺序一致，参见 idt.h）：
 *   [手动推入] ss, rsp, rflags, cs, rip, err(0), trapno(0x80)
 *   [pushq]    rax, rcx, rdx, rbx, rbp, rsi, rdi, r8, r9, r10, r11, r12-r15
 *===========================================================================*/

#define KERN_DATA_SEL   0x20
#define USER_CODE_SEL   0x2B    /* 0x28 | 3 */
#define USER_DATA_SEL   0x33    /* 0x30 | 3 */
#define T_SYSCALL       0x80

    .text
    .globl syscall_entry
    .type  syscall_entry, @function

/*---------------------------------------------------------------------------
 * syscall_entry — LSTAR MSR 指向此处
 *
 * 进入时：
 *   RSP = 用户栈（未切换！）
 *   RCX = 用户 RIP
 *   R11 = 用户 RFLAGS（但被我们临时用来保存 RSP，稍后从 tf 恢复）
 *   RAX = 系统调用号
 *---------------------------------------------------------------------------*/
syscall_entry:
    /* 1. 保存用户 RSP，切换到进程内核栈
     *
     *    g_syscall_kstack 是一个全局变量，由 uvmswitch() 在进程切换时
     *    写入当前进程的 kstack + KSTACKSIZE（即内核栈顶）。
     */
    movq    %rsp, %r11              /* 暂存用户 RSP（r11 此时保存用户 rflags，先挪用）*/
    movabsq $g_syscall_kstack, %rsp /* 加载全局变量地址（64位绝对寻址）*/
    movq    (%rsp), %rsp            /* rsp = *g_syscall_kstack = 内核栈顶 */

    /* 2. 在内核栈上手动构建 trapframe 的硬件保存部分
     *    （SYSCALL 不自动推入 SS/RSP/RFLAGS/CS/RIP）
     */
    pushq   $USER_DATA_SEL          /* ss  */
    pushq   %r11                    /* rsp = 用户 rsp（刚才存入 r11）*/
    pushq   $0x202                  /* rflags = IF|reserved（用户 rflags 已丢失）*/
    pushq   $USER_CODE_SEL          /* cs  */
    pushq   %rcx                    /* rip = 用户 RIP（syscall 保存到 rcx）*/

    /* err=0, trapno=T_SYSCALL */
    pushq   $0
    pushq   $T_SYSCALL

    /* 3. 保存通用寄存器（顺序与 struct trapframe 完全一致）*/
    pushq   %rax    /* rax（系统调用号，也是返回值）*/
    pushq   %rcx    /* rcx（用户 rip 副本）*/
    pushq   %rdx    /* rdx（arg3）*/
    pushq   %rbx
    pushq   %rbp
    pushq   %rsi    /* rsi（arg2）*/
    pushq   %rdi    /* rdi（arg1）*/
    pushq   %r8     /* r8 （arg5）*/
    pushq   %r9     /* r9 （arg6）*/
    pushq   %r10    /* r10（arg4）*/
    pushq   $0      /* r11（已被 syscall 覆盖，不可恢复，填 0）*/
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    /* 4. 设置内核数据段 */
    movw    $KERN_DATA_SEL, %ax
    movw    %ax, %ds
    movw    %ax, %es

    /* 5. 调用 C 分发函数：syscall_dispatch(struct trapframe *tf)
     *    栈对齐：当前 RSP 指向 trapframe 顶部；call 压入8字节返回地址
     *    需要保证调用前 RSP ≡ 0 mod 16（ABI 要求 RSP+8 ≡ 0 mod 16 在 call 后）
     */
    movq    %rsp, %rdi              /* arg1 = trapframe 指针 */
    pushq   %rbp
    movq    %rsp, %rbp
    andq    $~0xF, %rsp
    call    syscall_dispatch
    movq    %rbp, %rsp
    popq    %rbp

    /* 6. 恢复寄存器并通过 iretq 返回用户态
     *    trapframe->rax 已被 syscall_dispatch 填写为返回值
     */
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %r11
    popq    %r10
    popq    %r9
    popq    %r8
    popq    %rdi
    popq    %rsi
    popq    %rbp
    popq    %rbx
    popq    %rdx
    popq    %rcx
    popq    %rax            /* rax = 系统调用返回值 */

    addq    $16, %rsp       /* 跳过 trapno + err */

    iretq                   /* 弹出 rip/cs/rflags/rsp/ss，返回用户态 */

/*---------------------------------------------------------------------------
 * g_syscall_kstack — 当前进程内核栈顶的指针
 * 在 vm.c 的 uvmswitch() 中更新，此处声明为外部变量
 *---------------------------------------------------------------------------*/
    .extern g_syscall_kstack
