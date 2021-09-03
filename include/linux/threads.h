#ifndef _LINUX_THREADS_H
#define _LINUX_THREADS_H

#include <linux/config.h>

/*
 * The default limit for the nr of threads is now in
 * /proc/sys/kernel/threads-max.
 */
 
/*
 * 可在 SMP 下运行的最大支持处理器数。
 * 该值是通过配置设置设置的。
 * 最大值等于该平台上使用的位掩码的大小，即32或64。
 * 将这个设置得更小可以节省相当多的内存。
 *
 * CPU的数量
 */
#ifdef CONFIG_SMP
#define NR_CPUS		CONFIG_NR_CPUS
#else
#define NR_CPUS		1
#endif

#define MIN_THREADS_LEFT_FOR_ROOT 4

/*
 * This controls the default maximum pid allocated to a process
 */
#define PID_MAX_DEFAULT 0x8000

/*
 * A maximum of 4 million PIDs should be enough for a while:
 */
#define PID_MAX_LIMIT (sizeof(long) > 4 ? 4*1024*1024 : PID_MAX_DEFAULT)        // 32为32768个，64为位4M个

#endif
