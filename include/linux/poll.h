#ifndef _LINUX_POLL_H
#define _LINUX_POLL_H

#include <asm/poll.h>

#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <asm/uaccess.h>

struct poll_table_struct;

/* 
 * f_op->poll 实现的结构和助手
 */
typedef void (*poll_queue_proc)(struct file *, wait_queue_head_t *, struct poll_table_struct *);

typedef struct poll_table_struct {
	poll_queue_proc qproc;
} poll_table;

static inline void poll_wait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p)
{
	if (p && wait_address)
		p->qproc(filp, wait_address, p);
}

static inline void init_poll_funcptr(poll_table *pt, poll_queue_proc qproc)
{
	pt->qproc = qproc;
}

/*
 * sys_pollsys_poll 的结构和帮助程序
 */
struct poll_wqueues {
	poll_table pt;
	struct poll_table_page * table;
	int error;
};

extern void poll_initwait(struct poll_wqueues *pwq);
extern void poll_freewait(struct poll_wqueues *pwq);

/*
 * fd_set 的可扩展版本。
 */

typedef struct {
	unsigned long *in, *out, *ex;
	unsigned long *res_in, *res_out, *res_ex;
} fd_set_bits;

/*
 * How many longwords for "nr" bits?
 * 计算nr需要多个bitmap来表示，每个bitmap32位
 */
#define FDS_BITPERLONG	(8*sizeof(long))
#define FDS_LONGS(nr)	(((nr)+FDS_BITPERLONG-1)/FDS_BITPERLONG)
#define FDS_BYTES(nr)	(FDS_LONGS(nr)*sizeof(long))

/*
 * 我们在这里执行 VERIFY_WRITE，即使我们这次只是在阅读：
 * 我们最终会写信给它..
 *
 * 使用“无符号长”访问让用户模式 fd_set 长对齐。
 */
static inline
int get_fd_set(unsigned long nr, void __user *ufdset, unsigned long *fdset)
{
	nr = FDS_BYTES(nr);
	if (ufdset) {
		int error;
		error = verify_area(VERIFY_WRITE, ufdset, nr);
		if (!error && __copy_from_user(fdset, ufdset, nr))      // from ufdset to fdset
			error = -EFAULT;
		return error;
	}
	memset(fdset, 0, nr);
	return 0;
}

static inline unsigned long __must_check
set_fd_set(unsigned long nr, void __user *ufdset, unsigned long *fdset)
{
	if (ufdset)
		return __copy_to_user(ufdset, fdset, FDS_BYTES(nr));
	return 0;
}

static inline
void zero_fd_set(unsigned long nr, unsigned long *fdset)
{
	memset(fdset, 0, FDS_BYTES(nr));
}

extern int do_select(int n, fd_set_bits *fds, long *timeout);

#endif /* KERNEL */

#endif /* _LINUX_POLL_H */
