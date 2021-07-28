/*
 * This file contains the procedures for the handling of select and poll
 *
 * Created for Linux based loosely upon Mathius Lattner's minix
 * patches by Peter MacDonald. Heavily edited by Linus.
 *
 *  4 February 1994
 *     COFF/ELF binary emulation. If the process has the STICKY_TIMEOUTS
 *     flag set in its personality we do *not* modify the given timeout
 *     parameter to reflect time remaining.
 *
 *  24 January 2000
 *     Changed sys_poll()/do_poll() to use PAGE_SIZE chunk-based allocation 
 *     of fds to overcome nfds < 16390 descriptors limit (Tigran Aivazian).
 */

#include <linux/syscalls.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/poll.h>
#include <linux/personality.h> /* for STICKY_TIMEOUTS */
#include <linux/file.h>
#include <linux/fs.h>

#include <asm/uaccess.h>

#define ROUND_UP(x,y) (((x)+(y)-1)/(y))
#define DEFAULT_POLLMASK (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM)

struct poll_table_entry {
	struct file * filp;
	wait_queue_t wait;
	wait_queue_head_t * wait_address;
};

struct poll_table_page {
	struct poll_table_page * next;
	struct poll_table_entry * entry;
	struct poll_table_entry entries[0];
};

#define POLL_TABLE_FULL(table) \
	((unsigned long)((table)->entry+1) > PAGE_SIZE + (unsigned long)(table))

/*
 * 好的，Peter 做了一个复杂但简单的 multiple_wait() 函数。
 * 我已经重写了这个，采取了一些捷径：这段代码可能不容易遵循，但它应该没有竞争条件，而且很实用。
 * 如果您了解我在这里所做的事情，那么您就会了解 linux sleep/wakeup 机制是如何工作的。
 *
 * 两个非常简单的过程， poll_wait() 和 poll_freewait() 完成所有工作。
 * poll_wait() 是在 <linux/poll.h> 中定义的内联函数，因为所有 select/poll
 * 函数都必须调用它来向轮询表添加条目。
 */
// 前置声明
void __pollwait(struct file *filp, wait_queue_head_t *wait_address, poll_table *p);

void poll_initwait(struct poll_wqueues *pwq)
{
	init_poll_funcptr(&pwq->pt, __pollwait);
	pwq->error = 0;
	pwq->table = NULL;
}

EXPORT_SYMBOL(poll_initwait);

void poll_freewait(struct poll_wqueues *pwq)
{
	struct poll_table_page * p = pwq->table;
	while (p) {
		struct poll_table_entry * entry;
		struct poll_table_page *old;

		entry = p->entry;
		do {
			entry--;
			remove_wait_queue(entry->wait_address,&entry->wait);
			fput(entry->filp);
		} while (entry > p->entries);
		old = p;
		p = p->next;
		free_page((unsigned long) old);
	}
}

EXPORT_SYMBOL(poll_freewait);

void __pollwait(struct file *filp, wait_queue_head_t *wait_address, poll_table *_p)
{
	struct poll_wqueues *p = container_of(_p, struct poll_wqueues, pt);
	struct poll_table_page *table = p->table;

	if (!table || POLL_TABLE_FULL(table)) {
		struct poll_table_page *new_table;

		new_table = (struct poll_table_page *) __get_free_page(GFP_KERNEL);
		if (!new_table) {
			p->error = -ENOMEM;
			__set_current_state(TASK_RUNNING);
			return;
		}
		new_table->entry = new_table->entries;
		new_table->next = table;
		p->table = new_table;
		table = new_table;
	}

	/* Add a new entry */
	{
		struct poll_table_entry * entry = table->entry;
		table->entry = entry+1;
	 	get_file(filp);
	 	entry->filp = filp;
		entry->wait_address = wait_address;
		init_waitqueue_entry(&entry->wait, current);
		add_wait_queue(wait_address,&entry->wait);
	}
}

#define FDS_IN(fds, n)		(fds->in + n)
#define FDS_OUT(fds, n)		(fds->out + n)
#define FDS_EX(fds, n)		(fds->ex + n)

#define BITS(fds, n)	(*FDS_IN(fds, n)|*FDS_OUT(fds, n)|*FDS_EX(fds, n))

