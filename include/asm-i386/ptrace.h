#ifndef _I386_PTRACE_H
#define _I386_PTRACE_H

#define EBX 0
#define ECX 1
#define EDX 2
#define ESI 3
#define EDI 4
#define EBP 5
#define EAX 6
#define DS 7
#define ES 8
#define FS 9
#define GS 10
#define ORIG_EAX 11
#define EIP 12
#define CS  13
#define EFL 14
#define UESP 15
#define SS   16
#define FRAME_SIZE 17

/* 这个结构体定义了系统调用期间寄存器在堆栈上的存储方式。 */

struct pt_regs {
	long ebx;       // 0
	long ecx;       // 1
	long edx;       // 2
	long esi;       // 3
	long edi;       // 4
	long ebp;       // 5
	long eax;       // 6
	int  xds;       // 7
	int  xes;       // 8
	long orig_eax;       // 0
	long eip;
	int  xcs;
	long eflags;
	long esp;
	int  xss;
};

/* Arbitrarily choose the same ptrace numbers as used by the Sparc code. */
#define PTRACE_GETREGS            12
#define PTRACE_SETREGS            13
#define PTRACE_GETFPREGS          14
#define PTRACE_SETFPREGS          15
#define PTRACE_GETFPXREGS         18
#define PTRACE_SETFPXREGS         19

#define PTRACE_OLDSETOPTIONS         21

#define PTRACE_GET_THREAD_AREA    25
#define PTRACE_SET_THREAD_AREA    26

#ifdef __KERNEL__
struct task_struct;
extern void send_sigtrap(struct task_struct *tsk, struct pt_regs *regs, int error_code);
#define user_mode(regs) ((VM_MASK & (regs)->eflags) || (3 & (regs)->xcs))
#define instruction_pointer(regs) ((regs)->eip)
#if defined(CONFIG_SMP) && defined(CONFIG_FRAME_POINTER)
extern unsigned long profile_pc(struct pt_regs *regs);
#else
#define profile_pc(regs) instruction_pointer(regs)
#endif
#endif

#endif
