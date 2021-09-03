#ifndef _LINUX_PERCPU_COUNTER_H
#define _LINUX_PERCPU_COUNTER_H
/*
 * 用于 ext2 和 ext3 超级块的简单“近似计数器”。
 *
 * 警告：这些东西是巨大的。 32 路 P4 上每个计数器 4 KB。
 */

#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/percpu.h>

#ifdef CONFIG_SMP		// SMP中才有必要

// 近似per CPU counter
struct percpu_counter {
	spinlock_t lock;		// 自旋锁
	long count;				// 准确值
	long *counters;			// 指向per cpu变量的指针，里面保存每个cpu使用的近似值
};

// FBC_BATCH为触发同步的阈值
#if NR_CPUS >= 16
#define FBC_BATCH	(NR_CPUS*2)
#else
#define FBC_BATCH	(NR_CPUS*4)
#endif

// 动态初始化，实际上没有办法静态初始化
static inline void percpu_counter_init(struct percpu_counter *fbc)
{
	spin_lock_init(&fbc->lock);
	fbc->count = 0;
	fbc->counters = alloc_percpu(long);		// 动态分配per CPU变量
}

// 删除
static inline void percpu_counter_destroy(struct percpu_counter *fbc)
{
	free_percpu(fbc->counters);
}

void percpu_counter_mod(struct percpu_counter *fbc, long amount);

// 读取准确值
static inline long percpu_counter_read(struct percpu_counter *fbc)
{
	return fbc->count;
}

/*
 * percpu_counter_read() 有可能为某个永远不应该为负的计数器返回一个小的负数。
 */
static inline long percpu_counter_read_positive(struct percpu_counter *fbc)
{
	long ret = fbc->count;

	barrier();		/* Prevent reloads of fbc->count */
	if (ret > 0)
		return ret;
	return 1;
}

#else	// !CONFIG_SMP

struct percpu_counter {
	long count;
};

static inline void percpu_counter_init(struct percpu_counter *fbc)
{
	fbc->count = 0;
}

static inline void percpu_counter_destroy(struct percpu_counter *fbc)
{
}

static inline void
percpu_counter_mod(struct percpu_counter *fbc, long amount)
{
	preempt_disable();
	fbc->count += amount;
	preempt_enable();
}

static inline long percpu_counter_read(struct percpu_counter *fbc)
{
	return fbc->count;
}

static inline long percpu_counter_read_positive(struct percpu_counter *fbc)
{
	return fbc->count;
}

#endif	/* CONFIG_SMP */

static inline void percpu_counter_inc(struct percpu_counter *fbc)
{
	percpu_counter_mod(fbc, 1);
}

static inline void percpu_counter_dec(struct percpu_counter *fbc)
{
	percpu_counter_mod(fbc, -1);
}

#endif /* _LINUX_PERCPU_COUNTER_H */
