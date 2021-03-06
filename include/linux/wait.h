#ifndef _LINUX_WAIT_H
#define _LINUX_WAIT_H

#define WNOHANG		0x00000001
#define WUNTRACED	0x00000002
#define WSTOPPED	WUNTRACED
#define WEXITED		0x00000004
#define WCONTINUED	0x00000008
#define WNOWAIT		0x01000000	/* Don't reap, just poll status.  */

#define __WNOTHREAD	0x20000000	/* Don't wait on children of other threads in this group */
#define __WALL		0x40000000	/* Wait on all children, regardless of type */
#define __WCLONE	0x80000000	/* Wait only on non-SIGCHLD children */

/* First argument to waitid: */
#define P_ALL		0
#define P_PID		1
#define P_PGID		2

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/list.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/current.h>

typedef struct __wait_queue wait_queue_t;       // 等待队列成员
typedef int (*wait_queue_func_t)(wait_queue_t *wait, unsigned mode, int sync, void *key);   // 唤醒时调用的回调函数指针
int default_wake_function(wait_queue_t *wait, unsigned mode, int sync, void *key);

// 等待队列的成员
struct __wait_queue {
	unsigned int flags;     // 取WQ_FLAG_EXCLUSIVE或者0
	// 排他唤醒进程，防止惊群响应,https://blog.csdn.net/lyztyycode/article/details/78648798
    #define WQ_FLAG_EXCLUSIVE	0x01
	struct task_struct * task;  //为NULL表示异步IO
	wait_queue_func_t func;         // 回调函数
	struct list_head task_list;
};

struct wait_bit_key {
	void *flags;
	int bit_nr;
};

struct wait_bit_queue {
	struct wait_bit_key key;
	wait_queue_t wait;
};

// 等待队列的头
struct __wait_queue_head {
	spinlock_t lock;
	struct list_head task_list;
};
typedef struct __wait_queue_head wait_queue_head_t;


/*
 * Macros for declaration and initialisaton of the datatypes
 */
// 等待队列成员静态初始化
#define __WAITQUEUE_INITIALIZER(name, tsk) {				\
	.task		= tsk,						\
	.func		= default_wake_function,			\
	.task_list	= { NULL, NULL } }

#define DECLARE_WAITQUEUE(name, tsk)					\
	wait_queue_t name = __WAITQUEUE_INITIALIZER(name, tsk)

// 等待队列头静态初始化
#define __WAIT_QUEUE_HEAD_INITIALIZER(name) {				\
	.lock		= SPIN_LOCK_UNLOCKED,				\
	.task_list	= { &(name).task_list, &(name).task_list } }

#define DECLARE_WAIT_QUEUE_HEAD(name) \
	wait_queue_head_t name = __WAIT_QUEUE_HEAD_INITIALIZER(name)

#define __WAIT_BIT_KEY_INITIALIZER(word, bit)				\
	{ .flags = word, .bit_nr = bit, }

// 初始化动态生成的等待队列头
static inline void init_waitqueue_head(wait_queue_head_t *q)
{
	q->lock = SPIN_LOCK_UNLOCKED;
	INIT_LIST_HEAD(&q->task_list);
}

// 初始化动态生成的等待队列成员
static inline void init_waitqueue_entry(wait_queue_t *q, struct task_struct *p)
{
	q->flags = 0;
	q->task = p;
	q->func = default_wake_function;
}

// 自定义的等待队列成员
static inline void init_waitqueue_func_entry(wait_queue_t *q,
					wait_queue_func_t func)
{
	q->flags = 0;
	q->task = NULL;
	q->func = func;
}

// 队列判空
static inline int waitqueue_active(wait_queue_head_t *q)
{
	return !list_empty(&q->task_list);
}

/*
    用于区分同步和异步io等待上下文：sync io通常指定NULL等待队列条目或绑定到要唤醒的任务（当前任务）
    的等待队列条目。 aio使用异步通知回调例程指定一个等待队列条目，该例程不与任何任务关联.
 */
#define is_sync_wait(wait)	(!(wait) || ((wait)->task))

