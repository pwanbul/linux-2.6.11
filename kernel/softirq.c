/*
 *	linux/kernel/softirq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *
 * Rewritten. Old one was good in 2.2, but in 2.3 it was immoral. --ANK (990903)
 */

#include <linux/module.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/notifier.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>

#include <asm/irq.h>
/*
   - No shared variables, all the data are CPU local.
   - If a softirq needs serialization, let it serialize itself
     by its own spinlocks.
   - Even if softirq is serialized, only local cpu is marked for
     execution. Hence, we get something sort of weak cpu binding.
     Though it is still not clear, will it result in better locality
     or will not.

   Examples:
   - NET RX softirq. It is multithreaded and does not require
     any global serialization.
   - NET TX softirq. It kicks software netdevice queues, hence
     it is logically serialized per device, but this serialization
     is invisible to common code.
   - Tasklets: serialized wrt itself.
 */

#ifndef __ARCH_IRQ_STAT
irq_cpustat_t irq_stat[NR_CPUS] ____cacheline_aligned;		// 每个CPU都一个和如中断相关的状态
EXPORT_SYMBOL(irq_stat);
#endif

// 软中断注册在这个数组中，虽然有32个元素，但是目前只有6个软中断
static struct softirq_action softirq_vec[32] __cacheline_aligned_in_smp;

static DEFINE_PER_CPU(struct task_struct *, ksoftirqd);

/*
 * 我们不能在这里无限循环以避免用户空间饥饿，
 * 但我们也不希望将最坏情况下的 1HZ 延迟引入
 * 待处理事件，因此让调度程序为我们平衡 softirq 负载。
 */
static inline void wakeup_softirqd(void)
{
	/* Interrupts are disabled: no need to stop preemption */
	struct task_struct *tsk = __get_cpu_var(ksoftirqd);

	if (tsk && tsk->state != TASK_RUNNING)
		wake_up_process(tsk);
}

/*
 * 我们重新启动 softirq 处理 MAX_SOFTIRQ_RESTART 次，
 * 然后我们回退到 softirqd。
 *
 * 这个数字是通过实验确定的。需要平衡的两件事是延迟与公平，
 * 我们希望尽快处理软中断，但它们不应该能够锁定盒子。
 */
#define MAX_SOFTIRQ_RESTART 10

asmlinkage void __do_softirq(void)
{
	struct softirq_action *h;
	__u32 pending;
	int max_restart = MAX_SOFTIRQ_RESTART;
	int cpu;

	pending = local_softirq_pending();		// 获取本地cpu的软中断位图

	local_bh_disable();			// 软中断计数器加1
	cpu = smp_processor_id();
restart:
	/* 在启用 irqs 之前重置挂起的位掩码 */
	local_softirq_pending() = 0;		// 清空本地cpu软中断位图

	local_irq_enable();		// 开启本地中断

	h = softirq_vec;	// 软中断数组

	do {
		// 从0号软中断开始检查，如果激活了，就执行
		if (pending & 1) {
			h->action(h);
			rcu_bh_qsctr_inc(cpu);
		}
		h++;
		pending >>= 1;
	} while (pending);		// 最多32个软中断，实际只有6个

	local_irq_disable();			// 关闭本地中断

	pending = local_softirq_pending();
	/* 最多10次，
	 * 如NET_RX_SOFTIRQ会在net_rx_action中再次注册自己,
	 * 不加限制的话，用户进程就会饿死
	 * */
	if (pending && --max_restart)
		goto restart;

	if (pending)
		wakeup_softirqd();		// 通过内核线程来处理剩下的软中断

	__local_bh_enable();		// 软中断计数器减1
}

#ifndef __ARCH_HAS_DO_SOFTIRQ

/* 检查软中断
 * 通常在中断处理返回之前调用
 * */
asmlinkage void do_softirq(void)
{
	__u32 pending;
	unsigned long flags;

	if (in_interrupt())		// 是否处在硬/软中断中
		return;

	local_irq_save(flags);		// 保存IF状态，并关闭本地中断

	pending = local_softirq_pending();		// 获取本地CPU中软中断位图

	if (pending)
		__do_softirq();

	local_irq_restore(flags);		// 恢复本地中断
}

EXPORT_SYMBOL(do_softirq);

#endif

void local_bh_enable(void)
{
	WARN_ON(irqs_disabled());
	/*
	 * Keep preemption disabled until we are done with
	 * softirq processing:
 	 */
 	sub_preempt_count(SOFTIRQ_OFFSET - 1);

	if (unlikely(!in_interrupt() && local_softirq_pending()))
		do_softirq();

	dec_preempt_count();
	preempt_check_resched();
}
EXPORT_SYMBOL(local_bh_enable);

#ifdef __ARCH_IRQ_EXIT_IRQS_DISABLED
# define invoke_softirq()	__do_softirq()
#else
# define invoke_softirq()	do_softirq()
#endif

/*
 * Exit an interrupt context. Process softirqs if needed and possible:
 */
