#ifndef __LINUX_SPINLOCK_H
#define __LINUX_SPINLOCK_H

/*
 * include/linux/spinlock.h - generic locking declarations
 */

#include <linux/config.h>
#include <linux/preempt.h>
#include <linux/linkage.h>
#include <linux/compiler.h>
#include <linux/thread_info.h>
#include <linux/kernel.h>
#include <linux/stringify.h>

#include <asm/processor.h>	/* for cpu relax */
#include <asm/system.h>

/*
 * 必须在包含其他文件之前定义这些，内联函数需要它们
 *
 * LOCK_SECTION_START，LOCK_SECTION_END中间的
 * 内容是把这一段的代码汇编到一个叫.text.lock的节中，
 * 并且这个节的属性是可重定位和可执行的，
 * 这样在代码的执行过程中，因为不同的节会
 * 被加载到不同的页去，所以如果前面不出现jmp就在1: 处结束了。
 *
 * 2:后面的指令大部分情况下不会发生，这样这些"多余"的指令
 * 不会影响常规指令的命中率
 */
#define LOCK_SECTION_NAME                       \
        ".text.lock." __stringify(KBUILD_BASENAME)

#define LOCK_SECTION_START(extra)               \
        ".subsection 1\n\t"                     \
        extra                                   \
        ".ifndef " LOCK_SECTION_NAME "\n\t"     \
        LOCK_SECTION_NAME ":\n\t"               \
        ".endif\n"

#define LOCK_SECTION_END                        \
        ".previous\n\t"

#define __lockfunc fastcall __attribute__((section(".spinlock.text")))      // 自旋锁相关的代码放入专用section中

/*
 * 如果设置了 CONFIG_SMP，拉入 _raw_ 定义
 */
#ifdef CONFIG_SMP

#define assert_spin_locked(x)	BUG_ON(!spin_is_locked(x))
#include <asm/spinlock.h>

/* 自旋锁和读写自旋锁接口的声明，注意
 * 1. __acquires和__releases
 * 2. 不管那种形式的加锁，都会关闭抢占
 * */
int __lockfunc _spin_trylock(spinlock_t *lock);			// 自旋锁非阻塞加锁
int __lockfunc _read_trylock(rwlock_t *lock);
int __lockfunc _write_trylock(rwlock_t *lock);

void __lockfunc _spin_lock(spinlock_t *lock)	__acquires(spinlock_t); 	// 自旋锁加锁
void __lockfunc _read_lock(rwlock_t *lock)	__acquires(rwlock_t);
void __lockfunc _write_lock(rwlock_t *lock)	__acquires(rwlock_t);

void __lockfunc _spin_unlock(spinlock_t *lock)	__releases(spinlock_t); 	// 自旋锁释放锁
void __lockfunc _read_unlock(rwlock_t *lock)	__releases(rwlock_t);
void __lockfunc _write_unlock(rwlock_t *lock)	__releases(rwlock_t);

unsigned long __lockfunc _spin_lock_irqsave(spinlock_t *lock)	__acquires(spinlock_t);		// 自旋锁加锁并且禁用本地中断(保存flag)
unsigned long __lockfunc _read_lock_irqsave(rwlock_t *lock)	__acquires(rwlock_t);
unsigned long __lockfunc _write_lock_irqsave(rwlock_t *lock)	__acquires(rwlock_t);

void __lockfunc _spin_lock_irq(spinlock_t *lock)	__acquires(spinlock_t);			// 自旋锁加锁并且禁用本地中断(不保存flag)
void __lockfunc _spin_lock_bh(spinlock_t *lock)		__acquires(spinlock_t);			// 自旋锁加锁并禁用软中断
void __lockfunc _read_lock_irq(rwlock_t *lock)		__acquires(rwlock_t);
void __lockfunc _read_lock_bh(rwlock_t *lock)		__acquires(rwlock_t);
void __lockfunc _write_lock_irq(rwlock_t *lock)		__acquires(rwlock_t);
void __lockfunc _write_lock_bh(rwlock_t *lock)		__acquires(rwlock_t);

