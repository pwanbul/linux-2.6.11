#ifndef _ASM_GENERIC_PERCPU_H_
#define _ASM_GENERIC_PERCPU_H_
#include <linux/compiler.h>

// 静态的per cpu

#define __GENERIC_PER_CPU
#ifdef CONFIG_SMP       // SMP环境

extern unsigned long __per_cpu_offset[NR_CPUS];     // 保存元素数据到其"数组元素"之间偏移量

/* Separate out the type, so (int[3], foo) works. */
#define DEFINE_PER_CPU(type, name) \        // 定义一个per cpu变量，变量收集在自定义的section中
    __attribute__((__section__(".data.percpu"))) __typeof__(type) per_cpu__##name

/* var is in discarded region: offset to particular copy we want var在废弃区域中：我们想要的特定副本的偏移量*/
/*
 * 题外话：
 * 若 pointer-expression 为指向函数指针，则解引用运算符的结果为该函数的函数指代器。
 * 若 pointer-expression 为指向对象指针，则结果为指代被指向对象的左值表达式。
*/
#define per_cpu(var, cpu) (*RELOC_HIDE(&per_cpu__##var, __per_cpu_offset[cpu]))     // 获取指定cpu对应的per cpu变量，返回的是lvalue
#define __get_cpu_var(var) per_cpu(var, smp_processor_id())         // 获取cpu的本地变量

/* A macro to avoid #include hell... */
#define percpu_modcopy(pcpudst, src, size)			\
do {								\
	unsigned int __i;					\
	for (__i = 0; __i < NR_CPUS; __i++)			\
		if (cpu_possible(__i))				\
			memcpy((pcpudst)+__per_cpu_offset[__i],	\
			       (src), (size));			\
} while (0)
#else /* ! SMP */
// UP上的处理是简单的
#define DEFINE_PER_CPU(type, name) \
    __typeof__(type) per_cpu__##name

#define per_cpu(var, cpu)			(*((void)cpu, &per_cpu__##var))
#define __get_cpu_var(var)			per_cpu__##var

#endif	/* SMP */

#define DECLARE_PER_CPU(type, name) extern __typeof__(type) per_cpu__##name

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(per_cpu__##var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(per_cpu__##var)

#endif /* _ASM_GENERIC_PERCPU_H_ */