extern void FASTCALL(add_wait_queue(wait_queue_head_t *q, wait_queue_t * wait));
extern void FASTCALL(add_wait_queue_exclusive(wait_queue_head_t *q, wait_queue_t * wait));
extern void FASTCALL(remove_wait_queue(wait_queue_head_t *q, wait_queue_t * wait));

// 等待队列成员加入等待队列，头插
static inline void __add_wait_queue(wait_queue_head_t *head, wait_queue_t *new)
{
	list_add(&new->task_list, &head->task_list);
}

/*
 * Used for wake-one threads:
 */
// 等待队列成员加入等待队列，尾插
static inline void __add_wait_queue_tail(wait_queue_head_t *head,
						wait_queue_t *new)
{
	list_add_tail(&new->task_list, &head->task_list);
}

// 等待队列成员从等待队列中删除
static inline void __remove_wait_queue(wait_queue_head_t *head,
							wait_queue_t *old)
{
	list_del(&old->task_list);
}

void FASTCALL(__wake_up(wait_queue_head_t *q, unsigned int mode, int nr, void *key));
extern void FASTCALL(__wake_up_locked(wait_queue_head_t *q, unsigned int mode));
extern void FASTCALL(__wake_up_sync(wait_queue_head_t *q, unsigned int mode, int nr));
void FASTCALL(__wake_up_bit(wait_queue_head_t *, void *, int));
int FASTCALL(__wait_on_bit(wait_queue_head_t *, struct wait_bit_queue *, int (*)(void *), unsigned));
int FASTCALL(__wait_on_bit_lock(wait_queue_head_t *, struct wait_bit_queue *, int (*)(void *), unsigned));
void FASTCALL(wake_up_bit(void *, int));
int FASTCALL(out_of_line_wait_on_bit(void *, int, int (*)(void *), unsigned));
int FASTCALL(out_of_line_wait_on_bit_lock(void *, int, int (*)(void *), unsigned));
wait_queue_head_t *FASTCALL(bit_waitqueue(void *, int));

/* 各种唤醒函数
 * 1. wake_up: 唤醒可中断的和不可中断的，所有非排他+1个排他的
 * 2. wake_up_nr: 唤醒可中断的和不可中断的，所有非排他+nr个排他的
 * 3. wake_up_all: 唤醒可中断的和不可中断的，所有非排他+所有排他的
 * 4. wake_up_interruptible: 唤醒可中断的，所有非排他+所有排他的
 * 5. wake_up_interruptible_nr: 唤醒可中断的，所有非排他+所有排他的
 * 6. wake_up_interruptible_all: 唤醒可中断的，所有非排他+所有排他的
 *
 * 7. wake_up_locked与wake_up类似，不同的是调用者需要持有自旋锁
 * */
#define wake_up(x)			__wake_up(x, TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE, 1, NULL)
#define wake_up_nr(x, nr)		__wake_up(x, TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE, nr, NULL)
#define wake_up_all(x)			__wake_up(x, TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE, 0, NULL)
#define wake_up_interruptible(x)	__wake_up(x, TASK_INTERRUPTIBLE, 1, NULL)
#define wake_up_interruptible_nr(x, nr)	__wake_up(x, TASK_INTERRUPTIBLE, nr, NULL)
#define wake_up_interruptible_all(x)	__wake_up(x, TASK_INTERRUPTIBLE, 0, NULL)

#define	wake_up_locked(x)		__wake_up_locked((x), TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE)
#define wake_up_interruptible_sync(x)   __wake_up_sync((x),TASK_INTERRUPTIBLE, 1)

/* 等待第一个参数queue作为等待队列头的等待队列被唤醒，
 * 而且第二个参数condition必须满足，否则阻塞。
 *
 * wait_event()和wait_event_interruptible()的区别
 * 在于后者可以被信号打断，而前者不能。加上timeout后的
 * 宏意味着阻塞等待的超时时间，以jiffy为单位，在第三个参数
 * 的timeout到达时，不论condition是否满足，均返回。
 * */

/* 各种等待函数
 * 1. wait_event:等待condition为true，不可中断的等待
 * 2. wait_event_timeout:等待condition为true，不可中断的，有超时时间的等待
 * 3. wait_event_interruptible:等待condition为true，可中断的等待
 * 4. wait_event_interruptible_timeout:等待condition为true，可中断的，有超时时间的等待
 * 5. wait_event_interruptible_exclusive:等待condition为true，可中断的，排他的等待
 * */
