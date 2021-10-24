#ifndef __LINUX_SEQLOCK_H
#define __LINUX_SEQLOCK_H
/*
 * 读者一致的机制，而不会让写者挨饿。
 * 这种类型的数据锁，读者需要一组一致的信息，并愿意在信息发生变化时重试。
 * 读者永远不会阻塞，但如果写者正在进行中，他们可能不得不重试。写者不会等待读者。
 *
 * 这不像 brlock 那样缓存友好。此外，这不适用于包含指针的数据，
 * 因为任何写入器都可能使读取器跟随的指针无效。
 *
 * 预期的读者使用:
 * 	do {
 *	    seq = read_seqbegin(&foo);
 * 	...
 *      } while (read_seqretry(&foo, seq));
 *
 *
 * 在非SMP上，自旋锁消失了，但写者仍然需要增加序列变量，
 * 因为中断例程可能会改变数据的状态。
 *
 * Based on x86_64 vsyscall gettimeofday 
 * by Keith Owens and Andrea Arcangeli
 *
 * https://blog.csdn.net/weixin_38233274/article/details/79276359
 */

#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/preempt.h>

/* 顺序锁定义
 * 读者不能持有被保护数据的指针，如果写者释放了读者
 * 引用的内存，读者retry的时候，就会有bug
 * */
typedef struct {
	unsigned sequence;		// 顺序计数器，初始值为0，写者增加该值
	spinlock_t lock;		// 写写时互斥
} seqlock_t;

/*
 * 这些宏触发了 gcc-3.x 编译时问题。我们认为这些现在都可以。要小心。
 */
#define SEQLOCK_UNLOCKED { 0, SPIN_LOCK_UNLOCKED }
#define seqlock_init(x)	do { *(x) = (seqlock_t) SEQLOCK_UNLOCKED; } while (0)


/* 锁定其他写者并更新计数。
 * 就像普通的 spin_lock/unlock 一样。
 * 不需要 preempt_disable() 因为它已经在 spin_lock 中了。
 */
static inline void write_seqlock(seqlock_t *sl)
{
	spin_lock(&sl->lock);
	++sl->sequence;
	smp_wmb();			
}	

static inline void write_sequnlock(seqlock_t *sl) 
{
	smp_wmb();
	sl->sequence++;
	spin_unlock(&sl->lock);
}

static inline int write_tryseqlock(seqlock_t *sl)
{
	int ret = spin_trylock(&sl->lock);

	if (ret) {
		++sl->sequence;
		smp_wmb();			
	}
	return ret;
}

/* 开始读取计算——获取最后一个完整的写入者令牌 */
static inline unsigned read_seqbegin(const seqlock_t *sl)
{
	unsigned ret = sl->sequence;
	smp_rmb();
	return ret;
}

/* 测试读者是否处理了无效数据。
 * 如果初始值为奇数，则在读取节时写入器已经启动
 * 如果序列值改变了，那么写者在节中改变了数据
 *    
 * 使用 xor 可以节省一个条件分支。
 *
 * 写者在操作时，顺序计数器为奇数，
 * 如果读完的时候，还有写者在写，或者写者写完，那么必须重新读取
 */
static inline int read_seqretry(const seqlock_t *sl, unsigned iv)
{
	smp_rmb();
	return (iv & 1) | (sl->sequence ^ iv);		// 对于偶数，第0位必定为0，对于奇数第0位必定为1
}


/*
 * Version using sequence counter only.
 * This can be used when code has its own mutex protecting the
 * updating starting before the write_seqcountbeqin() and ending
 * after the write_seqcount_end().
 */

typedef struct seqcount {
	unsigned sequence;
} seqcount_t;

#define SEQCNT_ZERO { 0 }
#define seqcount_init(x)	do { *(x) = (seqcount_t) SEQCNT_ZERO; } while (0)

/* Start of read using pointer to a sequence counter only.  */
static inline unsigned read_seqcount_begin(const seqcount_t *s)
{
	unsigned ret = s->sequence;
	smp_rmb();
	return ret;
}

/* Test if reader processed invalid data.
 * Equivalent to: iv is odd or sequence number has changed.
 *                (iv & 1) || (*s != iv)
 * Using xor saves one conditional branch.
 */
static inline int read_seqcount_retry(const seqcount_t *s, unsigned iv)
{
	smp_rmb();
	return (iv & 1) | (s->sequence ^ iv);
}


/*
 * Sequence counter only version assumes that callers are using their
 * own mutexing.
 */
static inline void write_seqcount_begin(seqcount_t *s)
{
	s->sequence++;
	smp_wmb();
}

static inline void write_seqcount_end(seqcount_t *s)
{
	smp_wmb();
	s->sequence++;
}

/*
 * Possible sw/hw IRQ protected versions of the interfaces.
 */
#define write_seqlock_irqsave(lock, flags)				\
	do { local_irq_save(flags); write_seqlock(lock); } while (0)
#define write_seqlock_irq(lock)						\
	do { local_irq_disable();   write_seqlock(lock); } while (0)
#define write_seqlock_bh(lock)						\
        do { local_bh_disable();    write_seqlock(lock); } while (0)

#define write_sequnlock_irqrestore(lock, flags)				\
	do { write_sequnlock(lock); local_irq_restore(flags); } while(0)
#define write_sequnlock_irq(lock)					\
	do { write_sequnlock(lock); local_irq_enable(); } while(0)
#define write_sequnlock_bh(lock)					\
	do { write_sequnlock(lock); local_bh_enable(); } while(0)

#define read_seqbegin_irqsave(lock, flags)				\
	({ local_irq_save(flags);   read_seqbegin(lock); })

#define read_seqretry_irqrestore(lock, iv, flags)			\
	({								\
		int ret = read_seqretry(lock, iv);			\
		local_irq_restore(flags);				\
		ret;							\
	})

#endif /* __LINUX_SEQLOCK_H */
