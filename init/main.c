/*
 *  linux/init/main.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  GK 2/5/95  -  Changed to support mounting root fs via NFS
 *  Added initrd & change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Moan early if gcc is old, avoiding bogus kernels - Paul Gortmaker, May '96
 *  Simplified starting of init:  Michael A. Griffith <grif@acm.org> 
 */

#define __KERNEL_SYSCALLS__

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/initrd.h>
#include <linux/hdreg.h>
#include <linux/bootmem.h>
#include <linux/tty.h>
#include <linux/gfp.h>
#include <linux/percpu.h>
#include <linux/kmod.h>
#include <linux/kernel_stat.h>
#include <linux/security.h>
#include <linux/workqueue.h>
#include <linux/profile.h>
#include <linux/rcupdate.h>
#include <linux/moduleparam.h>
#include <linux/kallsyms.h>
#include <linux/writeback.h>
#include <linux/cpu.h>
#include <linux/efi.h>
#include <linux/unistd.h>
#include <linux/rmap.h>
#include <linux/mempolicy.h>
#include <linux/key.h>

#include <asm/io.h>
#include <asm/bugs.h>
#include <asm/setup.h>

/*
 * This is one of the first .c files built. Error out early
 * if we have compiler trouble..
 */
#if __GNUC__ == 2 && __GNUC_MINOR__ == 96
#ifdef CONFIG_FRAME_POINTER
#error This compiler cannot compile correctly with frame pointers enabled
#endif
#endif

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/smp.h>
#endif

/*
 * Versions of gcc older than that listed below may actually compile
 * and link okay, but the end product can have subtle run time bugs.
 * To avoid associated bogus bug reports, we flatly refuse to compile
 * with a gcc that is known to be too old from the very beginning.
 */
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 95)
#error Sorry, your GCC is too old. It builds incorrect kernels.
#endif

static int init(void *);

extern void init_IRQ(void);
extern void sock_init(void);
extern void fork_init(unsigned long);
extern void mca_init(void);
extern void sbus_init(void);
extern void sysctl_init(void);
extern void signals_init(void);
extern void buffer_init(void);
extern void pidhash_init(void);
extern void pidmap_init(void);
extern void prio_tree_init(void);
extern void radix_tree_init(void);
extern void free_initmem(void);
extern void populate_rootfs(void);
extern void driver_init(void);
extern void prepare_namespace(void);
#ifdef	CONFIG_ACPI
extern void acpi_early_init(void);
#else
static inline void acpi_early_init(void) { }
#endif

#ifdef CONFIG_TC
extern void tc_init(void);
#endif

enum system_states system_state;
EXPORT_SYMBOL(system_state);

/*
 * Boot command-line arguments
 */
#define MAX_INIT_ARGS 32
#define MAX_INIT_ENVS 32

extern void time_init(void);
/* Default late time init is NULL. archs can override this later. */
void (*late_time_init)(void);
extern void softirq_init(void);

/* 由特定于架构的代码保存的未触及的命令行（例如，用于 proc）。 */
char saved_command_line[COMMAND_LINE_SIZE];

static char *execute_command;

/* Setup configured maximum number of CPUs to activate */
static unsigned int max_cpus = NR_CPUS;

/*
 * Setup routine for controlling SMP activation
 *
 * Command-line option of "nosmp" or "maxcpus=0" will disable SMP
 * activation entirely (the MPS table probe still happens, though).
 *
 * Command-line option of "maxcpus=<NUM>", where <NUM> is an integer
 * greater than 0, limits the maximum number of CPUs activated in
 * SMP mode to <NUM>.
 */
static int __init nosmp(char *str)
{
	max_cpus = 0;
	return 1;
}

__setup("nosmp", nosmp);

static int __init maxcpus(char *str)
{
	get_option(&str, &max_cpus);
	return 1;
}

__setup("maxcpus=", maxcpus);

static char * argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
char * envp_init[MAX_INIT_ENVS+2] = { "HOME=/", "TERM=linux", NULL, };
static const char *panic_later, *panic_param;

