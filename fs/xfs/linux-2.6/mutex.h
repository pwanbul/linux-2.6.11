/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_SUPPORT_MUTEX_H__
#define __XFS_SUPPORT_MUTEX_H__

#include <linux/spinlock.h>
#include <asm/semaphore.h>

/*
 * 将互斥锁从 IRIX 映射到 Linux 信号量。
 * Destroy 只是简单地初始化为 -99，这应该会阻止所有其他调用者。
 *
 * mutex_t使用semaphore实现
 */
#define MUTEX_DEFAULT		0x0
typedef struct semaphore	mutex_t;

#define mutex_init(lock, type, name)		sema_init(lock, 1)
#define mutex_destroy(lock)			sema_init(lock, -99)
#define mutex_lock(lock, num)			down(lock)
#define mutex_trylock(lock)			(down_trylock(lock) ? 0 : 1)
#define mutex_unlock(lock)			up(lock)

#endif /* __XFS_SUPPORT_MUTEX_H__ */
