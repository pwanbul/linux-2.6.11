/*
 * include/asm-i386/cache.h
 */
#ifndef __ARCH_I386_CACHE_H
#define __ARCH_I386_CACHE_H

#include <linux/config.h>

/* L1 cache line size */
// L1缓存行的大小
#define L1_CACHE_SHIFT	(CONFIG_X86_L1_CACHE_SHIFT)
#define L1_CACHE_BYTES	(1 << L1_CACHE_SHIFT)       // 1 << 7，即128字节

#define L1_CACHE_SHIFT_MAX 7	/* largest L1 which this arch supports */

#endif