extern struct obs_kernel_param __setup_start[], __setup_end[];

static int __init obsolete_checksetup(char *line)
{
	struct obs_kernel_param *p;

	p = __setup_start;
	do {		// 变量.init.setup区域
		int n = strlen(p->str);		// p->str的取值，例如“clock=”
		if (!strncmp(line, p->str, n)) {
			if (p->early) {
				/* Already done in parse_early_param?  (Needs
				 * exact match on param part) */
				if (line[n] == '\0' || line[n] == '=')
					return 1;
			} else if (!p->setup_func) {
				printk(KERN_WARNING "Parameter %s is obsolete,"
				       " ignored\n", p->str);
				return 1;
			} else if (p->setup_func(line + n))
				return 1;
		}
		p++;
	} while (p < __setup_end);
	return 0;
}

/*
 * This should be approx 2 Bo*oMips to start (note initial shift), and will
 * still work even if initially too large, it will just take slightly longer
 */
unsigned long loops_per_jiffy = (1<<12);

EXPORT_SYMBOL(loops_per_jiffy);

static int __init debug_kernel(char *str)
{
	if (*str)
		return 0;
	console_loglevel = 10;
	return 1;
}

static int __init quiet_kernel(char *str)
{
	if (*str)
		return 0;
	console_loglevel = 4;
	return 1;
}

__setup("debug", debug_kernel);
__setup("quiet", quiet_kernel);

/*
 * Unknown boot options get handed to init, unless they look like
 * failed parameters
 */
static int __init unknown_bootoption(char *param, char *val)
{
	/* Change NUL term back to "=", to make "param" the whole string. */
	if (val) {
		/* param=val or param="val"? */
		if (val == param+strlen(param)+1)
			val[-1] = '=';
		else if (val == param+strlen(param)+2) {
			val[-2] = '=';
			memmove(val-1, val, strlen(val)+1);
			val--;
		} else
			BUG();
	}

	/* Handle obsolete-style parameters */
	if (obsolete_checksetup(param))
		return 0;

	/*
	 * Preemptive maintenance for "why didn't my mispelled command
	 * line work?"
	 */
	if (strchr(param, '.') && (!val || strchr(param, '.') < val)) {
		printk(KERN_ERR "Unknown boot option `%s': ignoring\n", param);
		return 0;
	}

	if (panic_later)
		return 0;

	if (val) {
		/* Environment option */
		unsigned int i;
		for (i = 0; envp_init[i]; i++) {
			if (i == MAX_INIT_ENVS) {
				panic_later = "Too many boot env vars at `%s'";
				panic_param = param;
			}
			if (!strncmp(param, envp_init[i], val - param))
				break;
		}
		envp_init[i] = param;
	} else {
		/* Command line option */
		unsigned int i;
		for (i = 0; argv_init[i]; i++) {
			if (i == MAX_INIT_ARGS) {
				panic_later = "Too many boot init vars at `%s'";
				panic_param = param;
			}
		}
		argv_init[i] = param;
	}
	return 0;
}

static int __init init_setup(char *str)
{
	unsigned int i;

	execute_command = str;
	/*
	 * In case LILO is going to boot us with default command line,
	 * it prepends "auto" before the whole cmdline which makes
	 * the shell think it should execute a script with such name.
	 * So we ignore all arguments entered _before_ init=... [MJ]
	 */
	for (i = 1; i < MAX_INIT_ARGS; i++)
		argv_init[i] = NULL;
	return 1;
}
__setup("init=", init_setup);

extern void setup_arch(char **);

#ifndef CONFIG_SMP      // !SMP

#ifdef CONFIG_X86_LOCAL_APIC
static void __init smp_init(void)
{
	APIC_init_uniprocessor();
}
#else
#define smp_init()	do { } while (0)
#endif

// 下面2个函数在!SMP中为空
static inline void setup_per_cpu_areas(void) { }
static inline void smp_prepare_cpus(unsigned int maxcpus) { }

