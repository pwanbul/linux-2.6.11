/* thread_info.h: i386 low-level thread information
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/page.h>

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#endif

/*
 * entry.S 需要立即访问的低级任务数据
 * - 这个结构应该完全适合一个缓存行
 * - 此结构共享主管堆栈页面
 * - 如果这个结构的内容改变了，汇编常量也必须改变
 */
#ifndef __ASSEMBLY__
// 线程描述符
struct thread_info {
	struct task_struct	*task;		/* 指向task_struct指针 */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	unsigned long		status;		/* thread-synchronous flags */
	__u32			cpu;		/* current CPU */
	// 见preempt_mask.h
	__s32			preempt_count; /* 0 => preemptable, <0 => BUG 抢占/中断相关计数器 */


	mm_segment_t		addr_limit;	/* 当前可访问的地址空间范围，thread address space:
					 	   0-0xBFFFFFFF for user-thead
						   0-0xFFFFFFFF for kernel-thread
						   access_ok()使用，set_fs()设置
						*/
	struct restart_block    restart_block;

	unsigned long           previous_esp;   /* ESP of the previous stack in case
						   of nested (IRQ) stacks
						*/
	__u8			supervisor_stack[0];
};

#else /* !__ASSEMBLY__ */

#include <asm/asm_offsets.h>

#endif

#define PREEMPT_ACTIVE		0x10000000
#ifdef CONFIG_4KSTACKS
#define THREAD_SIZE            (4096)
#else
#define THREAD_SIZE		(8192)      // 进程内核栈8K
#endif

#define STACK_WARN             (THREAD_SIZE/8)
/*
 * macros/functions for gaining access to the thread information structure
 *
 * preempt_count needs to be 1 initially, until the scheduler is functional.
 */
#ifndef __ASSEMBLY__

// init_task的线程描述符
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.exec_domain	= &default_exec_domain,	\
	.flags		= 0,			\
	.cpu		= 0,			\
	.preempt_count	= 1,			\
	.addr_limit	= KERNEL_DS,		\
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)


/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
	__asm__("andl %%esp,%0; ":"=r" (ti) : "0" (~(THREAD_SIZE - 1)));
	return ti;
}

/* how to get the current stack pointer from C */
register unsigned long current_stack_pointer asm("esp") __attribute_used__;

/* thread information allocation */
#ifdef CONFIG_DEBUG_STACK_USAGE
#define alloc_thread_info(tsk)					\
	({							\
		struct thread_info *ret;			\
								\
		ret = kmalloc(THREAD_SIZE, GFP_KERNEL);		\
		if (ret)					\
			memset(ret, 0, THREAD_SIZE);		\
		ret;						\
	})
#else
#define alloc_thread_info(tsk) kmalloc(THREAD_SIZE, GFP_KERNEL)
#endif

#define free_thread_info(info)	kfree(info)
#define get_thread_info(ti) get_task_struct((ti)->task)
#define put_thread_info(ti) put_task_struct((ti)->task)

#else /* !__ASSEMBLY__ */

/* 如何从 ASM 获取线程信息结构
 * 获取线程描述符thread_info的指针
 * */
#define GET_THREAD_INFO(reg) \
	movl $-THREAD_SIZE, reg; \
	andl %esp, reg

/* use this one if reg already contains %esp */
#define GET_THREAD_INFO_WITH_ESP(reg) \
	andl $-THREAD_SIZE, reg

#endif

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_SYSCALL_TRACE	0	/* 进程被跟踪 syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* 进程有未决信号 signal pending */
#define TIF_NEED_RESCHED	3	/* 进程可以被抢占 rescheduling necessary */
#define TIF_SINGLESTEP		4	/* 返回用户空间时恢复单步执行 restore singlestep on return to user mode */
#define TIF_IRET		5	/* return with iret */
#define TIF_SYSCALL_AUDIT	7	/* 进程被审计 syscall auditing active */
#define TIF_POLLING_NRFLAG	16	/* true if poll_idle() is polling TIF_NEED_RESCHED */
#define TIF_MEMDIE		17

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_SINGLESTEP		(1<<TIF_SINGLESTEP)
#define _TIF_IRET		(1<<TIF_IRET)
#define _TIF_SYSCALL_AUDIT	(1<<TIF_SYSCALL_AUDIT)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)

/* work to do on interrupt/exception return */
#define _TIF_WORK_MASK \
  (0x0000FFFF & ~(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT|_TIF_SINGLESTEP))
#define _TIF_ALLWORK_MASK	0x0000FFFF	/* 返回 u-space 时要做的工作 */

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 */
#define TS_USEDFPU		0x0001	/* FPU was used by this task this quantum (SMP) */

#endif /* __KERNEL__ */

#endif /* _ASM_THREAD_INFO_H */
