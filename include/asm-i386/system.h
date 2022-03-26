#ifndef __ASM_SYSTEM_H
#define __ASM_SYSTEM_H

#include <linux/config.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/cpufeature.h>
#include <linux/bitops.h> /* for LOCK_PREFIX */

#ifdef __KERNEL__

struct task_struct;	/* one of the stranger aspects of C forward declarations.. */
extern struct task_struct * FASTCALL(__switch_to(struct task_struct *prev, struct task_struct *next));

/* 切换进程内核栈和硬件上下文
 * prev表示被换下的进程，保存在eax中
 * next表示换上的进程，保存在edx中
 * last是输出参数，保存的prve进程的task_struct地址
 * */
#define switch_to(prev,next,last) do {					\
	unsigned long esi,edi;						\
	asm volatile("pushfl\n\t"		/* 保存EFLGAS */		\
		     "pushl %%ebp\n\t"					\
		     "movl %%esp,%0\n\t"	/* 保存prev的ESP */		\
		     "movl %5,%%esp\n\t"	/* 用next的thread.esp恢复ESP，此后在next的内核栈上运行 */	\
		     "movl $1f,%1\n\t"		/* 将$1的地址保存在prve的thread.eip中，当prve恢复时，从$1开始运行 */		\
		     "pushl %6\n\t"		/* 将next的thread.eip，即$1的地址放入next的内核栈 */	\
		     "jmp __switch_to\n"				\
		     "1:\t"						\
		     "popl %%ebp\n\t"		/* prev恢复运行，将之前保存在prve内核栈上的ebp值弹出到ebp里 */	\
		     "popfl"			/* 同上 */			\
		     :"=m" (prev->thread.esp),"=m" (prev->thread.eip),	\
		      "=a" (last),"=S" (esi),"=D" (edi)		/* esi中保存prve中的thread */	\
		     :"m" (next->thread.esp),"m" (next->thread.eip),	\
		      "2" (prev), "d" (next));				\
} while (0)

#define _set_base(addr,base) do { unsigned long __pr; \
__asm__ __volatile__ ("movw %%dx,%1\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %%dl,%2\n\t" \
	"movb %%dh,%3" \
	:"=&d" (__pr) \
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7)), \
         "0" (base) \
        ); } while(0)

#define _set_limit(addr,limit) do { unsigned long __lr; \
__asm__ __volatile__ ("movw %%dx,%1\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %2,%%dh\n\t" \
	"andb $0xf0,%%dh\n\t" \
	"orb %%dh,%%dl\n\t" \
	"movb %%dl,%2" \
	:"=&d" (__lr) \
	:"m" (*(addr)), \
	 "m" (*((addr)+6)), \
	 "0" (limit) \
        ); } while(0)

#define set_base(ldt,base) _set_base( ((char *)&(ldt)) , (base) )
#define set_limit(ldt,limit) _set_limit( ((char *)&(ldt)) , ((limit)-1)>>12 )

static inline unsigned long _get_base(char * addr)
{
	unsigned long __base;
	__asm__("movb %3,%%dh\n\t"
		"movb %2,%%dl\n\t"
		"shll $16,%%edx\n\t"
		"movw %1,%%dx"
		:"=&d" (__base)
		:"m" (*((addr)+2)),
		 "m" (*((addr)+4)),
		 "m" (*((addr)+7)));
	return __base;
}

#define get_base(ldt) _get_base( ((char *)&(ldt)) )

/*
 * Load a segment. Fall back on loading the zero
 * segment if something goes wrong..
 */
#define loadsegment(seg,value)			\
	asm volatile("\n"			\
		"1:\t"				\
		"movl %0,%%" #seg "\n"		\
		"2:\n"				\
		".section .fixup,\"ax\"\n"	\
		"3:\t"				\
		"pushl $0\n\t"			\
		"popl %%" #seg "\n\t"		\
		"jmp 2b\n"			\
		".previous\n"			\
		".section __ex_table,\"a\"\n\t"	\
		".align 4\n\t"			\
		".long 1b,3b\n"			\
		".previous"			\
		: :"m" (*(unsigned int *)&(value)))

/*
 * Save a segment register away
 */
#define savesegment(seg, value) \
	asm volatile("movl %%" #seg ",%0":"=m" (*(int *)&(value)))

/*
 * Clear and set 'TS' bit respectively
 */
#define clts() __asm__ __volatile__ ("clts")
#define read_cr0() ({ \
	unsigned int __dummy; \
	__asm__( \
		"movl %%cr0,%0\n\t" \
		:"=r" (__dummy)); \
	__dummy; \
})
#define write_cr0(x) \
	__asm__("movl %0,%%cr0": :"r" (x));

