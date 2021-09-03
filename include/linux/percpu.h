#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H
#include <linux/spinlock.h> /* For preempt_disable() */
#include <linux/slab.h> /* For kmalloc() */
#include <linux/smp.h>
#include <linux/string.h> /* For memset() */
#include <asm/percpu.h>

// 动态的per cpu

/* 足以涵盖内核中的所有 DEFINE_PER_CPU，包括模块。 */
#ifndef PERCPU_ENOUGH_ROOM
#define PERCPU_ENOUGH_ROOM 32768
#endif

/* 必须是左值。 */
#define get_cpu_var(var) (*({ preempt_disable(); &__get_cpu_var(var); }))       // 防止在引用var期间被抢占后切换到其他CPU上运行
#define put_cpu_var(var) preempt_enable()       // 开启抢占

#ifdef CONFIG_SMP

struct percpu_data {
	void *ptrs[NR_CPUS];
	void *blkp;
};

/* 
 * Use this to get to a cpu's version of the per-cpu object allocated using
 * alloc_percpu.  Non-atomic access to the current CPU's version should
 * probably be combined with get_cpu()/put_cpu().
 */ 
#define per_cpu_ptr(ptr, cpu)                   \
({                                              \
        struct percpu_data *__p = (struct percpu_data *)~(unsigned long)(ptr); \
        (__typeof__(ptr))__p->ptrs[(cpu)];	\
})

extern void *__alloc_percpu(size_t size, size_t align);
extern void free_percpu(const void *);

#else /* !CONFIG_SMP */

#define per_cpu_ptr(ptr, cpu) (ptr)

static inline void *__alloc_percpu(size_t size, size_t align)
{
	void *ret = kmalloc(size, GFP_KERNEL);
	if (ret)
		memset(ret, 0, size);
	return ret;
}
static inline void free_percpu(const void *ptr)
{	
	kfree(ptr);
}

#endif /* CONFIG_SMP */

/* 常见情况的简单包装：零内存. */
#define alloc_percpu(type) \
	((type *)(__alloc_percpu(sizeof(type), __alignof__(type))))

#endif /* __LINUX_PERCPU_H */
