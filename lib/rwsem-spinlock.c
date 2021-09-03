/* rwsem-spinlock.c: R/W semaphores: contention handling functions for
 * generic spinlock implementation
 *
 * Copyright (c) 2001   David Howells (dhowells@redhat.com).
 * - Derived partially from idea by Andrea Arcangeli <andrea@suse.de>
 * - Derived also from comments by Linus
 */
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/module.h>

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	unsigned int flags;
#define RWSEM_WAITING_FOR_READ	0x00000001		// 因为申请读锁不得而加入队列
#define RWSEM_WAITING_FOR_WRITE	0x00000002		// 因为申请写锁不得而加入队列
};

#if RWSEM_DEBUG
void rwsemtrace(struct rw_semaphore *sem, const char *str)
{
	if (sem->debug)
		printk("[%d] %s({%d,%d})\n",
		       current->pid, str, sem->activity,
		       list_empty(&sem->wait_list) ? 0 : 1);
}
#endif

/*
 * 初始化信号量
 */
void fastcall init_rwsem(struct rw_semaphore *sem)
{
	sem->activity = 0;
	spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->wait_list);
#if RWSEM_DEBUG
	sem->debug = 0;
#endif
}

/*
 * 当现在可以运行的进程被阻塞时处理锁定释放
 * - 如果我们来到这里，那么：
 *   - “活动计数”_达到_零
 *   - “等待计数”非零
 * - 自旋锁必须由调用者持有
 * - 任务归零后，唤醒的进程块将从列表中丢弃
 * - 仅当唤醒写入非零时才唤醒写入器
 */
static inline struct rw_semaphore *
__rwsem_do_wake(struct rw_semaphore *sem, int wakewrite)
{
	struct rwsem_waiter *waiter;
	struct task_struct *tsk;
	int woken;

	rwsemtrace(sem, "Entering __rwsem_do_wake");

	// 取出第一个等待的写者
	waiter = list_entry(sem->wait_list.next, struct rwsem_waiter, list);

	if (!wakewrite) {
		if (waiter->flags & RWSEM_WAITING_FOR_WRITE)
			goto out;
		goto dont_wake_writers;
	}

	/* 如果我们被允许唤醒写者尝试授予单个写锁
	 * 如果队列前面有作者
	 * - 我们让“等待计数”递增以表示潜在的争用
	 */
	if (waiter->flags & RWSEM_WAITING_FOR_WRITE) {
		sem->activity = -1;
		list_del(&waiter->list);
		tsk = waiter->task;
		/* Don't touch waiter after ->task has been NULLed */
		mb();
		waiter->task = NULL;
		wake_up_process(tsk);		// 唤醒进程
		put_task_struct(tsk);
		goto out;
	}

	/* 向队列前端授予无限数量的读锁 */
 dont_wake_writers:
	woken = 0;
	while (waiter->flags & RWSEM_WAITING_FOR_READ) {
		struct list_head *next = waiter->list.next;

		list_del(&waiter->list);
		tsk = waiter->task;
		mb();
		waiter->task = NULL;
		wake_up_process(tsk);
		put_task_struct(tsk);
		woken++;
		if (list_empty(&sem->wait_list))
			break;
		waiter = list_entry(next, struct rwsem_waiter, list);
	}

	sem->activity += woken;

 out:
	rwsemtrace(sem, "Leaving __rwsem_do_wake");
	return sem;
}

/*
 * 唤醒一个写者
 */
static inline struct rw_semaphore *
__rwsem_wake_one_writer(struct rw_semaphore *sem)
{
	struct rwsem_waiter *waiter;
	struct task_struct *tsk;

	sem->activity = -1;

	waiter = list_entry(sem->wait_list.next, struct rwsem_waiter, list);
	list_del(&waiter->list);

	tsk = waiter->task;
	mb();
	waiter->task = NULL;
	wake_up_process(tsk);
	put_task_struct(tsk);
	return sem;
}

/*
 * 获得信号量的读锁
 */
void fastcall __sched __down_read(struct rw_semaphore *sem)
{
	struct rwsem_waiter waiter;
	struct task_struct *tsk;

	rwsemtrace(sem, "Entering __down_read");

	spin_lock(&sem->wait_lock);

	if (sem->activity >= 0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity++;
		spin_unlock(&sem->wait_lock);
		goto out;
	}

	tsk = current;
	set_task_state(tsk, TASK_UNINTERRUPTIBLE);

	/* 设置我自己风格的等待队列 */
	waiter.task = tsk;
	waiter.flags = RWSEM_WAITING_FOR_READ;
	get_task_struct(tsk);

	list_add_tail(&waiter.list, &sem->wait_list);

	/* 我们不再需要接触信号量结构 */
	spin_unlock(&sem->wait_lock);

	/* wait to be given the lock */
	for (;;) {
		if (!waiter.task)
			break;
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}

	tsk->state = TASK_RUNNING;

 out:
	rwsemtrace(sem, "Leaving __down_read");
}