void __lockfunc _spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)	__releases(spinlock_t);		// 自旋锁释放锁
void __lockfunc _spin_unlock_irq(spinlock_t *lock)				__releases(spinlock_t);		// 自旋锁释放锁
void __lockfunc _spin_unlock_bh(spinlock_t *lock)				__releases(spinlock_t);		// 自旋锁释放锁
void __lockfunc _read_unlock_irqrestore(rwlock_t *lock, unsigned long flags)	__releases(rwlock_t);
void __lockfunc _read_unlock_irq(rwlock_t *lock)				__releases(rwlock_t);
void __lockfunc _read_unlock_bh(rwlock_t *lock)					__releases(rwlock_t);
void __lockfunc _write_unlock_irqrestore(rwlock_t *lock, unsigned long flags)	__releases(rwlock_t);
void __lockfunc _write_unlock_irq(rwlock_t *lock)				__releases(rwlock_t);
void __lockfunc _write_unlock_bh(rwlock_t *lock)				__releases(rwlock_t);

int __lockfunc _spin_trylock_bh(spinlock_t *lock);
int __lockfunc generic_raw_read_trylock(rwlock_t *lock);
int in_lock_functions(unsigned long addr);

#else		// !CONFIG_SMP

#define in_lock_functions(ADDR) 0

#if !defined(CONFIG_PREEMPT) && !defined(CONFIG_DEBUG_SPINLOCK)
# define _atomic_dec_and_lock(atomic,lock) atomic_dec_and_test(atomic)
# define ATOMIC_DEC_AND_LOCK
#endif

#ifdef CONFIG_DEBUG_SPINLOCK
 
#define SPINLOCK_MAGIC	0x1D244B3C
typedef struct {
	unsigned long magic;
	volatile unsigned long lock;
	volatile unsigned int babble;
	const char *module;
	char *owner;
	int oline;
} spinlock_t;
#define SPIN_LOCK_UNLOCKED (spinlock_t) { SPINLOCK_MAGIC, 0, 10, __FILE__ , NULL, 0}

#define spin_lock_init(x) \
	do { \
		(x)->magic = SPINLOCK_MAGIC; \
		(x)->lock = 0; \
		(x)->babble = 5; \
		(x)->module = __FILE__; \
		(x)->owner = NULL; \
		(x)->oline = 0; \
	} while (0)

#define CHECK_LOCK(x) \
	do { \
	 	if ((x)->magic != SPINLOCK_MAGIC) { \
			printk(KERN_ERR "%s:%d: spin_is_locked on uninitialized spinlock %p.\n", \
					__FILE__, __LINE__, (x)); \
		} \
	} while(0)

