/*
 * Generic pidhash and scalable, time-bounded PID allocator
 *
 * (C) 2002-2003 William Irwin, IBM
 * (C) 2004 William Irwin, Oracle
 * (C) 2002-2004 Ingo Molnar, Red Hat
 *
 * pid-structures are backing objects for tasks sharing a given ID to chain
 * against. There is very little to them aside from hashing them and
 * parking tasks using given ID's on a list.
 *
 * The hash is always changed with the tasklist_lock write-acquired,
 * and the hash is only accessed with the tasklist_lock at least
 * read-acquired, so there's no additional SMP locking needed here.
 *
 * We have a list of bitmap pages, which bitmaps represent the PID space.
 * Allocating and freeing PIDs is completely lockless. The worst-case
 * allocation scenario when all but one out of 1 million PIDs possible are
 * allocated already: the scanning of 32 list entries and at most PAGE_SIZE
 * bytes. The typical fastpath is a single successful setbit. Freeing is O(1).
 */

/* 两个数据结构：
 * 位图和hash表
 * */


#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/hash.h>
// 散列函数，pidhash_shift用来控制hash表的长度
#define pid_hashfn(nr) hash_long((unsigned long)nr, pidhash_shift)
// 4种进程标识符的hash表
static struct hlist_head *pid_hash[PIDTYPE_MAX];        // 静态的,大小为PIDTYPE_MAX
static int pidhash_shift;       // 用来控制hash表的长度

int pid_max = PID_MAX_DEFAULT;		// 0x8000，32768
int last_pid;		// PID全局变量，初始化为0

#define RESERVED_PIDS		300

int pid_max_min = RESERVED_PIDS + 1;
int pid_max_max = PID_MAX_LIMIT;

#define PIDMAP_ENTRIES		((PID_MAX_LIMIT + 8*PAGE_SIZE - 1)/PAGE_SIZE/8)	// 1
#define BITS_PER_PAGE		(PAGE_SIZE*8)	// 0x8000，32768
#define BITS_PER_PAGE_MASK	(BITS_PER_PAGE-1)	// 0x7FFF, 32767
#define mk_pid(map, off)	(((map) - pidmap_array)*BITS_PER_PAGE + (off))
#define find_next_offset(map, off)					\
		find_next_zero_bit((map)->page, BITS_PER_PAGE, off)

/*
 * PID-map pages start out as NULL, they get allocated upon
 * first use and are never deallocated. This way a low pid_max
 * value does not cause lots of bitmaps to be allocated, but
 * the scheme scales to up to 4 million PIDs, runtime.
 */
typedef struct pidmap {
	atomic_t nr_free;       // 空位的数量
	void *page;     // 位图页
} pidmap_t;

// pid位图实例数组，实际只有一个元素，静态的
static pidmap_t pidmap_array[PIDMAP_ENTRIES] =
	 { [ 0 ... PIDMAP_ENTRIES-1 ] = { ATOMIC_INIT(BITS_PER_PAGE), NULL } };

static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(pidmap_lock);

// 回收一个PID，do_fork中调用
fastcall void free_pidmap(int pid)
{
	pidmap_t *map = pidmap_array + pid / BITS_PER_PAGE;
	int offset = pid & BITS_PER_PAGE_MASK;

	clear_bit(offset, map->page);
	atomic_inc(&map->nr_free);
}

