/*
 *  linux/mm/oom_kill.c
 * 
 *  Copyright (C)  1998,2000  Rik van Riel
 *	Thanks go out to Claus Fischer for some serious inspiration and
 *	for goading me into coding this file...
 *
 *  The routines in this file are used to kill a process when
 *  we're seriously out of memory. This gets called from kswapd()
 *  in linux/mm/vmscan.c when we really run out of memory.
 *
 *  Since we won't call these routines often (on a well-configured
 *  machine) this file will double as a 'coding guide' and a signpost
 *  for newbie kernel hackers. It features several pointers to major
 *  kernel subsystems and hints as to where to find out what things do.
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/timex.h>
#include <linux/jiffies.h>

/* #define DEBUG */

/**
 * oom_badness - 计算此任务有多糟糕的数值
 * @p: task struct of which task we should calculate
 * @p: current uptime in seconds
 *
 * 使用的公式相对简单，并在函数中内联记录。主要理由是我们想在内存不足时选择一个好的任务来杀死。
 *
 * 良好在这种情况下意味着：
 * 1) 我们失去了完成的最少工作量
 * 2) 我们恢复了大量内存
 * 3) 我们不会杀死任何无辜的吃掉大量内存的东西
 * 4) 我们想杀死最少数量的进程（一个）
 * 5) 我们尝试杀死用户希望我们杀死的进程，该算法经过精心调整以满足最小意外原则......（更改时请小心）
 */

unsigned long badness(struct task_struct *p, unsigned long uptime)
{
	unsigned long points, cpu_time, run_time, s;
	struct list_head *tsk;

	if (!p->mm)
		return 0;

	/*
	 * 进程的内存大小是坏的基础。
	 */
	points = p->mm->total_vm;

	/*
	 * 派生出很多子进程的进程可能是一个不错的选择。如果他们有自己的 mm，我们添加孩子的 vmsize。
	 * 这可以防止fork服务器用无穷无尽的孩子淹没机器。
	 */
	list_for_each(tsk, &p->children) {
		struct task_struct *chld;
		chld = list_entry(tsk, struct task_struct, sibling);
		if (chld->mm != p->mm && chld->mm)
			points += chld->mm->total_vm;
	}

	/*
	 * CPU 时间以几十秒为单位，运行时间以数千秒为单位。除了在实践中证明它工作得很好之外，没有特别的原因。
	 */
	cpu_time = (cputime_to_jiffies(p->utime) + cputime_to_jiffies(p->stime)) >> (SHIFT_HZ + 3);

	if (uptime >= p->start_time.tv_sec)
		run_time = (uptime - p->start_time.tv_sec) >> 10;
	else
		run_time = 0;

	s = int_sqrt(cpu_time);
	if (s)
		points /= s;
	s = int_sqrt(int_sqrt(run_time));
	if (s)
		points /= s;

	/*
	 * 好的过程很可能不太重要，因此将它们的坏点加倍。
	 */
	if (task_nice(p) > 0)
		points *= 2;

	/*
	 * 超级用户进程通常更重要，因此我们不太可能杀死它们。
	 */
	if (cap_t(p->cap_effective) & CAP_TO_MASK(CAP_SYS_ADMIN) ||
				p->uid == 0 || p->euid == 0)
		points /= 4;

	/*
	 * 我们不想终止具有直接硬件访问权限的进程。这不仅会弄乱硬件，
	 * 而且通常用户倾向于只在他们认为重要的应用程序上设置此标志。
	 */
	if (cap_t(p->cap_effective) & CAP_TO_MASK(CAP_SYS_RAWIO))
		points /= 4;

	/*
	 * 通过 oomkilladj 调整分数。
	 */
	if (p->oomkilladj) {
		if (p->oomkilladj > 0)
			points <<= p->oomkilladj;
		else
			points >>= -(p->oomkilladj);
	}

#ifdef DEBUG
	printk(KERN_DEBUG "OOMkill: task %d (%s) got %d points\n",
	p->pid, p->comm, points);
#endif
	return points;
}

/*
 * 简单的选择循环。我们选择了“点数”最高的过程。我们期望调用者将锁定任务列表。
 *
 * (没有 docbooked，我们不希望这个弄乱手册)
 */