static int max_select_fd(unsigned long n, fd_set_bits *fds)
{
	unsigned long *open_fds;
	unsigned long set;
	int max;

	/* 先处理最后一个不完整的长字 */
	set = ~(~0UL << (n & (__NFDBITS-1)));
	n /= __NFDBITS;
	open_fds = current->files->open_fds->fds_bits+n;
	max = 0;
	if (set) {
		set &= BITS(fds, n);
		if (set) {
			if (!(set & ~*open_fds))
				goto get_max;
			return -EBADF;
		}
	}
	while (n) {
		open_fds--;
		n--;
		set = BITS(fds, n);
		if (!set)
			continue;
		if (set & ~*open_fds)
			return -EBADF;
		if (max)
			continue;
get_max:
		do {
			max++;
			set >>= 1;
		} while (set);
		max += n * __NFDBITS;
	}

	return max;
}

#define BIT(i)		(1UL << ((i)&(__NFDBITS-1)))
#define MEM(i,m)	((m)+(unsigned)(i)/__NFDBITS)
#define ISSET(i,m)	(((i)&*(m)) != 0)
#define SET(i,m)	(*(m) |= (i))

#define POLLIN_SET (POLLRDNORM | POLLRDBAND | POLLIN | POLLHUP | POLLERR)
#define POLLOUT_SET (POLLWRBAND | POLLWRNORM | POLLOUT | POLLERR)
#define POLLEX_SET (POLLPRI)

// select核心
int do_select(int n, fd_set_bits *fds, long *timeout)
{
	struct poll_wqueues table;
	poll_table *wait;   // 指向函数的指针的指针
	int retval, i;
	long __timeout = *timeout;

 	spin_lock(&current->files->file_lock);
	retval = max_select_fd(n, fds);
	spin_unlock(&current->files->file_lock);

	if (retval < 0)
		return retval;
	n = retval;

	poll_initwait(&table);  // 初始化轮询表
	wait = &table.pt;
	if (!__timeout)
		wait = NULL;
	retval = 0;
	for (;;) {
		unsigned long *rinp, *routp, *rexp, *inp, *outp, *exp;

		set_current_state(TASK_INTERRUPTIBLE);

		inp = fds->in; outp = fds->out; exp = fds->ex;
		rinp = fds->res_in; routp = fds->res_out; rexp = fds->res_ex;

		for (i = 0; i < n; ++rinp, ++routp, ++rexp) {
			unsigned long in, out, ex, all_bits, bit = 1, mask, j;
			unsigned long res_in = 0, res_out = 0, res_ex = 0;
			struct file_operations *f_op = NULL;
			struct file *file = NULL;

			// 每32位为一组
			in = *inp++; out = *outp++; ex = *exp++;
			all_bits = in | out | ex;
			if (all_bits == 0) {
				i += __NFDBITS;
				continue;
			}
			// 按组来处理
			for (j = 0; j < __NFDBITS; ++j, ++i, bit <<= 1) {
				if (i >= n)
					break;
				if (!(bit & all_bits))
					continue;
				file = fget(i);     // 文件描述符i有需要处理的
				if (file) {
					f_op = file->f_op;      // struct file_operations*
					mask = DEFAULT_POLLMASK;
					if (f_op && f_op->poll)
						mask = (*f_op->poll)(file, retval ? NULL : wait);
					fput(file);
					// 注意是3个if，而不是else if，返回值可理解为达成的事件的个数
					if ((mask & POLLIN_SET) && (in & bit)) {
						res_in |= bit;
						retval++;
					}
					if ((mask & POLLOUT_SET) && (out & bit)) {
						res_out |= bit;
						retval++;
					}
					if ((mask & POLLEX_SET) && (ex & bit)) {
						res_ex |= bit;
						retval++;
					}
				}
				cond_resched();
			}
			if (res_in)
				*rinp = res_in;
			if (res_out)
				*routp = res_out;
			if (res_ex)
				*rexp = res_ex;
		}
		wait = NULL;
		/* 3种情况会返回
		 * 1. 返回值非0
		 * 2. 超时时间为0
		 * 3. 有未决的信号
		 * */
		if (retval || !__timeout || signal_pending(current))
			break;
		if(table.error) {
			retval = table.error;
			break;
		}
		__timeout = schedule_timeout(__timeout);
	}
	__set_current_state(TASK_RUNNING);

	poll_freewait(&table);

	/*
	 * Up-to-date the caller timeout.
	 */
	*timeout = __timeout;
	return retval;
}

static void *select_bits_alloc(int size)
{
	return kmalloc(6 * size, GFP_KERNEL);
}

static void select_bits_free(void *bits, int size)
{
	kfree(bits);
}