#define read_cr4() ({ \
	unsigned int __dummy; \
	__asm__( \
		"movl %%cr4,%0\n\t" \
		:"=r" (__dummy)); \
	__dummy; \
})
#define write_cr4(x) \
	__asm__("movl %0,%%cr4": :"r" (x));
#define stts() write_cr0(8 | read_cr0())

#endif	/* __KERNEL__ */

#define wbinvd() \
	__asm__ __volatile__ ("wbinvd": : :"memory");

static inline unsigned long get_limit(unsigned long segment)
{
	unsigned long __limit;
	__asm__("lsll %1,%0"
		:"=r" (__limit):"r" (segment));
	return __limit+1;
}

#define nop() __asm__ __volatile__ ("nop")

#define xchg(ptr,v) ((__typeof__(*(ptr)))__xchg((unsigned long)(v),(ptr),sizeof(*(ptr))))

#define tas(ptr) (xchg((ptr),1))

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((struct __xchg_dummy *)(x))


/*
 * The semantics of XCHGCMP8B are a bit strange, this is why
 * there is a loop and the loading of %%eax and %%edx has to
 * be inside. This inlines well in most cases, the cached
 * cost is around ~38 cycles. (in the future we might want
 * to do an SIMD/3DNOW!/MMX/FPU 64-bit store here, but that
 * might have an implicit FPU-save as a cost, so it's not
 * clear which path to go.)
 *
 * cmpxchg8b must be used with the lock prefix here to allow
 * the instruction to be executed atomically, see page 3-102
 * of the instruction set reference 24319102.pdf. We need
 * the reader side to see the coherent 64bit value.
 */
static inline void __set_64bit (unsigned long long * ptr,
		unsigned int low, unsigned int high)
{
	__asm__ __volatile__ (
		"\n1:\t"
		"movl (%0), %%eax\n\t"
		"movl 4(%0), %%edx\n\t"
		"lock cmpxchg8b (%0)\n\t"
		"jnz 1b"
		: /* no outputs */
		:	"D"(ptr),
			"b"(low),
			"c"(high)
		:	"ax","dx","memory");
}

static inline void __set_64bit_constant (unsigned long long *ptr,
						 unsigned long long value)
{
	__set_64bit(ptr,(unsigned int)(value), (unsigned int)((value)>>32ULL));
}
#define ll_low(x)	*(((unsigned int*)&(x))+0)
#define ll_high(x)	*(((unsigned int*)&(x))+1)

static inline void __set_64bit_var (unsigned long long *ptr,
			 unsigned long long value)
{
	__set_64bit(ptr,ll_low(value), ll_high(value));
}

#define set_64bit(ptr,value) \
(__builtin_constant_p(value) ? \
 __set_64bit_constant(ptr, value) : \
 __set_64bit_var(ptr, value) )

#define _set_64bit(ptr,value) \
(__builtin_constant_p(value) ? \
 __set_64bit(ptr, (unsigned int)(value), (unsigned int)((value)>>32ULL) ) : \
 __set_64bit(ptr, ll_low(value), ll_high(value)) )

/*
 * Note: no "lock" prefix even on SMP: xchg always implies lock anyway
 * Note 2: xchg has side effect, so that attribute volatile is necessary,
 *	  but generally the primitive is invalid, *ptr is output argument. --ANK
 */
static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
		case 1:
			__asm__ __volatile__("xchgb %b0,%1"
				:"=q" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
		case 2:
			__asm__ __volatile__("xchgw %w0,%1"
				:"=r" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
		case 4:
			__asm__ __volatile__("xchgl %0,%1"
				:"=r" (x)
				:"m" (*__xg(ptr)), "0" (x)
				:"memory");
			break;
	}
	return x;
}

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */

#ifdef CONFIG_X86_CMPXCHG
#define __HAVE_ARCH_CMPXCHG 1
#endif

static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	unsigned long prev;
	switch (size) {
	case 1:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgb %b1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	case 2:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgw %w1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	case 4:
		__asm__ __volatile__(LOCK_PREFIX "cmpxchgl %1,%2"
				     : "=a"(prev)
				     : "q"(new), "m"(*__xg(ptr)), "0"(old)
				     : "memory");
		return prev;
	}
	return old;
}

#define cmpxchg(ptr,o,n)\
	((__typeof__(*(ptr)))__cmpxchg((ptr),(unsigned long)(o),\
					(unsigned long)(n),sizeof(*(ptr))))
    
#ifdef __KERNEL__
struct alt_instr { 
	__u8 *instr; 		/* original instruction */
	__u8 *replacement;
	__u8  cpuid;		/* cpuid bit set for replacement */
	__u8  instrlen;		/* length of original instruction */
	__u8  replacementlen; 	/* length of new instruction, <= instrlen */ 
	__u8  pad;
}; 
#endif