static struct task_struct * select_bad_process(void)
{
	unsigned long maxpoints = 0;
	struct task_struct *g, *p;
	struct task_struct *chosen = NULL;
	struct timespec uptime;

	do_posix_clock_monotonic_gettime(&uptime);
	do_each_thread(g, p)
		/* skip the init task with pid == 1 */
		if (p->pid > 1) {
			unsigned long points;

			/*
			 * 这是在释放内存的过程中，所以在错误地杀死其他任务之前等待它完成。
			 */
			if ((unlikely(test_tsk_thread_flag(p, TIF_MEMDIE)) || (p->flags & PF_EXITING)) && !(p->flags & PF_DEAD))
				return ERR_PTR(-1UL);

			if (p->flags & PF_SWAPOFF)
				return p;

			points = badness(p, uptime.tv_sec);
			if (points > maxpoints || !chosen) {
				chosen = p;
				maxpoints = points;
			}
		}
	while_each_thread(g, p);
	return chosen;
}

/**
 * We must be careful though to never send SIGKILL a process with
 * CAP_SYS_RAW_IO set, send SIGTERM instead (but it's unlikely that
 * we select a process with CAP_SYS_RAW_IO set).
 */
static void __oom_kill_task(task_t *p)
{
	if (p->pid == 1) {
		WARN_ON(1);
		printk(KERN_WARNING "tried to kill init!\n");
		return;
	}

	task_lock(p);
	if (!p->mm || p->mm == &init_mm) {
		WARN_ON(1);
		printk(KERN_WARNING "tried to kill an mm-less task!\n");
		task_unlock(p);
		return;
	}
	task_unlock(p);
	printk(KERN_ERR "Out of Memory: Killed process %d (%s).\n", p->pid, p->comm);

	/*
	 * We give our sacrificial lamb high priority and access to
	 * all the memory it needs. That way it should be able to
	 * exit() and clear out its resources quickly...
	 */
	p->time_slice = HZ;
	set_tsk_thread_flag(p, TIF_MEMDIE);

	force_sig(SIGKILL, p);
}

static struct mm_struct *oom_kill_task(task_t *p)
{
	struct mm_struct *mm = get_task_mm(p);
	task_t * g, * q;

	if (!mm)
		return NULL;
	if (mm == &init_mm) {
		mmput(mm);
		return NULL;
	}

	__oom_kill_task(p);
	/*
	 * kill all processes that share the ->mm (i.e. all threads),
	 * but are in a different thread group
	 */
	do_each_thread(g, q)
		if (q->mm == mm && q->tgid != p->tgid)
			__oom_kill_task(q);
	while_each_thread(g, q);

	return mm;
}

static struct mm_struct *oom_kill_process(struct task_struct *p)
{
 	struct mm_struct *mm;
	struct task_struct *c;
	struct list_head *tsk;

	/* Try to kill a child first */
	list_for_each(tsk, &p->children) {
		c = list_entry(tsk, struct task_struct, sibling);
		if (c->mm == p->mm)
			continue;
		mm = oom_kill_task(c);
		if (mm)
			return mm;
	}
	return oom_kill_task(p);
}

/**
 * oom_kill - 当内存不足时杀死“最佳”进程
 *
 * 如果内存不足，我们可以选择终止随机任务（坏）、
 * 让系统崩溃（更糟）或尝试聪明地决定要终止哪个进程。
 * 请注意，我们不必在这里做到完美，我们只需要做好。
 */
void out_of_memory(int gfp_mask)
{
	struct mm_struct *mm = NULL;
	task_t * p;

	read_lock(&tasklist_lock);
retry:
	p = select_bad_process();

	if (PTR_ERR(p) == -1UL)
		goto out;

	/* Found nothing?!?! Either we hang forever, or we panic. */
	if (!p) {
		read_unlock(&tasklist_lock);
		show_free_areas();
		panic("Out of memory and no killable processes...\n");
	}

	printk("oom-killer: gfp_mask=0x%x\n", gfp_mask);
	show_free_areas();
	mm = oom_kill_process(p);
	if (!mm)
		goto retry;

 out:
	read_unlock(&tasklist_lock);
	if (mm)
		mmput(mm);

	/*
	 * Give "p" a good chance of killing itself before we
	 * retry to allocate memory.
	 */
	__set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(1);
}