#define __wait_event(wq, condition) 					\
do {									\
	DEFINE_WAIT(__wait);						\
									\
	for (;;) {							\
		prepare_to_wait(&wq, &__wait, TASK_UNINTERRUPTIBLE);	\
		if (condition)						\
			break;						\
		schedule();						\
	}								\
	finish_wait(&wq, &__wait);					\
} while (0)

// 等待condition为true，不可中断的等待
#define wait_event(wq, condition) 					\
do {									\
	if (condition)	 						\
		break;							\
	__wait_event(wq, condition);					\
} while (0)

#define __wait_event_timeout(wq, condition, ret)			\
do {									\
	DEFINE_WAIT(__wait);						\
									\
	for (;;) {							\
		prepare_to_wait(&wq, &__wait, TASK_UNINTERRUPTIBLE);	\
		if (condition)						\
			break;						\
		ret = schedule_timeout(ret);				\
		if (!ret)						\
			break;						\
	}								\
	finish_wait(&wq, &__wait);					\
} while (0)

// 等待condition为true，不可中断的，有超时时间的等待
#define wait_event_timeout(wq, condition, timeout)			\
({									\
	long __ret = timeout;						\
	if (!(condition)) 						\
		__wait_event_timeout(wq, condition, __ret);		\
	__ret;								\
})

#define __wait_event_interruptible(wq, condition, ret)			\
do {									\
	DEFINE_WAIT(__wait);						\
									\
	for (;;) {							\
		prepare_to_wait(&wq, &__wait, TASK_INTERRUPTIBLE);	\
		if (condition)						\
			break;						\
		if (!signal_pending(current)) {				\
			schedule();					\
			continue;					\
		}							\
		ret = -ERESTARTSYS;					\
		break;							\
	}								\
	finish_wait(&wq, &__wait);					\
} while (0)

// 等待condition为true，可中断的等待
#define wait_event_interruptible(wq, condition)				\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__wait_event_interruptible(wq, condition, __ret);	\
	__ret;								\
})

#define __wait_event_interruptible_timeout(wq, condition, ret)		\
do {									\
	DEFINE_WAIT(__wait);						\
									\
	for (;;) {							\
		prepare_to_wait(&wq, &__wait, TASK_INTERRUPTIBLE);	\
		if (condition)						\
			break;						\
		if (!signal_pending(current)) {				\
			ret = schedule_timeout(ret);			\
			if (!ret)					\
				break;					\
			continue;					\
		}							\
		ret = -ERESTARTSYS;					\
		break;							\
	}								\
	finish_wait(&wq, &__wait);					\
} while (0)

// 等待condition为true，可中断的，有超时时间的等待
#define wait_event_interruptible_timeout(wq, condition, timeout)	\
({									\
	long __ret = timeout;						\
	if (!(condition))						\
		__wait_event_interruptible_timeout(wq, condition, __ret); \
	__ret;								\
})

#define __wait_event_interruptible_exclusive(wq, condition, ret)	\
do {									\
	DEFINE_WAIT(__wait);						\
									\
	for (;;) {							\
		prepare_to_wait_exclusive(&wq, &__wait,			\
					TASK_INTERRUPTIBLE);		\
		if (condition)						\
			break;						\
		if (!signal_pending(current)) {				\
			schedule();					\
			continue;					\
		}							\
		ret = -ERESTARTSYS;					\
		break;							\
	}								\
	finish_wait(&wq, &__wait);					\
} while (0)

// 等待condition为true，可中断的，排他的等待
#define wait_event_interruptible_exclusive(wq, condition)		\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__wait_event_interruptible_exclusive(wq, condition, __ret);\
	__ret;								\
})

/*
 * 必须使用持有的 wait_queue_head_t 中的自旋锁调用。
 */
static inline void add_wait_queue_exclusive_locked(wait_queue_head_t *q,
						   wait_queue_t * wait)
{
	wait->flags |= WQ_FLAG_EXCLUSIVE;
	__add_wait_queue_tail(q,  wait);
}

/*
 * Must be called with the spinlock in the wait_queue_head_t held.
 */