/* 
 * Alternative instructions for different CPU types or capabilities.
 * 
 * This allows to use optimized instructions even on generic binary
 * kernels.
 * 
 * length of oldinstr must be longer or equal the length of newinstr
 * It can be padded with nops as needed.
 * 
 * For non barrier like inlines please define new variants
 * without volatile and memory clobber.
 */
// 替换指令，要求被替换的指令比新的指令长，这样如果有空的位置可以放入nop(0x90)操作
// 因为x86的32位CPU有可能不提供mfence、lfence、sfence三条汇编指令的支持，故在不支持mfence的指令中使用："lock; addl $0,0(%%esp)", "mfence"。
#define alternative(oldinstr, newinstr, feature) 	\
	asm volatile ("661:\n\t" oldinstr "\n662:\n" 		     \
		      ".section .altinstructions,\"a\"\n"     	     \
		      "  .align 4\n"				       \
		      "  .long 661b\n"            /* label */          \
		      "  .long 663f\n"		  /* new instruction */ 	\
		      "  .byte %c0\n"             /* feature bit */    \
		      "  .byte 662b-661b\n"       /* sourcelen */      \
		      "  .byte 664f-663f\n"       /* replacementlen */ \
		      ".previous\n"						\
		      ".section .altinstr_replacement,\"ax\"\n"			\
		      "663:\n\t" newinstr "\n664:\n"   /* replacement */    \
		      ".previous" :: "i" (feature) : "memory")  

/*
 * Alternative inline assembly with input.
 * 
 * Pecularities:
 * No memory clobber here. 
 * Argument numbers start with 1.
 * Best is to use constraints that are fixed size (like (%1) ... "r")
 * If you use variable sized constraints like "m" or "g" in the 
 * replacement maake sure to pad to the worst case length.
 */
