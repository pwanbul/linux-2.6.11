/*
 * i386 semaphore implementation.
 *
 * (C) Copyright 1999 Linus Torvalds
 *
 * Portions Copyright 1999 Red Hat, Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * rw semaphores implemented November 1999 by Benjamin LaHaise <bcrl@redhat.com>
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/init.h>
#include <asm/semaphore.h>

/*
 * 信号量是使用双向计数器实现的：
 * 		“count”变量对于尝试获取信号量的每个进程递减，
 * 		而“sleeping”变量是此类获取的计数。
 *
 * 值得注意的是，内联的“up()”和“down()”函数可以有效地
 * 测试它们是否需要做任何额外的工作(只有在递增操作之前计数为负时，up 才需要做一些事情。)
 *
 * “休眠”和争用例程排序受信号量等待队列头中的自旋锁保护。
 *
 * 请注意，这些函数仅在锁存在争用时调用，
 * 因此所有这些都是整个信号量业务的“non-critical”部分。
 * critical部分是 <asm/semaphore.h> 中的内联内容，
 * 我们希望避免任何额外的跳转和调用。
 */

/*
 * Logic:
 *  - 只有在边界条件下，我们才需要关心。当我们从负数变为非负数时，我们会唤醒人们。
 *  - 当我们从非负计数变为负计数时，我们是否
 *    (a) 与“睡眠者”计数同步，以及
 *    (b) 确保我们在同步之前位于唤醒列表中，以便我们不会丢失唤醒事件。
 */

fastcall void __up(struct semaphore *sem)
{
	wake_up(&sem->wait);
}

fastcall void __sched __down(struct semaphore * sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);		// 静态初始化一个等待队列
	unsigned long flags;

	tsk->state = TASK_UNINTERRUPTIBLE;
	spin_lock_irqsave(&sem->wait.lock, flags);		// 加自旋锁并保存本地IF
	add_wait_queue_exclusive_locked(&sem->wait, &wait);		// 加入等待队列队尾，排他唤醒

	sem->sleepers++;			// 睡眠进程加1
	for (;;) {
		int sleepers = sem->sleepers;

		/*
		 * 将“其他人”添加到其中。他们没有在玩，因为我们拥有 wait_queue_head 中的自旋锁。
		 */
		if (!atomic_add_negative(sleepers - 1, &sem->count)) {
			sem->sleepers = 0;
			break;
		}
		sem->sleepers = 1;	/* us - see -1 above */
		spin_unlock_irqrestore(&sem->wait.lock, flags);

		schedule();

		spin_lock_irqsave(&sem->wait.lock, flags);
		tsk->state = TASK_UNINTERRUPTIBLE;
	}
	remove_wait_queue_locked(&sem->wait, &wait);		// 等待队列中移除
	wake_up_locked(&sem->wait);		// 唤醒下一个进程
	spin_unlock_irqrestore(&sem->wait.lock, flags);
	tsk->state = TASK_RUNNING;
}

fastcall int __sched __down_interruptible(struct semaphore * sem)
{
	int retval = 0;
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	unsigned long flags;

	tsk->state = TASK_INTERRUPTIBLE;
	spin_lock_irqsave(&sem->wait.lock, flags);
	add_wait_queue_exclusive_locked(&sem->wait, &wait);

	sem->sleepers++;
	for (;;) {
		int sleepers = sem->sleepers;

		/*
		 * 当信号挂起时，这会变成 trylock 失败的情况
		 * ——我们不会休眠，我们无法获得锁，因为它有争用。
		 * 只需更正计数并退出即可。
		 *
		 * 进入睡眠之前必须再检查一个是否未决信号
		 */
		if (signal_pending(current)) {
			retval = -EINTR;
			sem->sleepers = 0;
			atomic_add(sleepers, &sem->count);
			break;
		}

		/*
		 * Add "everybody else" into it. They aren't
		 * playing, because we own the spinlock in
		 * wait_queue_head. The "-1" is because we're
		 * still hoping to get the semaphore.
		 */
		if (!atomic_add_negative(sleepers - 1, &sem->count)) {
			sem->sleepers = 0;
			break;
		}
		sem->sleepers = 1;	/* us - see -1 above */
		spin_unlock_irqrestore(&sem->wait.lock, flags);

		schedule();

		spin_lock_irqsave(&sem->wait.lock, flags);
		tsk->state = TASK_INTERRUPTIBLE;
	}
	remove_wait_queue_locked(&sem->wait, &wait);
	wake_up_locked(&sem->wait);
	spin_unlock_irqrestore(&sem->wait.lock, flags);

	tsk->state = TASK_RUNNING;
	return retval;
}