#else   // CONFIG_SMP

#ifdef __GENERIC_PER_CPU
unsigned long __per_cpu_offset[NR_CPUS];        // 保存元素数据到其"数组元素"之间偏移量

EXPORT_SYMBOL(__per_cpu_offset);

static void __init setup_per_cpu_areas(void)        // per CPU变量初始化
{
	unsigned long size, i;
	char *ptr;
	/* Created by linker magic */
	extern char __per_cpu_start[], __per_cpu_end[];     // 链接脚本中定义的2个特殊符号，中间存放的per CPU变量

	/* Copy section for each CPU (we discard the original) */
	size = ALIGN(__per_cpu_end - __per_cpu_start, SMP_CACHE_BYTES);     // 一级缓存的大小128字节，向上对齐
#ifdef CONFIG_MODULES       // 是否支持模块
	if (size < PERCPU_ENOUGH_ROOM)
		size = PERCPU_ENOUGH_ROOM;
#endif
    // 定义一次，加载多次
	ptr = alloc_bootmem(size * NR_CPUS);        // 按cpu的数量，分配空间

	for (i = 0; i < NR_CPUS; i++, ptr += size) {
		__per_cpu_offset[i] = ptr - __per_cpu_start;        // 记录下每个"数组元素"起始地址和section之间偏移量
		memcpy(ptr, __per_cpu_start, __per_cpu_end - __per_cpu_start);      // 把每CPU变量复制各个cpu的"数组元素"内
	}
}
#endif /* !__GENERIC_PER_CPU */

/* 由引导处理器调用以激活其余部分。 */
static void __init smp_init(void)
{
	unsigned int i;

	/* FIXME: This should be done in userspace --RR */
	for_each_present_cpu(i) {
		if (num_online_cpus() >= max_cpus)
			break;
		if (!cpu_online(i))
			cpu_up(i);
	}

	/* Any cleanup work */
	printk("Brought up %ld CPUs\n", (long)num_online_cpus());
	smp_cpus_done(max_cpus);
#if 0
	/* Get other processors into their bootup holding patterns. */

	smp_threads_ready=1;
	smp_commence();
#endif
}

#endif

/*
 * 我们需要在非 __init 函数中完成，否则根线程和 init 线程之间的
 * 竞争条件可能会导致 start_kernel 在根线程进入 cpu_idle 之前被 free_initmem 获取。
 *
 * gcc-3.4 不小心内联了这个函数，所以使用 noinline。
 */

static void noinline rest_init(void)
	__releases(kernel_lock)
{
	kernel_thread(init, NULL, CLONE_FS | CLONE_SIGHAND);
	numa_default_policy();
	unlock_kernel();
	preempt_enable_no_resched();
	cpu_idle();
} 

/* Check for early params. */
static int __init do_early_param(char *param, char *val)
{
	struct obs_kernel_param *p;

	for (p = __setup_start; p < __setup_end; p++) {
        // early被设置
		if (p->early && strcmp(param, p->str) == 0) {
			if (p->setup_func(val) != 0)
				printk(KERN_WARNING "Malformed early option '%s'\n", param);
		}
	}
	/* We accept everything at this stage. */
	return 0;
}

/* Arch 代码在早期调用它，或者如果没有，就在其他解析之前调用它。 */
void __init parse_early_param(void)
{
	static __initdata int done = 0;
	static __initdata char tmp_cmdline[COMMAND_LINE_SIZE];

	if (done)
		return;

	/* 全部落入do_early_param。 */
	strlcpy(tmp_cmdline, saved_command_line, COMMAND_LINE_SIZE);
    // 虚晃一枪，实际调用的是do_early_param
	parse_args("early options", tmp_cmdline, NULL, 0, do_early_param);
	done = 1;
}

/*
 *	激活第一个处理器。
 */