// 分配一个PID，do_fork中调用
int alloc_pidmap(void)
{
	int i, offset, max_scan, pid, last = last_pid;
	pidmap_t *map;

	pid = last + 1;		// 按序分配一个PID
	if (pid >= pid_max)		// 超过0x8000，32768返绕到300
		pid = RESERVED_PIDS;

	offset = pid & BITS_PER_PAGE_MASK;		// 定位到在位图内的偏移， 为0表示在当前位图中，非0表示在下一个位图中
	map = &pidmap_array[pid/BITS_PER_PAGE];		// 找到该PID所在位图
	// max_scan可能是当前位图的最后一个格子，也可能是下一个位图的第一个格子
	max_scan = (pid_max + BITS_PER_PAGE - 1)/BITS_PER_PAGE - !offset;
	for (i = 0; i <= max_scan; ++i) {
		// 如果if成立，则该位图还未分配页面
		if (unlikely(!map->page)) {
			// 用于申请一个页，其内容会被清零
			unsigned long page = get_zeroed_page(GFP_KERNEL);
			/*
			 * Free the page if someone raced with us
			 * installing it:
			 */
			// 加自旋锁
			spin_lock(&pidmap_lock);
			if (map->page)		// 再次检查
				free_page(page);
			else
				map->page = (void *)page;
			spin_unlock(&pidmap_lock);
			if (unlikely(!map->page))
				break;
		}
		// 如果if成立，则该页面还有空格子
		if (likely(atomic_read(&map->nr_free))) {
			do {
				// 把map->page中的offset位置1，并放回之前的值
				if (!test_and_set_bit(offset, map->page)) {
					// map->page中的offset位之前没有被使用过，pid分配成功
					atomic_dec(&map->nr_free);	// 空格子数见1
					last_pid = pid;		// 设置全局变量
					return pid;		// 返回pid
				}
				/* 如果offset指定的位已被占用,就从offset开始在map->page中找到
                    第一个为0的位,就是下一个pid,返回值offset是距离地址map起第一个
                    为0的位,offset如果大于BITS_PER_PAGE说明已经不属于该pidmap了
                */
				offset = find_next_offset(map, offset);
				pid = mk_pid(map, offset);
			/*
			 * find_next_offset() found a bit, the pid from it
			 * is in-bounds, and if we fell back to the last
			 * bitmap block and the final block was the same
			 * as the starting point, pid is before last_pid.
			 */
			} while (offset < BITS_PER_PAGE && pid < pid_max &&
					(i != max_scan || pid < last ||
					    !((last+1) & BITS_PER_PAGE_MASK)));
		}
		/*
			在以上的代码中,比较理想的情况是找到了合适的pid,然后return,但是还有一
			些其他的情况,比如从某个offset开始直到该页结束的位都被用光了,那么根据
			find_next_offset查找到的offset就会超过BITS_PER_PAGE,此时假如还有多余的
			位图,offset置0,map指向下一个pidmap,从下一页继续查找即可;假如pid已使用到
			最后一张位图,就将map设为pidmap[0],从第一张位图开始继续查找,只不过要从
			offset 300开始查找,前提是max_scan为1,那么max_scan是否有可能不为1呢?
			
			如果最初查找空闲pid时获得的offset为0,只有一种情况,就是在内核启动过程中
			刚刚开始创建进程,即从offset为0开始查找空闲pid是肯定能查找到的,因而如果
			查找不到pid,最初的offset肯定不为0,即max_scan肯定为1,肯定还可以再执行一
			个for循环查找一次
        */
		if (map < &pidmap_array[(pid_max-1)/BITS_PER_PAGE]) {
			++map;
			offset = 0;
		} else {
			map = &pidmap_array[0];
			offset = RESERVED_PIDS;
			if (unlikely(last == offset))
				break;
		}
		pid = mk_pid(map, offset);
	}
	return -1;
}

// 查询是否有相同nr的struct pid，返回struct pid指针
struct pid * fastcall find_pid(enum pid_type type, int nr)
{
	struct hlist_node *elem;
	struct pid *pid;

	hlist_for_each_entry(pid, elem, &pid_hash[type][pid_hashfn(nr)], pid_chain) {
		if (pid->nr == nr)
			return pid;
	}
	return NULL;
}

// 把id和task_struct建立关联
int fastcall attach_pid(task_t *task, enum pid_type type, int nr)
{
	struct pid *pid, *task_pid;

	task_pid = &task->pids[type];       // 找到type对应的struct pid
	pid = find_pid(type, nr);       // 在hash表中查找是否存在有相同nr的struct pid
	// pid_chain和pid_list只会使用一个
	if (pid == NULL) {      // 没有相同nr的struct pid，则向hash表中加入
		hlist_add_head(&task_pid->pid_chain,        // 头插
				&pid_hash[type][pid_hashfn(nr)]);
		INIT_LIST_HEAD(&task_pid->pid_list);
	} else {    // 否则加入循环双向链表中
		INIT_HLIST_NODE(&task_pid->pid_chain);
		list_add_tail(&task_pid->pid_list, &pid->pid_list);
	}
	task_pid->nr = nr;

	return 0;
}

static fastcall int __detach_pid(task_t *task, enum pid_type type)
{
	struct pid *pid, *pid_next;
	int nr = 0;

	pid = &task->pids[type];
	if (!hlist_unhashed(&pid->pid_chain)) {     // 成立则在hash桶里
		hlist_del(&pid->pid_chain);     // 从hash桶里删除

		if (list_empty(&pid->pid_list))     // 不存在list_head
			nr = pid->nr;
		else {
			pid_next = list_entry(pid->pid_list.next,
						struct pid, pid_list);
			/* insert next pid from pid_list to hash */
			hlist_add_head(&pid_next->pid_chain,        // 存在则把下一个struct pid插入hash表头部
				&pid_hash[type][pid_hashfn(pid_next->nr)]);
		}
	}

	list_del(&pid->pid_list);
	pid->nr = 0;

	return nr;
}