/*
 * 用于读取的 trylock -- 如果成功则返回 1，如果争用则返回 0
 */
int fastcall __down_read_trylock(struct rw_semaphore *sem)
{
	int ret = 0;
	rwsemtrace(sem, "Entering __down_read_trylock");

	spin_lock(&sem->wait_lock);

	if (sem->activity >= 0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity++;
		ret = 1;
	}

	spin_unlock(&sem->wait_lock);

	rwsemtrace(sem, "Leaving __down_read_trylock");
	return ret;
}

/*
 * 在信号量上获得写锁
 * 我们无论如何都会增加等待计数以指示排他锁
 */
void fastcall __sched __down_write(struct rw_semaphore *sem)
{
	struct rwsem_waiter waiter;
	struct task_struct *tsk;

	rwsemtrace(sem, "Entering __down_write");

	spin_lock(&sem->wait_lock);		// 加自旋锁

	if (sem->activity == 0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity = -1;
		spin_unlock(&sem->wait_lock);
		goto out;		// 读者获得锁
	}

	tsk = current;
	set_task_state(tsk, TASK_UNINTERRUPTIBLE);		// 不可中断的睡眠

	/* 设置我自己风格的等待队列 */
	waiter.task = tsk;
	waiter.flags = RWSEM_WAITING_FOR_WRITE;
	get_task_struct(tsk);

	list_add_tail(&waiter.list, &sem->wait_list);

	/* 我们不再需要接触信号量结构 */
	spin_unlock(&sem->wait_lock);

	/* 等待获得锁 */
	for (;;) {
		if (!waiter.task)
			break;
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}

	tsk->state = TASK_RUNNING;

 out:
	rwsemtrace(sem, "Leaving __down_write");
}

/*
 * 用于写入的 trylock -- 如果成功则返回 1，如果争用则返回 0
 */
int fastcall __down_write_trylock(struct rw_semaphore *sem)
{
	int ret = 0;
	rwsemtrace(sem, "Entering __down_write_trylock");

	spin_lock(&sem->wait_lock);

	if (sem->activity == 0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity = -1;
		ret = 1;
	}

	spin_unlock(&sem->wait_lock);

	rwsemtrace(sem, "Leaving __down_write_trylock");
	return ret;
}

/*
 * 释放信号量上的读锁
 */
void fastcall __up_read(struct rw_semaphore *sem)
{
	rwsemtrace(sem, "Entering __up_read");

	spin_lock(&sem->wait_lock);

	if (--sem->activity == 0 && !list_empty(&sem->wait_list))
		sem = __rwsem_wake_one_writer(sem);

	spin_unlock(&sem->wait_lock);

	rwsemtrace(sem, "Leaving __up_read");
}

/*
 * 释放信号量的写锁
 */
void fastcall __up_write(struct rw_semaphore *sem)
{
	rwsemtrace(sem, "Entering __up_write");

	spin_lock(&sem->wait_lock);

	sem->activity = 0;
	if (!list_empty(&sem->wait_list))
		sem = __rwsem_do_wake(sem, 1);

	spin_unlock(&sem->wait_lock);

	rwsemtrace(sem, "Leaving __up_write");
}

/*
 * 将写锁降级为读锁
 * - 唤醒队列前面的任何读者
 */
void fastcall __downgrade_write(struct rw_semaphore *sem)
{
	rwsemtrace(sem, "Entering __downgrade_write");

	spin_lock(&sem->wait_lock);

	sem->activity = 1;
	if (!list_empty(&sem->wait_list))
		sem = __rwsem_do_wake(sem, 0);

	spin_unlock(&sem->wait_lock);

	rwsemtrace(sem, "Leaving __downgrade_write");
}

EXPORT_SYMBOL(init_rwsem);
EXPORT_SYMBOL(__down_read);
EXPORT_SYMBOL(__down_read_trylock);
EXPORT_SYMBOL(__down_write);
EXPORT_SYMBOL(__down_write_trylock);
EXPORT_SYMBOL(__up_read);
EXPORT_SYMBOL(__up_write);
EXPORT_SYMBOL(__downgrade_write);
#if RWSEM_DEBUG
EXPORT_SYMBOL(rwsemtrace);
#endif