asmlinkage void __init start_kernel(void)
{
	char * command_line;
	extern struct kernel_param __start___param[], __stop___param[]; // 链接脚本中定义特殊符号
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
	lock_kernel();
	/* 建立持久内核映射（PKMap，Persistent Kernel Map）机制所需的散列表，
	 * 该机制利用散列表从给定的虚拟地址确定持久内核映射的物理地址。
	 * */
	page_address_init();
	printk(linux_banner);
	setup_arch(&command_line);      // 其中一项任务是负责初始化自举分配器
	setup_per_cpu_areas();      // 初始化per cpu变量

	/*
	 * Mark the boot cpu "online" so that it can call console drivers in
	 * printk() and can access its per-cpu storage.
	 */
	smp_prepare_boot_cpu();

	/*
	 * Set up the scheduler prior starting any interrupts (such as the
	 * timer interrupt). Full topology setup happens at smp_init()
	 * time - but meanwhile we still have a functioning scheduler.
	 */
	sched_init();
	/*
	 * Disable preemption - early bootup scheduling is extremely
	 * fragile until we cpu_idle() for the first time.
	 */
	preempt_disable();
	build_all_zonelists();      // 建立结点和内存域的数据结构
	page_alloc_init();

	printk("Kernel command line: %s\n", saved_command_line);    // cat /proc/cmdline
	parse_early_param();
    // /sys中的参数
	parse_args("Booting kernel", command_line, __start___param, __stop___param - __start___param, &unknown_bootoption);

    sort_main_extable();
	trap_init();		// IDT初始化
	rcu_init();
	init_IRQ();
	pidhash_init();		// pid hash表初始化
	init_timers();
	softirq_init();		// tasklet初始化
	time_init();		// 时间度量初始化

	/*
	 * HACK ALERT! This is early. We're enabling the console before
	 * we've done PCI setups etc, and console_init() must be aware of
	 * this. But we do want output early, in case something goes wrong.
	 */
	console_init();
	if (panic_later)
		panic(panic_later, panic_param);
	profile_init();
	local_irq_enable();
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start && !initrd_below_start_ok &&
			initrd_start < min_low_pfn << PAGE_SHIFT) {
		printk(KERN_CRIT "initrd overwritten (0x%08lx < 0x%08lx) - "
		    "disabling it.\n",initrd_start,min_low_pfn << PAGE_SHIFT);
		initrd_start = 0;
	}
#endif
	vfs_caches_init_early();		// 初始vfs cache
	mem_init();     // mem_init是另一个特定于体系结构的函数，用于停用bootmem分配器并迁移到实际的内存管理函数；
	kmem_cache_init();      // 通用slab初始化
	numa_policy_init();
	if (late_time_init)		// hpet使用内存映射IO，需要在mem_init后期执行
		late_time_init();
	calibrate_delay();
	pidmap_init();      // 初始化pid bitmap
	pgtable_cache_init();
	prio_tree_init();
	anon_vma_init();
#ifdef CONFIG_X86
	if (efi_enabled)
		efi_enter_virtual_mode();
#endif
	fork_init(num_physpages);   // 为task_struct的slab缓存初始化，并设置进程数量限制
	proc_caches_init();     // 6种slab缓存初始化
	buffer_init();
	unnamed_dev_init();
	security_init();		// LSM初始化
	vfs_caches_init(num_physpages);			// vfs缓存初始化
	radix_tree_init();
	signals_init();
	/* rootfs populating might need page-writeback */
	page_writeback_init();
#ifdef CONFIG_PROC_FS
	proc_root_init();
#endif
	check_bugs();

	acpi_early_init(); /* before LAPIC and SMP init */

	/* 做剩下的非 __init'ed，我们现在还活着 */
	rest_init();		// 创建第一个内核线程
}

static int __initdata initcall_debug;

static int __init initcall_debug_setup(char *str)
{
	initcall_debug = 1;
	return 1;
}
__setup("initcall_debug", initcall_debug_setup);

struct task_struct *child_reaper = &init_task;

// initcall特殊符号
extern initcall_t __initcall_start[], __initcall_end[];

