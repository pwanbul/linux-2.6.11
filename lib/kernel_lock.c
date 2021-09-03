/*
 * lib/kernel_lock.c
 *
 * This is the traditional BKL - big kernel lock. Largely
 * relegated to obsolescense, but used by various less
 * important (or lazy) subsystems.
 */
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <linux/kallsyms.h>

#if defined(CONFIG_PREEMPT) && defined(__smp_processor_id) && \
		defined(CONFIG_DEBUG_PREEMPT)

/*
 * Debugging check.
 */
unsigned int smp_processor_id(void)
{
	unsigned long preempt_count = preempt_count();
	int this_cpu = __smp_processor_id();
	cpumask_t this_mask;

	if (likely(preempt_count))
		goto out;

	if (irqs_disabled())
		goto out;

	/*
	 * Kernel threads bound to a single CPU can safely use
	 * smp_processor_id():
	 */
	this_mask = cpumask_of_cpu(this_cpu);

	if (cpus_equal(current->cpus_allowed, this_mask))
		goto out;

	/*
	 * It is valid to assume CPU-locality during early bootup:
	 */
	if (system_state != SYSTEM_RUNNING)
		goto out;

	/*
	 * Avoid recursion:
	 */
	preempt_disable();

	if (!printk_ratelimit())
		goto out_enable;

	printk(KERN_ERR "BUG: using smp_processor_id() in preemptible [%08x] code: %s/%d\n", preempt_count(), current->comm, current->pid);
	print_symbol("caller is %s\n", (long)__builtin_return_address(0));
	dump_stack();

out_enable:
	preempt_enable_no_resched();
out:
	return this_cpu;
}

EXPORT_SYMBOL(smp_processor_id);

#endif /* PREEMPT && __smp_processor_id && DEBUG_PREEMPT */

#ifdef CONFIG_PREEMPT_BKL			// 支持抢占的大内核锁
/*
 * The 'big kernel semaphore'
 *
 * 这个互斥锁由lock_kernel() 和unlock_kernel() 递归地获取和释放。
 * 它在 schedule() 中被透明地丢弃和重新获取。
 * 它用于保护尚未迁移到适当锁定设计的遗留代码。
 *
 * 注意：被这个信号量锁定的代码只会使用
 * 相同的锁定工具针对其他代码进行序列化。
 * 代码保证任务保持在同一个 CPU 上。
 *
 * 不要在新代码中使用。
 */
DECLARE_MUTEX(kernel_sem);

/*
 * 重新获取内核锁，信号实现
 *
 * 这个函数在抢占关闭的情况下被调用。
 *
 * 我们在 schedule() 中执行，因此代码必须非常小心递归，
 * 无论是由于 down() 还是由于启用了抢占。
 * schedule() 将在重新获取信号量后重新检查抢占标志。
 */
int __lockfunc __reacquire_kernel_lock(void)
{
	struct task_struct *task = current;
	int saved_lock_depth = task->lock_depth;

	BUG_ON(saved_lock_depth < 0);

	task->lock_depth = -1;
	preempt_enable_no_resched();

	down(&kernel_sem);

	preempt_disable();
	task->lock_depth = saved_lock_depth;

	return 0;
}

/* 释放大内核锁，信号量实现
 * 强制释放，不要求lock_depth==0，但要>=0
 * */
void __lockfunc __release_kernel_lock(void)
{
	up(&kernel_sem);
}

/*
 * 获取大内核锁，信号量实现。
 */
void __lockfunc lock_kernel(void)
{
	struct task_struct *task = current;
	int depth = task->lock_depth + 1;

	if (likely(!depth))
		/*
		 * 无需担心递归 - 我们设置了 lock_depth _after_
		 */
		down(&kernel_sem);

	task->lock_depth = depth;
}
/* 释放大内核锁，信号量实现
 * */
void __lockfunc unlock_kernel(void)
{
	struct task_struct *task = current;

	BUG_ON(task->lock_depth < 0);

	if (likely(--task->lock_depth < 0))
		up(&kernel_sem);
}

#else		// !CONFIG_PREEMPT_BKL

/*
 * The 'big kernel lock'
 *
 * 这个自旋锁由lock_kernel() 和unlock_kernel() 递归地获取和释放。
 * 它在 schedule() 中被透明地丢弃和重新获取。
 * 它用于保护尚未迁移到适当锁定设计的遗留代码。
 *
 * 不要在新代码中使用。
 */
static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(kernel_flag);


/*
 * Acquire/release the underlying lock from the scheduler.
 *
 * This is called with preemption disabled, and should
 * return an error value if it cannot get the lock and
 * TIF_NEED_RESCHED gets set.
 *
 * If it successfully gets the lock, it should increment
 * the preemption count like any spinlock does.
 *
 * (This works on UP too - _raw_spin_trylock will never
 * return false in that case)
 */
int __lockfunc __reacquire_kernel_lock(void)
{
	while (!_raw_spin_trylock(&kernel_flag)) {
		if (test_thread_flag(TIF_NEED_RESCHED))
			return -EAGAIN;
		cpu_relax();
	}
	preempt_disable();
	return 0;
}
/* 释放大内核锁，自旋锁实现
 * 强制释放，不要求lock_depth==0，但要>=0
 * */
void __lockfunc __release_kernel_lock(void)
{
	_raw_spin_unlock(&kernel_flag);
	preempt_enable_no_resched();
}

/*
 * 这些是 BKL 自旋锁 - 我们尽量对抢占保持礼貌。
 * 如果 SMP 未开启（即 UP 抢占），这一切都会消失，
 * 因为 _raw_spin_trylock() 将始终成功。
 */
#ifdef CONFIG_PREEMPT
static inline void __lock_kernel(void)
{
	preempt_disable();
	if (unlikely(!_raw_spin_trylock(&kernel_flag))) {
		/*
		 * If preemption was disabled even before this
		 * was called, there's nothing we can be polite
		 * about - just spin.
		 */
		if (preempt_count() > 1) {
			_raw_spin_lock(&kernel_flag);
			return;
		}

		/*
		 * Otherwise, let's wait for the kernel lock
		 * with preemption enabled..
		 */
		do {
			preempt_enable();
			while (spin_is_locked(&kernel_flag))
				cpu_relax();
			preempt_disable();
		} while (!_raw_spin_trylock(&kernel_flag));
	}
}

#else

/*
 * Non-preemption case - just get the spinlock
 */
static inline void __lock_kernel(void)
{
	_raw_spin_lock(&kernel_flag);
}
#endif

static inline void __unlock_kernel(void)
{
	_raw_spin_unlock(&kernel_flag);
	preempt_enable();
}

/*
 * 获得大内核锁，使用自旋锁实现
 *
 * 这不能异步发生，所以我们只需要担心其他 CPU。
 */
void __lockfunc lock_kernel(void)
{
	int depth = current->lock_depth+1;
	if (likely(!depth))
		__lock_kernel();
	current->lock_depth = depth;
}
/*
 * 释放大内核锁，自旋锁实现
 * */
void __lockfunc unlock_kernel(void)
{
	BUG_ON(current->lock_depth < 0);
	if (likely(--current->lock_depth < 0))
		__unlock_kernel();
}

#endif

EXPORT_SYMBOL(lock_kernel);
EXPORT_SYMBOL(unlock_kernel);

