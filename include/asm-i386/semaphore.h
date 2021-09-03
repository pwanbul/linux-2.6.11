#ifndef _I386_SEMAPHORE_H
#define _I386_SEMAPHORE_H

#include <linux/linkage.h>

#ifdef __KERNEL__

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * Modified 1996-12-23 by Dave Grothe <dave@gcom.com> to fix bugs in
 *                     the original code and to make semaphore waits
 *                     interruptible so that processes waiting on
 *                     semaphores can be killed.
 * Modified 1999-02-14 by Andrea Arcangeli, split the sched.c helper
 *		       functions in asm/sempahore-helper.h while fixing a
 *		       potential and subtle race discovered by Ulrich Schmid
 *		       in down_interruptible(). Since I started to play here I
 *		       also implemented the `trylock' semaphore operation.
 *          1999-07-02 Artur Skawina <skawina@geocities.com>
 *                     Optimized "0(ecx)" -> "(ecx)" (the assembler does not
 *                     do this). Changed calling sequences from push/jmp to
 *                     traditional call/ret.
 * Modified 2001-01-01 Andreas Franck <afranck@gmx.de>
 *		       Some hacks to ensure compatibility with recent
 *		       GCC snapshots, to avoid stack corruption when compiling
 *		       with -fomit-frame-pointer. It's not sure if this will
 *		       be fixed in GCC, as our previous implementation was a
 *		       bit dubious.
 *
 * If you would like to see an analysis of this implementation, please
 * ftp to gcom.com and download the file
 * /pub/linux/src/semaphore/semaphore-2.0.24.tar.gz.
 *
 */

#include <asm/system.h>
#include <asm/atomic.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

// 信号量实现
struct semaphore {
	/* count统计量，表示资源的数量，
	 * 大于0时表示资源还未占用完，
	 * 等于0时表示资源已经占用完但还没有等待的进程，
	 * 小于0时表示资源不可用，并且有进程在等待
	 * */
	atomic_t count;
	int sleepers;		// 取值为0和1，0表示没有进程等待，1表示有进程等待
	wait_queue_head_t wait;		// 等待进程的队列
};


#define __SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.count		= ATOMIC_INIT(n),				\
	.sleepers	= 0,						\
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
}

#define __MUTEX_INITIALIZER(name) \
	__SEMAPHORE_INITIALIZER(name,1)

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

// 二值信号量(互斥信号量)
#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)
#define DECLARE_MUTEX_LOCKED(name) __DECLARE_SEMAPHORE_GENERIC(name,0)

// 动态初始化信号量
static inline void sema_init (struct semaphore *sem, int val)
{
/*
 *	*sem = (struct semaphore)__SEMAPHORE_INITIALIZER((*sem),val);
 *
 * i'd rather use the more flexible initialization above, but sadly
 * GCC 2.7.2.3 emits a bogus warning. EGCS doesn't. Oh well.
 */
	atomic_set(&sem->count, val);
	sem->sleepers = 0;
	init_waitqueue_head(&sem->wait);
}

// 初始化空闲信号量
static inline void init_MUTEX (struct semaphore *sem)
{
	sema_init(sem, 1);
}

// 初始化忙的信号量
static inline void init_MUTEX_LOCKED (struct semaphore *sem)
{
	sema_init(sem, 0);
}

fastcall void __down_failed(void /* special register calling convention */);
fastcall int  __down_failed_interruptible(void  /* params in registers */);
fastcall int  __down_failed_trylock(void  /* params in registers */);
fastcall void __up_wakeup(void /* special register calling convention */);

fastcall void __down(struct semaphore * sem);
fastcall int  __down_interruptible(struct semaphore * sem);
fastcall int  __down_trylock(struct semaphore * sem);
fastcall void __up(struct semaphore * sem);

/*
 * 这很丑陋，但我们希望默认情况下通过。
 * “__down_failed”是一个特殊的asm处理程序，
 * 它调用实际等待的C例程。
 * 参见 arch/i386/kernel/semaphore.c
 *
 * 加锁信号量
 */
static inline void down(struct semaphore * sem)
{
	might_sleep();
	__asm__ __volatile__(
		"# atomic down operation\n\t"
		LOCK "decl %0\n\t"     /* --sem->count 只有这一行代码是临界区*/
		"js 2f\n"			// 变成负数则跳转到2f
		"1:\n"		// 下面没有指令了
		LOCK_SECTION_START("")
		"2:\tlea %0,%%eax\n\t"			// fastcall
		"call __down_failed\n\t"
		"jmp 1b\n"
		LOCK_SECTION_END
		:"=m" (sem->count)
		:
		:"memory","ax");
}

/*
 * 可中断尝试获取信号量。如果我们得到它，返回零。如果我们被打断，返回 -EINTR
 */
static inline int down_interruptible(struct semaphore * sem)
{
	int result;

	might_sleep();
	__asm__ __volatile__(
		"# atomic interruptible down operation\n\t"
		LOCK "decl %1\n\t"     /* --sem->count */
		"js 2f\n\t"
		"xorl %0,%0\n"
		"1:\n"
		LOCK_SECTION_START("")
		"2:\tlea %1,%%eax\n\t"
		"call __down_failed_interruptible\n\t"
		"jmp 1b\n"
		LOCK_SECTION_END
		:"=a" (result), "=m" (sem->count)
		:
		:"memory");
	return result;
}

/*
 * 非阻塞地尝试 down() 一个信号量。如果我们获得它，则返回零
 */
static inline int down_trylock(struct semaphore * sem)
{
	int result;

	__asm__ __volatile__(
		"# atomic interruptible down operation\n\t"
		LOCK "decl %1\n\t"     /* --sem->count */
		"js 2f\n\t"
		"xorl %0,%0\n"
		"1:\n"
		LOCK_SECTION_START("")
		"2:\tlea %1,%%eax\n\t"
		"call __down_failed_trylock\n\t"
		"jmp 1b\n"
		LOCK_SECTION_END
		:"=a" (result), "=m" (sem->count)
		:
		:"memory");
	return result;
}

/*
 * 笔记！这是微妙的。仅当信号量为负时（== 有人在等待），
 * 我们才会跳转唤醒人们。
 * 默认情况下（无争用）将导致 down() 和 up() 都没有跳转。
 *
 * 释放信号量
 */
static inline void up(struct semaphore * sem)
{
	__asm__ __volatile__(
		"# atomic up operation\n\t"
		LOCK "incl %0\n\t"     /* ++sem->count */
		"jle 2f\n"		// 小于等于0，即ZF==1，但从负数变成0才会唤醒
		"1:\n"
		LOCK_SECTION_START("")
		"2:\tlea %0,%%eax\n\t"
		"call __up_wakeup\n\t"
		"jmp 1b\n"
		LOCK_SECTION_END
		".subsection 0\n"
		:"=m" (sem->count)
		:
		:"memory","ax");
}

#endif
#endif