static void __init do_initcalls(void)
{
	initcall_t *call;
	int count = preempt_count();

	for (call = __initcall_start; call < __initcall_end; call++) {
		char *msg;

		if (initcall_debug) {
			printk(KERN_DEBUG "Calling initcall 0x%p", *call);
			print_fn_descriptor_symbol(": %s()", (unsigned long) *call);
			printk("\n");
		}

		(*call)();

		msg = NULL;
		if (preempt_count() != count) {
			msg = "preemption imbalance";
			preempt_count() = count;
		}
		if (irqs_disabled()) {
			msg = "disabled interrupts";
			local_irq_enable();
		}
		if (msg) {
			printk("error in initcall at 0x%p: "
				"returned with %s\n", *call, msg);
		}
	}

	/* Make sure there is no pending stuff from the initcall sequence */
	flush_scheduled_work();
}

/*
 * 好的，现在机器已经初始化了。尚未触及任何设备，
 * 但 CPU 子系统已启动并运行，内存和进程管理工作正常。
 *
 * 现在我们终于可以开始做一些真正的工作了..
 */
static void __init do_basic_setup(void)
{
	/* drivers will send hotplug events */
	init_workqueues();
	usermodehelper_init();
	key_init();
	driver_init();

#ifdef CONFIG_SYSCTL
	sysctl_init();			// 初始化内核参数sysctl
#endif

	/* Networking initialization needs a process context */ 
	sock_init();

	do_initcalls();         // 按顺序调用
}

static void do_pre_smp_initcalls(void)
{
	extern int spawn_ksoftirqd(void);
#ifdef CONFIG_SMP
	extern int migration_init(void);

	migration_init();
#endif
	spawn_ksoftirqd();
}

static void run_init_process(char *init_filename)
{
	argv_init[0] = init_filename;
	execve(init_filename, argv_init, envp_init);
}

static inline void fixup_cpu_present_map(void)
{
#ifdef CONFIG_SMP
	int i;

	/*
	 * If arch is not hotplug ready and did not populate
	 * cpu_present_map, just make cpu_present_map same as cpu_possible_map
	 * for other cpu bringup code to function as normal. e.g smp_init() etc.
	 */
	if (cpus_empty(cpu_present_map)) {
		for_each_cpu(i) {
			cpu_set(i, cpu_present_map);
		}
	}
#endif
}

static int init(void * unused)
{
	lock_kernel();
	/*
	 * 告诉全世界，我们将成为无辜孤儿的死神。
	 *
	 * 我们不希望人们对任务数组中的位置做出错误的假设.
	 */
	child_reaper = current;

	/* Sets up cpus_possible() */
	smp_prepare_cpus(max_cpus);

	do_pre_smp_initcalls();

	fixup_cpu_present_map();
	smp_init();
	sched_init_smp();

	/*
	 * 在 initcalls 之前执行此操作，因为某些驱动程序想要访问固件文件。
	 */
	populate_rootfs();

	do_basic_setup();

	/*
	 * check if there is an early userspace init.  If yes, let it do all
	 * the work
	 */
	if (sys_access((const char __user *) "/init", 0) == 0)
		execute_command = "/init";
	else
		prepare_namespace();

	/*
	 * Ok, we have completed the initial bootup, and
	 * we're essentially up and running. Get rid of the
	 * initmem segments and start the user-mode stuff..
	 */
	free_initmem();
	unlock_kernel();
	system_state = SYSTEM_RUNNING;
	numa_default_policy();

	if (sys_open((const char __user *) "/dev/console", O_RDWR, 0) < 0)
		printk("Warning: unable to open an initial console.\n");

	(void) sys_dup(0);
	(void) sys_dup(0);
	
	/*
	 * We try each of these until one succeeds.
	 *
	 * The Bourne shell can be used instead of init if we are 
	 * trying to recover a really broken machine.
	 */

	if (execute_command)
		run_init_process(execute_command);

	run_init_process("/sbin/init");
	run_init_process("/etc/init");
	run_init_process("/bin/init");
	run_init_process("/bin/sh");

	panic("No init found.  Try passing init= option to kernel.");
}
