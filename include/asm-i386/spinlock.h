#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/atomic.h>
#include <asm/rwlock.h>
#include <asm/page.h>
#include <linux/config.h>
#include <linux/compiler.h>

asmlinkage int printk(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

/*
 * 您的基本 SMP 自旋锁，在任何地方只允许一个 CPU
 *
 * 自旋锁
 * 临界区C只能有一个内核控制路径P，使用于SMP环境
 * 如果内核控制路径发现自旋锁是开着的，就获得锁并继续自己的执行。
 * 如果发现锁由运行在另一个CPU上的内核控制路径锁着，就反复执行一条紧凑的循环指令，直到锁被释放。
 * 循环等待即忙等，即使等待的内核控制路径无事可做，它也在CPU上保持运行。
 * 要求持有锁的时间非常短，加锁和解锁的时间也非常短，不能在持有锁时睡眠。
 * 一般来说，由自旋锁保护的临界区时禁止抢占的，因此，在UP中，这种锁仅仅起到禁止/启用内核抢占。
 * 注意，在自旋锁忙等期间，内核抢占还是有效的，等待自旋锁释放的进程仍有可能被更高优先级的进程代替。
 * spin_lock_init()
 * spin_lock()	1.关闭抢占；2.slock减1看不能获得自旋锁，不能就自旋，直到slcok变为1
 * spin_trylock()	1.关闭抢占；2.非阻塞获得自旋锁xchg，如果未获取，则关闭抢占
 * spin_unlock()	释放锁
 * spin_unlock_wait()	等待锁的释放，并获取锁
 * spin_is_locked()		检查锁是否释放
 * https://www.cnblogs.com/aaronlinux/p/5904479.html?utm_source=itdadao&utm_medium=referral
 *
 * 由于请求锁而得不到时，会导致进程忙等，所以
 * 1. 持有锁的时间要短，加锁、解锁时间要短
 * 2. 持有锁的期间内不能休眠，要关闭抢占
 * 注意：忙等的进程仍然有可能被抢占
 * */
typedef struct {
	volatile unsigned int slock;		// 状态，slock等于1时表示未加锁，slock<=0表示已加锁
#ifdef CONFIG_DEBUG_SPINLOCK
	unsigned magic;
#endif
#ifdef CONFIG_PREEMPT		// 支持SMP(CONFIG_SMP)和内核抢占(CONFIG_PREEMPT)的情况下使用
	unsigned int break_lock;		// 记录正在等待该锁的进程，初始值为0，有等待的为1
#endif
} spinlock_t;

#define SPINLOCK_MAGIC	0xdead4ead

#ifdef CONFIG_DEBUG_SPINLOCK
#define SPINLOCK_MAGIC_INIT	, SPINLOCK_MAGIC
#else
#define SPINLOCK_MAGIC_INIT	/* */
#endif

#define SPIN_LOCK_UNLOCKED (spinlock_t) { 1 SPINLOCK_MAGIC_INIT }

// 初始化锁，即把slock置1
#define spin_lock_init(x)	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)

/*
 * 简单的自旋锁操作。有两种变体，一种清除本地处理器上的 IRQ，一种不清除。
 *
 * 我们不做任何公平假设。他们有成本。
 */

#define spin_is_locked(x)	(*(volatile signed char *)(&(x)->slock) <= 0)
#define spin_unlock_wait(x)	do { barrier(); } while(spin_is_locked(x))

// 加锁过程
#define spin_lock_string \
	"\n1:\t" \
	"lock ; decb %0\n\t" \		// 使用lock前缀，dec对slock减1
	"jns 3f\n" \		// 减1后，如果为0，则加锁成功
	"2:\t" \
	"rep;nop\n\t" \		// 循环nop
	"cmpb $0,%0\n\t" \
	"jle 2b\n\t" \		// 如果slock小于等于0，则继续循环
	"jmp 1b\n" \	// 走到这里说明，slock变为1了，无条件跳回到1，加锁
	"3:\n\t"

#define spin_lock_string_flags \
	"\n1:\t" \
	"lock ; decb %0\n\t" \
	"jns 4f\n\t" \
	"2:\t" \
	"testl $0x200, %1\n\t" \
	"jz 3f\n\t" \
	"sti\n\t" \
	"3:\t" \
	"rep;nop\n\t" \
	"cmpb $0, %0\n\t" \
	"jle 3b\n\t" \
	"cli\n\t" \
	"jmp 1b\n" \
	"4:\n\t"

/*
 * This works. Despite all the confusion.
 * (except on PPro SMP or if we are using OOSTORE)
 * (PPro errata 66, 92)
 */

// 注意这个条件编译
#if !defined(CONFIG_X86_OOSTORE) && !defined(CONFIG_X86_PPRO_FENCE)

#define spin_unlock_string \
	"movb $1,%0" \
		:"=m" (lock->slock) : : "memory"


static inline void _raw_spin_unlock(spinlock_t *lock)
{
#ifdef CONFIG_DEBUG_SPINLOCK
	BUG_ON(lock->magic != SPINLOCK_MAGIC);
	BUG_ON(!spin_is_locked(lock));
#endif
	__asm__ __volatile__(
		spin_unlock_string
	);
}

#else

#define spin_unlock_string \
	"xchgb %b0, %1" \
		:"=q" (oldval), "=m" (lock->slock) \
		:"0" (oldval) : "memory"

static inline void _raw_spin_unlock(spinlock_t *lock)
{
	char oldval = 1;
#ifdef CONFIG_DEBUG_SPINLOCK
	BUG_ON(lock->magic != SPINLOCK_MAGIC);
	BUG_ON(!spin_is_locked(lock));
#endif
	__asm__ __volatile__(
		spin_unlock_string
	);
}

#endif

// 非阻塞获取自旋锁锁
static inline int _raw_spin_trylock(spinlock_t *lock)
{
	char oldval;
	__asm__ __volatile__(
		"xchgb %b0,%1"		// 默认带上lock前缀
		:"=q" (oldval), "=m" (lock->slock)
		:"0" (0) : "memory");		// oldval被初始化为0
	return oldval > 0;		// 为true则，获取到了自旋锁
}

// 检查能否获得锁，不能就自旋，直到获取到时
static inline void _raw_spin_lock(spinlock_t *lock)
{
#ifdef CONFIG_DEBUG_SPINLOCK
	if (unlikely(lock->magic != SPINLOCK_MAGIC)) {
		printk("eip: %p\n", __builtin_return_address(0));
		BUG();
	}
#endif
	__asm__ __volatile__(
		spin_lock_string
		:"=m" (lock->slock) : : "memory");
}

static inline void _raw_spin_lock_flags (spinlock_t *lock, unsigned long flags)
{
#ifdef CONFIG_DEBUG_SPINLOCK
	if (unlikely(lock->magic != SPINLOCK_MAGIC)) {
		printk("eip: %p\n", __builtin_return_address(0));
		BUG();
	}
#endif
	__asm__ __volatile__(
		spin_lock_string_flags
		:"=m" (lock->slock) : "r" (flags) : "memory");
}

/*
 * 读写自旋锁，允许多个读者但只有一个写者。
 *
 * 笔记！在中断中有读取器但没有中断写入器是很常见的。
 * 对于这些情况，我们可以“混合” irq 安全锁
 * ——任何写者都需要获得 irq 安全的写锁，
 * 但读者可以获得非 irq 安全的读锁。
 *
 * 读写自旋锁
 */
typedef struct {
	volatile unsigned int lock;
#ifdef CONFIG_DEBUG_SPINLOCK
	unsigned magic;
#endif
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} rwlock_t;

#define RWLOCK_MAGIC	0xdeaf1eed

#ifdef CONFIG_DEBUG_SPINLOCK
#define RWLOCK_MAGIC_INIT	, RWLOCK_MAGIC
#else
#define RWLOCK_MAGIC_INIT	/* */
#endif

#define RW_LOCK_UNLOCKED (rwlock_t) { RW_LOCK_BIAS RWLOCK_MAGIC_INIT }

// 初始化读写自旋锁，初始值为0x01000000
#define rwlock_init(x)	do { *(x) = RW_LOCK_UNLOCKED; } while(0)

/*
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define read_can_lock(x) ((int)(x)->lock > 0)

/*
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define write_can_lock(x) ((x)->lock == RW_LOCK_BIAS)

/*
 * On x86, we implement read-write locks as a 32-bit counter
 * with the high bit (sign) being the "contended" bit.
 *
 * The inline assembly is non-obvious. Think about it.
 *
 * Changed to use the same technique as rw semaphores.  See
 * semaphore.h for details.  -ben
 */
/* the spinlock helpers are in arch/i386/kernel/semaphore.c */

static inline void _raw_read_lock(rwlock_t *rw)
{
#ifdef CONFIG_DEBUG_SPINLOCK
	BUG_ON(rw->magic != RWLOCK_MAGIC);
#endif
	__build_read_lock(rw, "__read_lock_failed");
}

static inline void _raw_write_lock(rwlock_t *rw)
{
#ifdef CONFIG_DEBUG_SPINLOCK
	BUG_ON(rw->magic != RWLOCK_MAGIC);
#endif
	__build_write_lock(rw, "__write_lock_failed");
}

#define _raw_read_unlock(rw)		asm volatile("lock ; incl %0" :"=m" ((rw)->lock) : : "memory")
#define _raw_write_unlock(rw)	asm volatile("lock ; addl $" RW_LOCK_BIAS_STR ",%0":"=m" ((rw)->lock) : : "memory")

static inline int _raw_read_trylock(rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;
	atomic_dec(count);
	if (atomic_read(count) >= 0)
		return 1;
	atomic_inc(count);
	return 0;
}

static inline int _raw_write_trylock(rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;
	if (atomic_sub_and_test(RW_LOCK_BIAS, count))
		return 1;
	atomic_add(RW_LOCK_BIAS, count);
	return 0;
}

#endif /* __ASM_SPINLOCK_H */