/*
 * 我们实际上可以返回 ERESTARTSYS 而不是 EINTR，但我想确定这不会导致任何问题。所以我返回 EINTR 只是为了安全。
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
#define MAX_SELECT_SECONDS \
	((unsigned long) (MAX_SCHEDULE_TIMEOUT / HZ)-1)

/* select系统调用
 * fd_set在glibc中实现的时使用的定长大小的数组
 * 如果n取最大值FD_SETSZIE，那么数组会完整的拷贝过来
 * */
asmlinkage long
sys_select(int n, fd_set __user *inp, fd_set __user *outp, fd_set __user *exp, struct timeval __user *tvp)
{
	fd_set_bits fds;        // 用于处理3个传进来的值-结果参数
	char *bits;
	long timeout;
	int ret, size, max_fdset;

	timeout = MAX_SCHEDULE_TIMEOUT;     // 超时时间，滴答时间
	if (tvp) {      // tvp非NULL表示立即返回或者等待一段时间
		time_t sec, usec;

        // 检查用户空间的参数，并复制到内存空间
		if ((ret = verify_area(VERIFY_READ, tvp, sizeof(*tvp)))
		    || (ret = __get_user(sec, &tvp->tv_sec))		// 秒
		    || (ret = __get_user(usec, &tvp->tv_usec)))		// 微秒
			goto out_nofds;

		ret = -EINVAL;
		if (sec < 0 || usec < 0)
			goto out_nofds;

		// 转成滴答时间
		if ((unsigned long) sec < MAX_SELECT_SECONDS) {
			timeout = ROUND_UP(usec, 1000000/HZ);
			timeout += sec * (unsigned long) HZ;
		}
	}

	ret = -EINVAL;
	if (n < 0)
		goto out_nofds;

	/* max_fdset 可以增加，所以抓住它一次以避免竞争
	 * n为3个集合最大值加1，但最多不超过1024，current->files->max_fdset
	 * 在copy_files中被初始化1024
	 *
	 * 由于fd_set在glibc中的实现，fd的最大值不能超过1023
	 * */
	max_fdset = current->files->max_fdset;
	if (n > max_fdset)
		n = max_fdset;

	/*
	 * We need 6 bitmaps (in/out/ex for both incoming and outgoing),
	 * since we used fdset we need to allocate memory in units of
	 * long-words. 
	 */
	ret = -ENOMEM;
	size = FDS_BYTES(n);        // 返回需要的字节大小，如n取0-31，则需要返回4，取32-63，返回8
	bits = select_bits_alloc(size);     // bits = 6 * size
	if (!bits)
		goto out_nofds;
	// 每个成员均有size大小的空间
	fds.in      = (unsigned long *)  bits;
	fds.out     = (unsigned long *) (bits +   size);
	fds.ex      = (unsigned long *) (bits + 2*size);
	fds.res_in  = (unsigned long *) (bits + 3*size);
	fds.res_out = (unsigned long *) (bits + 4*size);
	fds.res_ex  = (unsigned long *) (bits + 5*size);

	// glic中的fd_set使用的是固定大小的数组，long arr[32]
	if ((ret = get_fd_set(n, inp, fds.in)) ||
	    (ret = get_fd_set(n, outp, fds.out)) ||
	    (ret = get_fd_set(n, exp, fds.ex)))
		goto out;
	// 清空输出
	zero_fd_set(n, fds.res_in);
	zero_fd_set(n, fds.res_out);
	zero_fd_set(n, fds.res_ex);

	ret = do_select(n, &fds, &timeout);         // 核心

	if (tvp && !(current->personality & STICKY_TIMEOUTS)) {
		time_t sec = 0, usec = 0;
		if (timeout) {
			sec = timeout / HZ;
			usec = timeout % HZ;
			usec *= (1000000/HZ);
		}
		put_user(sec, &tvp->tv_sec);
		put_user(usec, &tvp->tv_usec);
	}

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}

	if (set_fd_set(n, inp, fds.res_in) ||
	    set_fd_set(n, outp, fds.res_out) ||
	    set_fd_set(n, exp, fds.res_ex))
		ret = -EFAULT;

out:
	select_bits_free(bits, size);
out_nofds:
	return ret;
}

struct poll_list {
	struct poll_list *next;
	int len;
	struct pollfd entries[0];
};
// 每个页面上去掉sizeof(struct poll_list)后还能放POLLFD_PER_PAGE个struct pollfd结构
#define POLLFD_PER_PAGE  ((PAGE_SIZE-sizeof(struct poll_list)) / sizeof(struct pollfd))

