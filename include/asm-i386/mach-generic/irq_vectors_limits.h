#ifndef _ASM_IRQ_VECTORS_LIMITS_H
#define _ASM_IRQ_VECTORS_LIMITS_H

/*
 * 对于 Summit 或通用（即安装程序）内核，我们有很多 IO APIC，
 * 即使使用 uni-proc 内核，因此请使用大数组。
 *
 * This value should be the same in both the generic and summit subarches.
 * Change one, change 'em both.
 */
#define NR_IRQS	224
#define NR_IRQ_VECTORS	1024

#endif /* _ASM_IRQ_VECTORS_LIMITS_H */