static inline void remove_wait_queue_locked(wait_queue_head_t *q,
					    wait_queue_t * wait)
{
	__remove_wait_queue(q,  wait);
}

/*
 * These are the old interfaces to sleep waiting for an event.
 * They are racy.  DO NOT use them, use the wait_event* interfaces above.  
 * We plan to remove these interfaces during 2.7.
 */
extern void FASTCALL(sleep_on(wait_queue_head_t *q));
extern long FASTCALL(sleep_on_timeout(wait_queue_head_t *q,
				      signed long timeout));
extern void FASTCALL(interruptible_sleep_on(wait_queue_head_t *q));
extern long FASTCALL(interruptible_sleep_on_timeout(wait_queue_head_t *q,
						    signed long timeout));

/*
 * Waitqueues which are removed from the waitqueue_head at wakeup time
 */
void FASTCALL(prepare_to_wait(wait_queue_head_t *q,
				wait_queue_t *wait, int state));
void FASTCALL(prepare_to_wait_exclusive(wait_queue_head_t *q,
				wait_queue_t *wait, int state));
void FASTCALL(finish_wait(wait_queue_head_t *q, wait_queue_t *wait));
int autoremove_wake_function(wait_queue_t *wait, unsigned mode, int sync, void *key);
int wake_bit_function(wait_queue_t *wait, unsigned mode, int sync, void *key);

#define DEFINE_WAIT(name)						\
	wait_queue_t name = {						\
		.task		= current,				\
		.func		= autoremove_wake_function,		\
		.task_list	= {	.next = &(name).task_list,	\
					.prev = &(name).task_list,	\
				},					\
	}

#define DEFINE_WAIT_BIT(name, word, bit)				\
	struct wait_bit_queue name = {					\
		.key = __WAIT_BIT_KEY_INITIALIZER(word, bit),		\
		.wait	= {						\
			.task		= current,			\
			.func		= wake_bit_function,		\
			.task_list	=				\
				LIST_HEAD_INIT((name).wait.task_list),	\
		},							\
	}

#define init_wait(wait)							\
	do {								\
		(wait)->task = current;					\
		(wait)->func = autoremove_wake_function;		\
		INIT_LIST_HEAD(&(wait)->task_list);			\
	} while (0)

/**
 * wait_on_bit - wait for a bit to be cleared
 * @word: the word being waited on, a kernel virtual address
 * @bit: the bit of the word being waited on
 * @action: the function used to sleep, which may take special actions
 * @mode: the task state to sleep in
 *
 * There is a standard hashed waitqueue table for generic use. This
 * is the part of the hashtable's accessor API that waits on a bit.
 * For instance, if one were to have waiters on a bitflag, one would
 * call wait_on_bit() in threads waiting for the bit to clear.
 * One uses wait_on_bit() where one is waiting for the bit to clear,
 * but has no intention of setting it.
 */
static inline int wait_on_bit(void *word, int bit,
				int (*action)(void *), unsigned mode)
{
	if (!test_bit(bit, word))
		return 0;
	return out_of_line_wait_on_bit(word, bit, action, mode);
}

/**
 * wait_on_bit_lock - wait for a bit to be cleared, when wanting to set it
 * @word: the word being waited on, a kernel virtual address
 * @bit: the bit of the word being waited on
 * @action: the function used to sleep, which may take special actions
 * @mode: the task state to sleep in
 *
 * There is a standard hashed waitqueue table for generic use. This
 * is the part of the hashtable's accessor API that waits on a bit
 * when one intends to set it, for instance, trying to lock bitflags.
 * For instance, if one were to have waiters trying to set bitflag
 * and waiting for it to clear before setting it, one would call
 * wait_on_bit() in threads waiting to be able to set the bit.
 * One uses wait_on_bit_lock() where one is waiting for the bit to
 * clear with the intention of setting it, and when done, clearing it.
 */
static inline int wait_on_bit_lock(void *word, int bit,
				int (*action)(void *), unsigned mode)
{
	if (!test_and_set_bit(bit, word))
		return 0;
	return out_of_line_wait_on_bit_lock(word, bit, action, mode);
}
	
#endif /* __KERNEL__ */

#endif