// 从task_struct中回收pid
void fastcall detach_pid(task_t *task, enum pid_type type)
{
	int tmp, nr;

	nr = __detach_pid(task, type);
	if (!nr)        // 在type相同的情况下，是否只有task这一个进程在使用nr，如果不是，则return
		return;
    // 检查不同类型的标识符中，nr是否被占用，如果是，则return
	for (tmp = PIDTYPE_MAX; --tmp >= 0; )
		if (tmp != type && find_pid(tmp, nr))
			return;

	free_pidmap(nr);        // 释放位图
}

// 返回task_struct指针
task_t *find_task_by_pid_type(int type, int nr)
{
	struct pid *pid;

	pid = find_pid(type, nr);
	if (!pid)
		return NULL;

	return pid_task(&pid->pid_list, type);      // list_entry(elem, struct task_struct, pids[type].pid_list)
}

EXPORT_SYMBOL(find_task_by_pid_type);

/*
 * This function switches the PIDs if a non-leader thread calls
 * sys_execve() - this must be done without releasing the PID.
 * (which a detach_pid() would eventually do.)
 */
void switch_exec_pids(task_t *leader, task_t *thread)
{
	__detach_pid(leader, PIDTYPE_PID);
	__detach_pid(leader, PIDTYPE_TGID);
	__detach_pid(leader, PIDTYPE_PGID);
	__detach_pid(leader, PIDTYPE_SID);

	__detach_pid(thread, PIDTYPE_PID);
	__detach_pid(thread, PIDTYPE_TGID);

	leader->pid = leader->tgid = thread->pid;
	thread->pid = thread->tgid;

	attach_pid(thread, PIDTYPE_PID, thread->pid);
	attach_pid(thread, PIDTYPE_TGID, thread->tgid);
	attach_pid(thread, PIDTYPE_PGID, thread->signal->pgrp);
	attach_pid(thread, PIDTYPE_SID, thread->signal->session);f
	list_add_tail(&thread->tasks, &init_task.tasks);

	attach_pid(leader, PIDTYPE_PID, leader->pid);
	attach_pid(leader, PIDTYPE_TGID, leader->tgid);
	attach_pid(leader, PIDTYPE_PGID, leader->signal->pgrp);
	attach_pid(leader, PIDTYPE_SID, leader->signal->session);
}

/*
 * The pid hash table is scaled according to the amount of memory in the
 * machine.  From a minimum of 16 slots up to 4096 slots at one gigabyte or
 * more.
 */
// 初始化hash表
void __init pidhash_init(void)      // start kernel
{
	int i, j, pidhash_size;
	unsigned long megabytes = nr_kernel_pages >> (20 - PAGE_SHIFT);     // 低端内存页数       和散列度

	pidhash_shift = max(4, fls(megabytes * 4));     // fls: find last bit set.
	pidhash_shift = min(12, pidhash_shift);     // 4 <= pidhash_shift <= 12，用来控制hash表的长度
	pidhash_size = 1 << pidhash_shift;      // hash表的大小，最小16，最大4096

	printk("PID hash table entries: %d (order: %d, %Zd bytes)\n",pidhash_size, pidhash_shift, PIDTYPE_MAX * pidhash_size * sizeof(struct hlist_head));

	for (i = 0; i < PIDTYPE_MAX; i++) {
		pid_hash[i] = alloc_bootmem(pidhash_size * sizeof(*(pid_hash[i]))); // sizeof(*(pid_hash[i]))即sizeof(struct hlist_head)
		if (!pid_hash[i])
			panic("Could not alloc pidhash!\n");
		for (j = 0; j < pidhash_size; j++)
			INIT_HLIST_HEAD(&pid_hash[i][j]);
	}
}

// 初始化bitmap
void __init pidmap_init(void)       // start kernel
{
	int i;
    // 初始化第0个bitmap
	pidmap_array->page = (void *)get_zeroed_page(GFP_KERNEL);
	set_bit(0, pidmap_array->page);     // 从pidmap_array->page开始，偏移量为0的bit设置为1
	atomic_dec(&pidmap_array->nr_free);     // 空位减少1

	/*
	 * Allocate PID 0, and hash it via all PID types:
	 */

	for (i = 0; i < PIDTYPE_MAX; i++)
		attach_pid(current, i, 0);          //
}