void irq_exit(void)
{
	account_system_vtime(current);
	sub_preempt_count(IRQ_EXIT_OFFSET);
	if (!in_interrupt() && local_softirq_pending())
		invoke_softirq();
	preempt_enable_no_resched();
}

/*
 * 此功能必须在禁用irqs的情况下运行！
 *
 */
inline fastcall void raise_softirq_irqoff(unsigned int nr)
{
	__raise_softirq_irqoff(nr);		// 把nr设置和本地CPU相关的数据结构中

	/*
	 * 如果我们处于中断或软中断状态，我们就完成了（这也会捕获禁用软中断的代码）。
	 * 一旦我们从irq或softirq返回，我们将实际运行softirq。
	 *
	 * 否则我们唤醒ksoftirqd以确保我们尽快安排softirq。
	 */
	if (!in_interrupt())		// 是否在硬中断或软中断上下文中
		wakeup_softirqd();
}

EXPORT_SYMBOL(raise_softirq_irqoff);

// 激活软中断
void fastcall raise_softirq(unsigned int nr)
{
	unsigned long flags;

	local_irq_save(flags);
	raise_softirq_irqoff(nr);
	local_irq_restore(flags);
}

/* 设置一个软中断
 * nr表示软中断号，同时是优先级，数值越大优先级越低
 * action表示处理函数
 * */
void open_softirq(int nr, void (*action)(struct softirq_action*), void *data)
{
	softirq_vec[nr].data = data;
	softirq_vec[nr].action = action;
}

EXPORT_SYMBOL(open_softirq);

/* Tasklets */
struct tasklet_head
{
	struct tasklet_struct *list;
};

/* Some compilers disobey section attribute on statics when not
   initialized -- RR */
static DEFINE_PER_CPU(struct tasklet_head, tasklet_vec) = { NULL };
static DEFINE_PER_CPU(struct tasklet_head, tasklet_hi_vec) = { NULL };

void fastcall __tasklet_schedule(struct tasklet_struct *t)
{
	unsigned long flags;

	local_irq_save(flags);
	t->next = __get_cpu_var(tasklet_vec).list;
	__get_cpu_var(tasklet_vec).list = t;
	raise_softirq_irqoff(TASKLET_SOFTIRQ);
	local_irq_restore(flags);
}

EXPORT_SYMBOL(__tasklet_schedule);

void fastcall __tasklet_hi_schedule(struct tasklet_struct *t)
{
	unsigned long flags;

	local_irq_save(flags);
	t->next = __get_cpu_var(tasklet_hi_vec).list;
	__get_cpu_var(tasklet_hi_vec).list = t;
	raise_softirq_irqoff(HI_SOFTIRQ);
	local_irq_restore(flags);
}

EXPORT_SYMBOL(__tasklet_hi_schedule);