#define _raw_spin_lock(x)		\
	do { \
	 	CHECK_LOCK(x); \
		if ((x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_lock(%s:%p) already locked by %s/%d\n", \
					__FILE__,__LINE__, (x)->module, \
					(x), (x)->owner, (x)->oline); \
		} \
		(x)->lock = 1; \
		(x)->owner = __FILE__; \
		(x)->oline = __LINE__; \
	} while (0)

/* without debugging, spin_is_locked on UP always says
 * FALSE. --> printk if already locked. */
#define spin_is_locked(x) \
	({ \
	 	CHECK_LOCK(x); \
		if ((x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_is_locked(%s:%p) already locked by %s/%d\n", \
					__FILE__,__LINE__, (x)->module, \
					(x), (x)->owner, (x)->oline); \
		} \
		0; \
	})

/* with debugging, assert_spin_locked() on UP does check
 * the lock value properly */
#define assert_spin_locked(x) \
	({ \
		CHECK_LOCK(x); \
		BUG_ON(!(x)->lock); \
	})

/* without debugging, spin_trylock on UP always says
 * TRUE. --> printk if already locked. */
#define _raw_spin_trylock(x) \
	({ \
	 	CHECK_LOCK(x); \
		if ((x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_trylock(%s:%p) already locked by %s/%d\n", \
					__FILE__,__LINE__, (x)->module, \
					(x), (x)->owner, (x)->oline); \
		} \
		(x)->lock = 1; \
		(x)->owner = __FILE__; \
		(x)->oline = __LINE__; \
		1; \
	})

#define spin_unlock_wait(x)	\
	do { \
	 	CHECK_LOCK(x); \
		if ((x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_unlock_wait(%s:%p) owned by %s/%d\n", \
					__FILE__,__LINE__, (x)->module, (x), \
					(x)->owner, (x)->oline); \
		}\
	} while (0)

#define _raw_spin_unlock(x) \
	do { \
	 	CHECK_LOCK(x); \
		if (!(x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_unlock(%s:%p) not locked\n", \
					__FILE__,__LINE__, (x)->module, (x));\
		} \
		(x)->lock = 0; \
	} while (0)
#else
/*
 * gcc versions before ~2.95 have a nasty bug with empty initializers.
 */
#if (__GNUC__ > 2)
  typedef struct { } spinlock_t;
  #define SPIN_LOCK_UNLOCKED (spinlock_t) { }
#else
  typedef struct { int gcc_is_buggy; } spinlock_t;
  #define SPIN_LOCK_UNLOCKED (spinlock_t) { 0 }
#endif

/*
 * 如果未设置 CONFIG_SMP，则将 _raw_ 定义声明为 nops
 */
#define spin_lock_init(lock)	do { (void)(lock); } while(0)
#define _raw_spin_lock(lock)	do { (void)(lock); } while(0)
#define spin_is_locked(lock)	((void)(lock), 0)
#define assert_spin_locked(lock)	do { (void)(lock); } while(0)
#define _raw_spin_trylock(lock)	(((void)(lock), 1))
#define spin_unlock_wait(lock)	(void)(lock)
#define _raw_spin_unlock(lock) do { (void)(lock); } while(0)
#endif /* CONFIG_DEBUG_SPINLOCK */

/* RW spinlocks: No debug version */

#if (__GNUC__ > 2)
  typedef struct { } rwlock_t;
  #define RW_LOCK_UNLOCKED (rwlock_t) { }
#else
  typedef struct { int gcc_is_buggy; } rwlock_t;
  #define RW_LOCK_UNLOCKED (rwlock_t) { 0 }
#endif

#define rwlock_init(lock)	do { (void)(lock); } while(0)
#define _raw_read_lock(lock)	do { (void)(lock); } while(0)
#define _raw_read_unlock(lock)	do { (void)(lock); } while(0)
#define _raw_write_lock(lock)	do { (void)(lock); } while(0)
#define _raw_write_unlock(lock)	do { (void)(lock); } while(0)
#define read_can_lock(lock)	(((void)(lock), 1))
#define write_can_lock(lock)	(((void)(lock), 1))
#define _raw_read_trylock(lock) ({ (void)(lock); (1); })
#define _raw_write_trylock(lock) ({ (void)(lock); (1); })

#define _spin_trylock(lock)	({preempt_disable(); _raw_spin_trylock(lock) ? \
				1 : ({preempt_enable(); 0;});})

#define _read_trylock(lock)	({preempt_disable();_raw_read_trylock(lock) ? \
				1 : ({preempt_enable(); 0;});})

#define _write_trylock(lock)	({preempt_disable(); _raw_write_trylock(lock) ? \
				1 : ({preempt_enable(); 0;});})

#define _spin_trylock_bh(lock)	({preempt_disable(); local_bh_disable(); \
				_raw_spin_trylock(lock) ? \
				1 : ({preempt_enable(); local_bh_enable(); 0;});})

#define _spin_lock(lock)	\
do { \
	preempt_disable(); \
	_raw_spin_lock(lock); \
	__acquire(lock); \
} while(0)

#define _write_lock(lock) \
do { \
	preempt_disable(); \
	_raw_write_lock(lock); \
	__acquire(lock); \
} while(0)
 
#define _read_lock(lock)	\
do { \
	preempt_disable(); \
	_raw_read_lock(lock); \
	__acquire(lock); \
} while(0)

#define _spin_unlock(lock) \
do { \
	_raw_spin_unlock(lock); \
	preempt_enable(); \
	__release(lock); \
} while (0)

#define _write_unlock(lock) \
do { \
	_raw_write_unlock(lock); \
	preempt_enable(); \
	__release(lock); \
} while(0)

#define _read_unlock(lock) \
do { \
	_raw_read_unlock(lock); \
	preempt_enable(); \
	__release(lock); \
} while(0)

#define _spin_lock_irqsave(lock, flags) \
do {	\
	local_irq_save(flags); \
	preempt_disable(); \
	_raw_spin_lock(lock); \
	__acquire(lock); \
} while (0)

#define _spin_lock_irq(lock) \
do { \
	local_irq_disable(); \
	preempt_disable(); \
	_raw_spin_lock(lock); \
	__acquire(lock); \
} while (0)

#define _spin_lock_bh(lock) \
do { \
	local_bh_disable(); \
	preempt_disable(); \
	_raw_spin_lock(lock); \
	__acquire(lock); \
} while (0)

#define _read_lock_irqsave(lock, flags) \
do {	\
	local_irq_save(flags); \
	preempt_disable(); \
	_raw_read_lock(lock); \
	__acquire(lock); \
} while (0)

#define _read_lock_irq(lock) \
do { \
	local_irq_disable(); \
	preempt_disable(); \
	_raw_read_lock(lock); \
	__acquire(lock); \
} while (0)

#define _read_lock_bh(lock) \
do { \
	local_bh_disable(); \
	preempt_disable(); \
	_raw_read_lock(lock); \
	__acquire(lock); \
} while (0)

#define _write_lock_irqsave(lock, flags) \
do {	\
	local_irq_save(flags); \
	preempt_disable(); \
	_raw_write_lock(lock); \
	__acquire(lock); \
} while (0)

#define _write_lock_irq(lock) \
do { \
	local_irq_disable(); \
	preempt_disable(); \
	_raw_write_lock(lock); \
	__acquire(lock); \
} while (0)

#define _write_lock_bh(lock) \
do { \
	local_bh_disable(); \
	preempt_disable(); \
	_raw_write_lock(lock); \
	__acquire(lock); \
} while (0)

#define _spin_unlock_irqrestore(lock, flags) \
do { \
	_raw_spin_unlock(lock); \
	local_irq_restore(flags); \
	preempt_enable(); \
	__release(lock); \
} while (0)

#define _spin_unlock_irq(lock) \
do { \
	_raw_spin_unlock(lock); \
	local_irq_enable(); \
	preempt_enable(); \
	__release(lock); \
} while (0)

#define _spin_unlock_bh(lock) \
do { \
	_raw_spin_unlock(lock); \
	preempt_enable(); \
	local_bh_enable(); \
	__release(lock); \
} while (0)

#define _write_unlock_bh(lock) \
do { \
	_raw_write_unlock(lock); \
	preempt_enable(); \
	local_bh_enable(); \
	__release(lock); \
} while (0)

#define _read_unlock_irqrestore(lock, flags) \
do { \
	_raw_read_unlock(lock); \
	local_irq_restore(flags); \
	preempt_enable(); \
	__release(lock); \
} while (0)

#define _write_unlock_irqrestore(lock, flags) \
do { \
	_raw_write_unlock(lock); \
	local_irq_restore(flags); \
	preempt_enable(); \
	__release(lock); \
} while (0)

#define _read_unlock_irq(lock)	\
do { \
	_raw_read_unlock(lock);	\
	local_irq_enable();	\
	preempt_enable();	\
	__release(lock); \
} while (0)

#define _read_unlock_bh(lock)	\
do { \
	_raw_read_unlock(lock);	\
	local_bh_enable();	\
	preempt_enable();	\
	__release(lock); \
} while (0)

#define _write_unlock_irq(lock)	\
do { \
	_raw_write_unlock(lock);	\
	local_irq_enable();	\
	preempt_enable();	\
	__release(lock); \
} while (0)

#endif /* !SMP */

/*
 * 定义各种 spin_lock 和 rw_lock 方法。
 * 请注意，无论是否设置了 CONFIG_SMP 或 CONFIG_PREEMPT，
 * 我们都会定义这些。在不需要的情况下，各种方法被定义为 nops。
 */
#define spin_trylock(lock)	__cond_lock(_spin_trylock(lock))		// 加锁宏
#define read_trylock(lock)	__cond_lock(_read_trylock(lock))
#define write_trylock(lock)	__cond_lock(_write_trylock(lock))

#define spin_lock(lock)		_spin_lock(lock)		// 加锁宏
#define write_lock(lock)	_write_lock(lock)
#define read_lock(lock)		_read_lock(lock)

#ifdef CONFIG_SMP
#define spin_lock_irqsave(lock, flags)	flags = _spin_lock_irqsave(lock)
#define read_lock_irqsave(lock, flags)	flags = _read_lock_irqsave(lock)
#define write_lock_irqsave(lock, flags)	flags = _write_lock_irqsave(lock)
#else
#define spin_lock_irqsave(lock, flags)	_spin_lock_irqsave(lock, flags)
#define read_lock_irqsave(lock, flags)	_read_lock_irqsave(lock, flags)
#define write_lock_irqsave(lock, flags)	_write_lock_irqsave(lock, flags)
#endif

#define spin_lock_irq(lock)		_spin_lock_irq(lock)
#define spin_lock_bh(lock)		_spin_lock_bh(lock)

#define read_lock_irq(lock)		_read_lock_irq(lock)
#define read_lock_bh(lock)		_read_lock_bh(lock)

#define write_lock_irq(lock)		_write_lock_irq(lock)
#define write_lock_bh(lock)		_write_lock_bh(lock)

#define spin_unlock(lock)	_spin_unlock(lock)
#define write_unlock(lock)	_write_unlock(lock)
#define read_unlock(lock)	_read_unlock(lock)

#define spin_unlock_irqrestore(lock, flags)	_spin_unlock_irqrestore(lock, flags)
#define spin_unlock_irq(lock)		_spin_unlock_irq(lock)
#define spin_unlock_bh(lock)		_spin_unlock_bh(lock)

#define read_unlock_irqrestore(lock, flags)	_read_unlock_irqrestore(lock, flags)
#define read_unlock_irq(lock)			_read_unlock_irq(lock)
#define read_unlock_bh(lock)			_read_unlock_bh(lock)

#define write_unlock_irqrestore(lock, flags)	_write_unlock_irqrestore(lock, flags)
#define write_unlock_irq(lock)			_write_unlock_irq(lock)
#define write_unlock_bh(lock)			_write_unlock_bh(lock)

#define spin_trylock_bh(lock)			__cond_lock(_spin_trylock_bh(lock))

#define spin_trylock_irq(lock) \
({ \
	local_irq_disable(); \
	_spin_trylock(lock) ? \
	1 : ({local_irq_enable(); 0; }); \
})

#define spin_trylock_irqsave(lock, flags) \
({ \
	local_irq_save(flags); \
	_spin_trylock(lock) ? \
	1 : ({local_irq_restore(flags); 0;}); \
})

#ifdef CONFIG_LOCKMETER
extern void _metered_spin_lock   (spinlock_t *lock);
extern void _metered_spin_unlock (spinlock_t *lock);
extern int  _metered_spin_trylock(spinlock_t *lock);
extern void _metered_read_lock    (rwlock_t *lock);
extern void _metered_read_unlock  (rwlock_t *lock);
extern void _metered_write_lock   (rwlock_t *lock);
extern void _metered_write_unlock (rwlock_t *lock);
extern int  _metered_read_trylock (rwlock_t *lock);
extern int  _metered_write_trylock(rwlock_t *lock);
#endif

/* "lock on reference count zero" */
#ifndef ATOMIC_DEC_AND_LOCK
#include <asm/atomic.h>
extern int _atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock);
#endif

#define atomic_dec_and_lock(atomic,lock) __cond_lock(_atomic_dec_and_lock(atomic,lock))

/*
 *  bit-based spin_lock()
 *
 * Don't use this unless you really need to: spin_lock() and spin_unlock()
 * are significantly faster.
 */
static inline void bit_spin_lock(int bitnum, unsigned long *addr)
{
	/*
	 * Assuming the lock is uncontended, this never enters
	 * the body of the outer loop. If it is contended, then
	 * within the inner loop a non-atomic test is used to
	 * busywait with less bus contention for a good time to
	 * attempt to acquire the lock bit.
	 */
	preempt_disable();
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	while (test_and_set_bit(bitnum, addr)) {
		while (test_bit(bitnum, addr)) {
			preempt_enable();
			cpu_relax();
			preempt_disable();
		}
	}
#endif
	__acquire(bitlock);
}

/*
 * Return true if it was acquired
 */
static inline int bit_spin_trylock(int bitnum, unsigned long *addr)
{
	preempt_disable();	
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	if (test_and_set_bit(bitnum, addr)) {
		preempt_enable();
		return 0;
	}
#endif
	__acquire(bitlock);
	return 1;
}

/*
 *  bit-based spin_unlock()
 */
static inline void bit_spin_unlock(int bitnum, unsigned long *addr)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	BUG_ON(!test_bit(bitnum, addr));
	smp_mb__before_clear_bit();
	clear_bit(bitnum, addr);
#endif
	preempt_enable();
	__release(bitlock);
}

/*
 * Return true if the lock is held.
 */
static inline int bit_spin_is_locked(int bitnum, unsigned long *addr)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	return test_bit(bitnum, addr);
#elif defined CONFIG_PREEMPT
	return preempt_count();
#else
	return 1;
#endif
}

#define DEFINE_SPINLOCK(x) spinlock_t x = SPIN_LOCK_UNLOCKED
#define DEFINE_RWLOCK(x) rwlock_t x = RW_LOCK_UNLOCKED

/**
 * spin_can_lock - would spin_trylock() succeed?
 * @lock: the spinlock in question.
 */
#define spin_can_lock(lock)		(!spin_is_locked(lock))

#endif /* __LINUX_SPINLOCK_H */