/*
 * Trylock 失败 - 确保我们更正减少了计数。
 *
 * 我们本可以在没有失败案例的情况下使用
 * 单个“cmpxchg”完成 trylock，
 * 但是它在 386 上不起作用。
 */
fastcall int __down_trylock(struct semaphore * sem)
{
	int sleepers;
	unsigned long flags;

	spin_lock_irqsave(&sem->wait.lock, flags);
	sleepers = sem->sleepers + 1;
	sem->sleepers = 0;

	/*
	 * 将“其他人”和我们加入其中。他们没有在玩，
	 * 因为我们拥有 wait_queue_head 中的自旋锁。
	 */
	if (!atomic_add_negative(sleepers, &sem->count)) {
		wake_up_locked(&sem->wait);
	}

	spin_unlock_irqrestore(&sem->wait.lock, flags);
	return 1;
}


/*
 * 信号量操作有一个特殊的调用序列，允许我们对它们进行更简单的内联版本。
 * 当信号量存在争用时，这些例程需要将该序列转换回 C 序列。
 *
 * %eax 包含进入时的信号量指针。
 * 保存 C-clobbered 寄存器（%eax、%edx 和 %ecx），
 * 除了 %eax 是返回值或只是 clobbered..
 */
asm(
".section .sched.text\n"
".align 4\n"
".globl __down_failed\n"
"__down_failed:\n\t"
#if defined(CONFIG_FRAME_POINTER)
	"pushl %ebp\n\t"
	"movl  %esp,%ebp\n\t"
#endif
	"pushl %edx\n\t"
	"pushl %ecx\n\t"
	"call __down\n\t"
	"popl %ecx\n\t"
	"popl %edx\n\t"
#if defined(CONFIG_FRAME_POINTER)
	"movl %ebp,%esp\n\t"
	"popl %ebp\n\t"
#endif
	"ret"
);

asm(
".section .sched.text\n"
".align 4\n"
".globl __down_failed_interruptible\n"
"__down_failed_interruptible:\n\t"
#if defined(CONFIG_FRAME_POINTER)
	"pushl %ebp\n\t"
	"movl  %esp,%ebp\n\t"
#endif
	"pushl %edx\n\t"
	"pushl %ecx\n\t"
	"call __down_interruptible\n\t"
	"popl %ecx\n\t"
	"popl %edx\n\t"
#if defined(CONFIG_FRAME_POINTER)
	"movl %ebp,%esp\n\t"
	"popl %ebp\n\t"
#endif
	"ret"
);

asm(
".section .sched.text\n"
".align 4\n"
".globl __down_failed_trylock\n"
"__down_failed_trylock:\n\t"
#if defined(CONFIG_FRAME_POINTER)
	"pushl %ebp\n\t"
	"movl  %esp,%ebp\n\t"
#endif
	"pushl %edx\n\t"
	"pushl %ecx\n\t"
	"call __down_trylock\n\t"
	"popl %ecx\n\t"
	"popl %edx\n\t"
#if defined(CONFIG_FRAME_POINTER)
	"movl %ebp,%esp\n\t"
	"popl %ebp\n\t"
#endif
	"ret"
);

asm(
".section .sched.text\n"
".align 4\n"
".globl __up_wakeup\n"
"__up_wakeup:\n\t"
	"pushl %edx\n\t"
	"pushl %ecx\n\t"
	"call __up\n\t"
	"popl %ecx\n\t"
	"popl %edx\n\t"
	"ret"
);

/*
 * rw spinlock fallbacks
 */
#if defined(CONFIG_SMP)
asm(
".section .sched.text\n"
".align	4\n"
".globl	__write_lock_failed\n"
"__write_lock_failed:\n\t"
	LOCK "addl	$" RW_LOCK_BIAS_STR ",(%eax)\n"
"1:	rep; nop\n\t"
	"cmpl	$" RW_LOCK_BIAS_STR ",(%eax)\n\t"
	"jne	1b\n\t"
	LOCK "subl	$" RW_LOCK_BIAS_STR ",(%eax)\n\t"
	"jnz	__write_lock_failed\n\t"
	"ret"
);

asm(
".section .sched.text\n"
".align	4\n"
".globl	__read_lock_failed\n"
"__read_lock_failed:\n\t"
	LOCK "incl	(%eax)\n"
"1:	rep; nop\n\t"
	"cmpl	$1,(%eax)\n\t"
	"js	1b\n\t"
	LOCK "decl	(%eax)\n\t"
	"js	__read_lock_failed\n\t"
	"ret"
);
#endif