static void tasklet_action(struct softirq_action *a)
{
	struct tasklet_struct *list;

	local_irq_disable();
	list = __get_cpu_var(tasklet_vec).list;
	__get_cpu_var(tasklet_vec).list = NULL;
	local_irq_enable();

	while (list) {
		struct tasklet_struct *t = list;

		list = list->next;

		if (tasklet_trylock(t)) {
			if (!atomic_read(&t->count)) {
				if (!test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
					BUG();
				t->func(t->data);
				tasklet_unlock(t);
				continue;
			}
			tasklet_unlock(t);
		}

		local_irq_disable();
		t->next = __get_cpu_var(tasklet_vec).list;
		__get_cpu_var(tasklet_vec).list = t;
		__raise_softirq_irqoff(TASKLET_SOFTIRQ);
		local_irq_enable();
	}
}

static void tasklet_hi_action(struct softirq_action *a)
{
	struct tasklet_struct *list;

	local_irq_disable();
	list = __get_cpu_var(tasklet_hi_vec).list;
	__get_cpu_var(tasklet_hi_vec).list = NULL;
	local_irq_enable();

	while (list) {
		struct tasklet_struct *t = list;

		list = list->next;

		if (tasklet_trylock(t)) {
			if (!atomic_read(&t->count)) {
				if (!test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
					BUG();
				t->func(t->data);
				tasklet_unlock(t);
				continue;
			}
			tasklet_unlock(t);
		}

		local_irq_disable();
		t->next = __get_cpu_var(tasklet_hi_vec).list;
		__get_cpu_var(tasklet_hi_vec).list = t;
		__raise_softirq_irqoff(HI_SOFTIRQ);
		local_irq_enable();
	}
}


void tasklet_init(struct tasklet_struct *t,
		  void (*func)(unsigned long), unsigned long data)
{
	t->next = NULL;
	t->state = 0;
	atomic_set(&t->count, 0);
	t->func = func;
	t->data = data;
}

EXPORT_SYMBOL(tasklet_init);

void tasklet_kill(struct tasklet_struct *t)
{
	if (in_interrupt())
		printk("Attempt to kill tasklet from interrupt\n");

	while (test_and_set_bit(TASKLET_STATE_SCHED, &t->state)) {
		do
			yield();
		while (test_bit(TASKLET_STATE_SCHED, &t->state));
	}
	tasklet_unlock_wait(t);
	clear_bit(TASKLET_STATE_SCHED, &t->state);
}

EXPORT_SYMBOL(tasklet_kill);

// tasklet初始化
void __init softirq_init(void)
{
	open_softirq(TASKLET_SOFTIRQ, tasklet_action, NULL);
	open_softirq(HI_SOFTIRQ, tasklet_hi_action, NULL);
}

static int ksoftirqd(void * __bind_cpu)
{
	set_user_nice(current, 19);
	current->flags |= PF_NOFREEZE;

	set_current_state(TASK_INTERRUPTIBLE);

	while (!kthread_should_stop()) {
		if (!local_softirq_pending())
			schedule();

		__set_current_state(TASK_RUNNING);

		while (local_softirq_pending()) {
			/* Preempt disable stops cpu going offline.
			   If already offline, we'll be on wrong CPU:
			   don't process */
			preempt_disable();
			if (cpu_is_offline((long)__bind_cpu))
				goto wait_to_die;
			do_softirq();
			preempt_enable();
			cond_resched();
		}

		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);
	return 0;

wait_to_die:
	preempt_enable();
	/* Wait for kthread_stop */
	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * tasklet_kill_immediate is called to remove a tasklet which can already be
 * scheduled for execution on @cpu.
 *
 * Unlike tasklet_kill, this function removes the tasklet
 * _immediately_, even if the tasklet is in TASKLET_STATE_SCHED state.
 *
 * When this function is called, @cpu must be in the CPU_DEAD state.
 */
void tasklet_kill_immediate(struct tasklet_struct *t, unsigned int cpu)
{
	struct tasklet_struct **i;

	BUG_ON(cpu_online(cpu));
	BUG_ON(test_bit(TASKLET_STATE_RUN, &t->state));

	if (!test_bit(TASKLET_STATE_SCHED, &t->state))
		return;

	/* CPU is dead, so no lock needed. */
	for (i = &per_cpu(tasklet_vec, cpu).list; *i; i = &(*i)->next) {
		if (*i == t) {
			*i = t->next;
			return;
		}
	}
	BUG();
}

static void takeover_tasklets(unsigned int cpu)
{
	struct tasklet_struct **i;

	/* CPU is dead, so no lock needed. */
	local_irq_disable();

	/* Find end, append list for that CPU. */
	for (i = &__get_cpu_var(tasklet_vec).list; *i; i = &(*i)->next);
	*i = per_cpu(tasklet_vec, cpu).list;
	per_cpu(tasklet_vec, cpu).list = NULL;
	raise_softirq_irqoff(TASKLET_SOFTIRQ);

	for (i = &__get_cpu_var(tasklet_hi_vec).list; *i; i = &(*i)->next);
	*i = per_cpu(tasklet_hi_vec, cpu).list;
	per_cpu(tasklet_hi_vec, cpu).list = NULL;
	raise_softirq_irqoff(HI_SOFTIRQ);

	local_irq_enable();
}
#endif /* CONFIG_HOTPLUG_CPU */

static int __devinit cpu_callback(struct notifier_block *nfb,
				  unsigned long action,
				  void *hcpu)
{
	int hotcpu = (unsigned long)hcpu;
	struct task_struct *p;

	switch (action) {
	case CPU_UP_PREPARE:
		BUG_ON(per_cpu(tasklet_vec, hotcpu).list);
		BUG_ON(per_cpu(tasklet_hi_vec, hotcpu).list);
		p = kthread_create(ksoftirqd, hcpu, "ksoftirqd/%d", hotcpu);
		if (IS_ERR(p)) {
			printk("ksoftirqd for %i failed\n", hotcpu);
			return NOTIFY_BAD;
		}
		kthread_bind(p, hotcpu);
  		per_cpu(ksoftirqd, hotcpu) = p;
 		break;
	case CPU_ONLINE:
		wake_up_process(per_cpu(ksoftirqd, hotcpu));
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
		/* Unbind so it can run.  Fall thru. */
		kthread_bind(per_cpu(ksoftirqd, hotcpu), smp_processor_id());
	case CPU_DEAD:
		p = per_cpu(ksoftirqd, hotcpu);
		per_cpu(ksoftirqd, hotcpu) = NULL;
		kthread_stop(p);
		takeover_tasklets(hotcpu);
		break;
#endif /* CONFIG_HOTPLUG_CPU */
 	}
	return NOTIFY_OK;
}

static struct notifier_block __devinitdata cpu_nfb = {
	.notifier_call = cpu_callback
};

__init int spawn_ksoftirqd(void)
{
	void *cpu = (void *)(long)smp_processor_id();
	cpu_callback(&cpu_nfb, CPU_UP_PREPARE, cpu);
	cpu_callback(&cpu_nfb, CPU_ONLINE, cpu);
	register_cpu_notifier(&cpu_nfb);
	return 0;
}
