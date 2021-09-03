/* rwsem-spinlock.h: fallback C implementation
 *
 * Copyright (c) 2001   David Howells (dhowells@redhat.com).
 * - Derived partially from ideas by Andrea Arcangeli <andrea@suse.de>
 * - Derived also from comments by Linus
 */

#ifndef _LINUX_RWSEM_SPINLOCK_H
#define _LINUX_RWSEM_SPINLOCK_H

#ifndef _LINUX_RWSEM_H
#error "please don't include linux/rwsem-spinlock.h directly, use linux/rwsem.h instead"
#endif

#include <linux/spinlock.h>
#include <linux/list.h>

#ifdef __KERNEL__

#include <linux/types.h>

struct rwsem_waiter;

/*
 * the rw-semaphore definition
 * - 如果activity为 0，则没有活动的读者或作者
 * - 如果activity是 +ve 那么这就是活跃读者的数量
 * - 如果activity为 -1，则有一个活动写者
 * - 如果wait_list 不为空，则有进程在等待信号量
 *
 * 通用实现
 */
struct rw_semaphore {
	__s32			activity;		// 非atomic_t，因为有自旋锁保护
	spinlock_t		wait_lock;
	struct list_head	wait_list;
#if RWSEM_DEBUG
	int			debug;
#endif
};

/*
 * initialisation
 */
#if RWSEM_DEBUG
#define __RWSEM_DEBUG_INIT      , 0
#else
#define __RWSEM_DEBUG_INIT	/* */
#endif

#define __RWSEM_INITIALIZER(name) \
{ 0, SPIN_LOCK_UNLOCKED, LIST_HEAD_INIT((name).wait_list) __RWSEM_DEBUG_INIT }

#define DECLARE_RWSEM(name) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name)

extern void FASTCALL(init_rwsem(struct rw_semaphore *sem));
extern void FASTCALL(__down_read(struct rw_semaphore *sem));
extern int FASTCALL(__down_read_trylock(struct rw_semaphore *sem));
extern void FASTCALL(__down_write(struct rw_semaphore *sem));
extern int FASTCALL(__down_write_trylock(struct rw_semaphore *sem));
extern void FASTCALL(__up_read(struct rw_semaphore *sem));
extern void FASTCALL(__up_write(struct rw_semaphore *sem));
extern void FASTCALL(__downgrade_write(struct rw_semaphore *sem));

#endif /* __KERNEL__ */
#endif /* _LINUX_RWSEM_SPINLOCK_H */