static void do_pollfd(unsigned int num, struct pollfd * fdpage, poll_table ** pwait, int *count)
{
	int i;

	for (i = 0; i < num; i++) {
		int fd;
		unsigned int mask;
		struct pollfd *fdp;

		mask = 0;
		fdp = fdpage+i;			// 获取struct pollfd
		fd = fdp->fd;
		if (fd >= 0) {
			struct file * file = fget(fd);
			mask = POLLNVAL;
			if (file != NULL) {
				mask = DEFAULT_POLLMASK;
				if (file->f_op && file->f_op->poll)
					mask = file->f_op->poll(file, *pwait);
				// https://blog.csdn.net/SUKHOI27SMK/article/details/48287137
				mask &= fdp->events | POLLERR | POLLHUP;
				fput(file);
			}
			if (mask) {
				*pwait = NULL;
				(*count)++;
			}
		}
		fdp->revents = mask;
	}
}
// poll核心
static int do_poll(unsigned int nfds,  struct poll_list *list, struct poll_wqueues *wait, long timeout)
{
	int count = 0;
	poll_table* pt = &wait->pt;

	if (!timeout)		// 永久阻塞
		pt = NULL;
 
	for (;;) {
		struct poll_list *walk;
		set_current_state(TASK_INTERRUPTIBLE);
		walk = list;
		while(walk != NULL) {
			do_pollfd( walk->len, walk->entries, &pt, &count);
			walk = walk->next;
		}
		pt = NULL;
		if (count || !timeout || signal_pending(current))
			break;
		count = wait->error;
		if (count)
			break;
		timeout = schedule_timeout(timeout);
	}
	__set_current_state(TASK_RUNNING);
	return count;
}
/* poll系统调用
 * ufds是指向struct pollfd数组的指针
 * nfds是数组的大小
 *
 * 这样设计就可以避免select中的
 * 把定长数组全部拷贝过来的问题
 * */
asmlinkage long sys_poll(struct pollfd __user * ufds, unsigned int nfds, long timeout)
{
	struct poll_wqueues table;
 	int fdcount, err;
 	unsigned int i;
	struct poll_list *head;
 	struct poll_list *walk;

	/* 对 nfds 进行健全性检查 ...
	 *
	 * 相对于select，poll不限制最大描述符的取值，但是限制个数
	 * */
	if (nfds > current->files->max_fdset && nfds > OPEN_MAX)
		return -EINVAL;

	if (timeout) {		// 毫秒
		/* 小心中间值的溢出 */
		if ((unsigned long) timeout < MAX_SCHEDULE_TIMEOUT / HZ)
			timeout = (unsigned long)(timeout*HZ+999)/1000+1;
		else /* Negative or overflow */
			timeout = MAX_SCHEDULE_TIMEOUT;
	}

	poll_initwait(&table);

	head = NULL;		// 指向第一个块的指针
	walk = NULL;
	i = nfds;
	err = -ENOMEM;
	while(i!=0) {
		// 分配一个连续分段的空间，每个块最多POLLFD_PER_PAGE个struct pollfd，超出就需要重新分配块
		struct poll_list *pp;
		pp = kmalloc(sizeof(struct poll_list)+sizeof(struct pollfd)*(i>POLLFD_PER_PAGE?POLLFD_PER_PAGE:i), GFP_KERNEL);
		if(pp==NULL)
			goto out_fds;
		pp->next=NULL;
		pp->len = (i>POLLFD_PER_PAGE?POLLFD_PER_PAGE:i);		// len不含struct poll_list中固定部分的大小
		if (head == NULL)
			head = pp;
		else
			walk->next = pp;

		walk = pp;		// 执行当前块的指针
		// 从用户空间拷贝数据
		if (copy_from_user(pp->entries, ufds + nfds-i,sizeof(struct pollfd)*pp->len)) {
			err = -EFAULT;
			goto out_fds;
		}
		i -= pp->len;
	}
	fdcount = do_poll(nfds, head, &table, timeout);		// poll核心

	/* OK, now copy the revents fields back to user space. */
	walk = head;
	err = -EFAULT;
	while(walk != NULL) {
		struct pollfd *fds = walk->entries;
		int j;

		for (j=0; j < walk->len; j++, ufds++) {
			if(__put_user(fds[j].revents, &ufds->revents))
				goto out_fds;
		}
		walk = walk->next;
  	}
	err = fdcount;
	if (!fdcount && signal_pending(current))
		err = -EINTR;
out_fds:
	walk = head;
	while(walk!=NULL) {
		struct poll_list *pp = walk->next;
		kfree(walk);
		walk = pp;
	}
	poll_freewait(&table);
	return err;
}