#define alternative_input(oldinstr, newinstr, feature, input...)		\
	asm volatile ("661:\n\t" oldinstr "\n662:\n"				\
		      ".section .altinstructions,\"a\"\n"			\
		      "  .align 4\n"						\
		      "  .long 661b\n"            /* label */			\
		      "  .long 663f\n"		  /* new instruction */ 	\
		      "  .byte %c0\n"             /* feature bit */		\
		      "  .byte 662b-661b\n"       /* sourcelen */		\
		      "  .byte 664f-663f\n"       /* replacementlen */ 		\
		      ".previous\n"						\
		      ".section .altinstr_replacement,\"ax\"\n"			\
		      "663:\n\t" newinstr "\n664:\n"   /* replacement */ 	\
		      ".previous" :: "i" (feature), ##input)

/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 *
 * For now, "wmb()" doesn't actually do anything, as all
 * Intel CPU's follow what Intel calls a *Processor Order*,
 * in which all writes are seen in the program order even
 * outside the CPU.
 *
 * I expect future Intel CPU's to have a weaker ordering,
 * but I'd also expect them to finally get their act together
 * and add some real memory barriers if so.
 *
 * Some non intel clones support out of order store. wmb() ceases to be a
 * nop for these.
 */
 

/* 
 * Actually only lfence would be needed for mb() because all stores done 
 * by the kernel should be already ordered. But keep a full barrier for now. 
 */

#define mb() alternative("lock; addl $0,0(%%esp)", "mfence", X86_FEATURE_XMM2)	/* Streaming SIMD Extensions-2 */
#define rmb() alternative("lock; addl $0,0(%%esp)", "lfence", X86_FEATURE_XMM2)

/**
 * read_barrier_depends - Flush all pending reads that subsequents reads
 * depend on.
 *
 * No data-dependent reads from memory-like regions are ever reordered
 * over this barrier.  All reads preceding this primitive are guaranteed
 * to access memory (but not necessarily other CPUs' caches) before any
 * reads following this primitive that depend on the data return by
 * any of the preceding reads.  This primitive is much lighter weight than
 * rmb() on most CPUs, and is never heavier weight than is
 * rmb().
 *
 * These ordering constraints are respected by both the local CPU
 * and the compiler.
 *
 * Ordering is not guaranteed by anything other than these primitives,
 * not even by data dependencies.  See the documentation for
 * memory_barrier() for examples and URLs to more information.
 *
 * For example, the following code would force ordering (the initial
 * value of "a" is zero, "b" is one, and "p" is "&a"):
 *
 * <programlisting>
 *	CPU 0				CPU 1
 *
 *	b = 2;
 *	memory_barrier();
 *	p = &b;				q = p;
 *					read_barrier_depends();
 *					d = *q;
 * </programlisting>
 *
 * because the read of "*q" depends on the read of "p" and these
 * two reads are separated by a read_barrier_depends().  However,
 * the following code, with the same initial values for "a" and "b":
 *
 * <programlisting>
 *	CPU 0				CPU 1
 *
 *	a = 2;
 *	memory_barrier();
 *	b = 3;				y = b;
 *					read_barrier_depends();
 *					x = a;
 * </programlisting>
 *
 * does not enforce ordering, since there is no data dependency between
 * the read of "a" and the read of "b".  Therefore, on some CPUs, such
 * as Alpha, "y" could be set to 3 and "x" to 0.  Use rmb()
 * in cases like thiswhere there are no data dependencies.
 **/

#define read_barrier_depends()	do { } while(0)
/**
内存屏障的分类
硬件层提供了一系列的内存屏障 memory barrier / memory fence(Intel的提法)来提供一致性的能力。拿X86平台来说，有几种主要的内存屏障
1. lfence，是一种Load Barrier 读屏障。在读指令前插入读屏障，可以让高速缓存中的数据失效，重新从主内存加载数据
2. sfence, 是一种Store Barrier 写屏障。在写指令之后插入写屏障，能让写入缓存的最新数据写回到主内存
3. mfence, 是一种全能型的屏障，具备ifence和sfence的能力
4. Lock前缀，Lock不是一种内存屏障，但是它能完成类似内存屏障的功能。Lock会对CPU总线和高速缓存加锁，可以理解为CPU指令级的一种锁。
它后面可以跟ADD, ADC, AND, BTC, BTR, BTS, CMPXCHG, CMPXCH8B, DEC, INC, NEG, NOT, OR, SBB, SUB, XOR, XADD, and XCHG等指令。
Lock前缀实现了类似的能力.
1. 它先对总线/缓存加锁，然后执行后面的指令，最后释放锁后会把高速缓存中的脏数据全部刷新回主内存。
2. 在Lock锁住总线的时候，其他CPU的读写请求都会被阻塞，直到锁释放。Lock后的写操作会让其他CPU相关的cache line失效，从而从新从内存加载最新的数据。这个是通过缓存一致性协议做的。
*/

/**
Intel和AMD都没有在IA32 CPU中实现乱续写(Out-Of-Order Store)，所以wmb()定义为空操作，不约束CPU行为；但
有些IA32 CPU厂商实现了OOO Store，所以就有了使用sfence的那个wmb()实现。
 */
#ifdef CONFIG_X86_OOSTORE		// Out-Of-Order Store
/* Actually there are no OOO store capable CPUs for now that do SSE, 
   but make it already an possibility. */
#define wmb() alternative("lock; addl $0,0(%%esp)", "sfence", X86_FEATURE_XMM)		/* Streaming SIMD Extensions */
#else
#define wmb()	__asm__ __volatile__ ("": : :"memory")
#endif

#ifdef CONFIG_SMP
// 内存屏障
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#define set_mb(var, value) do { xchg(&var, value); } while (0)
#else
// UP上内存屏障退化成优化屏障，仅仅约束编译器的重排列即可
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while(0)
#define set_mb(var, value) do { var = value; barrier(); } while (0)
#endif

#define set_wmb(var, value) do { var = value; wmb(); } while (0)

/* interrupt control.. */
// 获取IF的状态
#define local_save_flags(x)	do { typecheck(unsigned long,x); __asm__ __volatile__("pushfl ; popl %0":"=g" (x): /* no input */); } while (0)
// 把变量x中保存的值恢复到IF中
#define local_irq_restore(x) 	do { typecheck(unsigned long,x); __asm__ __volatile__("pushl %0 ; popfl": /* no output */ :"g" (x):"memory", "cc"); } while (0)
// 关闭本地中断
#define local_irq_disable() 	__asm__ __volatile__("cli": : :"memory")
// 开启本地中断
#define local_irq_enable()	__asm__ __volatile__("sti": : :"memory")
/* used in the idle loop; sti takes one instruction cycle to complete */
#define safe_halt()		__asm__ __volatile__("sti; hlt": : :"memory")

/* ELFAGS中IF在第9位(从0开始算)
 * IF为0时 关闭中断
 * IF为1时 打开中断
 * */
#define irqs_disabled()			\
({					\
	unsigned long flags;		\
	local_save_flags(flags);	\
	!(flags & (1<<9));		\
})

/* For spinlocks etc
 * 把EFLAGS保存下来，关闭(清除)本地中断
 * */
#define local_irq_save(x)	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=g" (x): /* no input */ :"memory")

/*
 * disable hlt during certain critical i/o operations
 */
#define HAVE_DISABLE_HLT
void disable_hlt(void);
void enable_hlt(void);

extern int es7000_plat;
void cpu_idle_wait(void);

#endif
