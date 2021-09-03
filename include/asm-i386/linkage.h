#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

/* 这个标志符和函数声明放在一起，带regparm(0)的属性声明告诉gcc编译器，该函数不需要通过任何寄存器来传递参数，参数只是通过堆栈来传递。
 * gcc编译器在汇编过程中调用c语言函数时传递参数有两种方法：一种是通过堆栈，另一种是通过寄存器。
 * 缺省时采用寄存器，假如你要在你的汇编过程中调用c语言函数，并且想通过堆栈传递参数，你定义的 c 函数时要在函数前加上宏asmlinkage。
 * */
#define asmlinkage CPP_ASMLINKAGE __attribute__((regparm(0)))
#define FASTCALL(x)	x __attribute__((regparm(3)))
#define fastcall	__attribute__((regparm(3)))         // 最多可以使用n个寄存器（eax, edx, ecx）传递参数，n的范围是0~3

#ifdef CONFIG_REGPARM
# define prevent_tail_call(ret) __asm__ ("" : "=r" (ret) : "0" (ret))
#endif

#ifdef CONFIG_X86_ALIGNMENT_16
#define __ALIGN .align 16,0x90
#define __ALIGN_STR ".align 16,0x90"
#endif

#endif
