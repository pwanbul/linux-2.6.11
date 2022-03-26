#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/fs.h>
#include <linux/mqueue.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/desc.h>

static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);

EXPORT_SYMBOL(init_mm);

/*
 * 初始线程结构。
 *
 * 由于处理进程堆栈的方式，我们需要确保这是 THREAD_SIZE 对齐的。
 * 这是通过有一个特殊的“init_task”链接器映射条目来完成的。
 */
union thread_union init_thread_union 
	__attribute__((__section__(".data.init_task"))) =
		{ INIT_THREAD_INFO(init_task) };

/*
 * Initial task structure.
 *
 * All other task structs will be allocated on slabs in fork.c
 */
// 1号进程，手工初始化，全局变量
// 注意这总初始化的方式
struct task_struct init_task = INIT_TASK(init_task);

EXPORT_SYMBOL(init_task);

/*
 * 每个CPU的TSS段。
 * Linux上的线程是完全“软的”，不再有每个任务的TSS。
 */ 
DEFINE_PER_CPU(struct tss_struct, init_tss) ____cacheline_maxaligned_in_smp = INIT_TSS;

