/*
 *  linux/arch/i386/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * This file contains the PC-specific time handling details:
 * reading the RTC at bootup, etc..
 * 1994-07-02    Alan Modra
 *	fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1995-03-26    Markus Kuhn
 *      fixed 500 ms bug at call to set_rtc_mmss, fixed DS12887
 *      precision CMOS clock update
 * 1996-05-03    Ingo Molnar
 *      fixed time warps in do_[slow|fast]_gettimeoffset()
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 * 1998-09-05    (Various)
 *	More robust do_fast_gettimeoffset() algorithm implemented
 *	(works with APM, Cyrix 6x86MX and Centaur C6),
 *	monotonic gettimeofday() with fast_get_timeoffset(),
 *	drift-proof precision TSC calibration on boot
 *	(C. Scott Ananian <cananian@alumni.princeton.edu>, Andrew D.
 *	Balsa <andrebalsa@altern.org>, Philip Gladstone <philip@raptor.com>;
 *	ported from 2.0.35 Jumbo-9 by Michael Krause <m.krause@tu-harburg.de>).
 * 1998-12-16    Andrea Arcangeli
 *	Fixed Jumbo-9 code in 2.1.131: do_gettimeofday was missing 1 jiffy
 *	because was not accounting lost_ticks.
 * 1998-12-24 Copyright (C) 1998  Andrea Arcangeli
 *	Fixed a xtime SMP race (we need the xtime_lock rw spinlock to
 *	serialize accesses to xtime/lost_ticks).
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/sysdev.h>
#include <linux/bcd.h>
#include <linux/efi.h>
#include <linux/mca.h>

#include <asm/io.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/msr.h>
#include <asm/delay.h>
#include <asm/mpspec.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/timer.h>

#include "mach_time.h"

#include <linux/timex.h>
#include <linux/config.h>

#include <asm/hpet.h>

#include <asm/arch_hooks.h>

#include "io_ports.h"

extern spinlock_t i8259A_lock;
int pit_latch_buggy;              /* extern */

#include "do_timer.h"

u64 jiffies_64 = INITIAL_JIFFIES;

EXPORT_SYMBOL(jiffies_64);

unsigned long cpu_khz;	/* Detected as we calibrate the TSC */

extern unsigned long wall_jiffies;

DEFINE_SPINLOCK(rtc_lock);

DEFINE_SPINLOCK(i8253_lock);
EXPORT_SYMBOL(i8253_lock);

struct timer_opts *cur_timer = &timer_none;		// 初始化为一个空的定时器对象

/*
 * 此版本的 gettimeofday 在具有 TSC 的快速 x86 机器上具有微秒分辨率和优于微秒精度。
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq;
	unsigned long usec, sec;
	unsigned long max_ntp_tick;

	do {
		unsigned long lost;

		seq = read_seqbegin(&xtime_lock);		// 获取顺序计数器

		usec = cur_timer->get_offset();		// 从上一个tick开始到现在的时间
		lost = jiffies - wall_jiffies;

		/*
		 * 如果 time_adjust 为负，则 NTP 正在减慢时钟，因此请确保不要进入下一个可能的间隔。
		 * 与其让时间倒流，不如失去一些准确性。
		 */
		if (unlikely(time_adjust < 0)) {
			max_ntp_tick = (USEC_PER_SEC / HZ) - tickadj;
			usec = min(usec, max_ntp_tick);

			if (lost)
				usec += lost * max_ntp_tick;
		}
		else if (unlikely(lost))
			usec += lost * (USEC_PER_SEC / HZ);

		sec = xtime.tv_sec;
		usec += (xtime.tv_nsec / 1000);
	} while (read_seqretry(&xtime_lock, seq));

	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);

int do_settimeofday(struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);
	/*
	 * This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * made, and then undo it!
	 */
	nsec -= cur_timer->get_offset() * NSEC_PER_USEC;
	nsec -= (jiffies - wall_jiffies) * TICK_NSEC;

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	write_sequnlock_irq(&xtime_lock);
	clock_was_set();
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

static int set_rtc_mmss(unsigned long nowtime)
{
	int retval;

	/* gets recalled with irq locally disabled */
	spin_lock(&rtc_lock);
	if (efi_enabled)
		retval = efi_set_rtc_mmss(nowtime);
	else
		retval = mach_set_rtc_mmss(nowtime);
	spin_unlock(&rtc_lock);

	return retval;
}

/* last time the cmos clock got updated */
static long last_rtc_update;

int timer_ack;

/* monotonic_clock(): returns # of nanoseconds passed since time_init()
 *		Note: This function is required to return accurate
 *		time even in the absence of multiple timer ticks.
 */
unsigned long long monotonic_clock(void)
{
	return cur_timer->monotonic_clock();
}
EXPORT_SYMBOL(monotonic_clock);

#if defined(CONFIG_SMP) && defined(CONFIG_FRAME_POINTER)
unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	if (in_lock_functions(pc))
		return *(unsigned long *)(regs->ebp + 4);

	return pc;
}
EXPORT_SYMBOL(profile_pc);
#endif

/*
 * timer_interrupt() 需要跟上实时时钟，并在每个时钟滴答时调用“do_timer()”例程
 */
static inline void do_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
#ifdef CONFIG_X86_IO_APIC
	if (timer_ack) {
		/*
		 * Subtle, when I/O APICs are used we have to ack timer IRQ
		 * manually to reset the IRR bit for do_slow_gettimeoffset().
		 * This will also deassert NMI lines for the watchdog if run
		 * on an 82489DX-based system.
		 */
		spin_lock(&i8259A_lock);
		outb(0x0c, PIC_MASTER_OCW3);
		/* Ack the IRQ; AEOI will end it automatically. */
		inb(PIC_MASTER_POLL);
		spin_unlock(&i8259A_lock);
	}
#endif

	do_timer_interrupt_hook(regs);

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if ((time_status & STA_UNSYNC) == 0 &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000)
			>= USEC_AFTER - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000)
			<= USEC_BEFORE + ((unsigned) TICK_SIZE) / 2) {
		/* horrible...FIXME */
		if (efi_enabled) {
	 		if (efi_set_rtc_mmss(xtime.tv_sec) == 0)
				last_rtc_update = xtime.tv_sec;
			else
				last_rtc_update = xtime.tv_sec - 600;
		} else if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}

	if (MCA_bus) {
		/* The PS/2 uses level-triggered interrupts.  You can't
		turn them off, nor would you want to (any attempt to
		enable edge-triggered interrupts usually gets intercepted by a
		special hardware circuit).  Hence we have to acknowledge
		the timer interrupt.  Through some incredibly stupid
		design idea, the reset for IRQ 0 is done by setting the
		high bit of the PPI port B (0x61).  Note that some PS/2s,
		notably the 55SX, work fine if this is removed.  */

		irq = inb_p( 0x61 );	/* read the current state */
		outb_p( irq|0x80, 0x61 );	/* reset the IRQ */
	}
}

/*
 * 这与上面的相同，除了我们_还_保存定时器中断时的当前时间戳计数器值，
 * 以便我们以后可以更准确地估计一天中的时间。
 */
irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	/*
	 * 在这里，我们在计时器irq处理程序中。我们只是在本地禁用了irqs，
	 * 但我们不知道timer_bh是否正在另一个CPU上运行。
	 * 我们需要避免与SMP竞争。注意: 我们不需要write_lock的irq版本，
	 * 因为正如刚才所说，我们在本地禁用了irq。 -arca
	 */
	write_seqlock(&xtime_lock);

	cur_timer->mark_offset();		// nop
 
	do_timer_interrupt(irq, NULL, regs);

	write_sequnlock(&xtime_lock);
	return IRQ_HANDLED;
}

/* not static: needed by APM */
unsigned long get_cmos_time(void)
{
	unsigned long retval;

	spin_lock(&rtc_lock);

	if (efi_enabled)
		retval = efi_get_time();
	else
		retval = mach_get_cmos_time();

	spin_unlock(&rtc_lock);

	return retval;
}

static long clock_cmos_diff, sleep_start;

static int timer_suspend(struct sys_device *dev, u32 state)
{
	/*
	 * Estimate time zone so that set_time can update the clock
	 */
	clock_cmos_diff = -get_cmos_time();
	clock_cmos_diff += get_seconds();
	sleep_start = get_cmos_time();
	return 0;
}

static int timer_resume(struct sys_device *dev)
{
	unsigned long flags;
	unsigned long sec;
	unsigned long sleep_length;

#ifdef CONFIG_HPET_TIMER
	if (is_hpet_enabled())
		hpet_reenable();
#endif
	sec = get_cmos_time() + clock_cmos_diff;
	sleep_length = (get_cmos_time() - sleep_start) * HZ;
	write_seqlock_irqsave(&xtime_lock, flags);
	xtime.tv_sec = sec;
	xtime.tv_nsec = 0;
	write_sequnlock_irqrestore(&xtime_lock, flags);
	jiffies += sleep_length;
	wall_jiffies += sleep_length;
	return 0;
}

static struct sysdev_class timer_sysclass = {
	.resume = timer_resume,
	.suspend = timer_suspend,
	set_kset_name("timer"),
};


/* XXX this driverfs stuff should probably go elsewhere later -john */
static struct sys_device device_timer = {
	.id	= 0,
	.cls	= &timer_sysclass,
};

static int time_init_device(void)
{
	int error = sysdev_class_register(&timer_sysclass);
	if (!error)
		error = sysdev_register(&device_timer);
	return error;
}

device_initcall(time_init_device);

#ifdef CONFIG_HPET_TIMER
extern void (*late_time_init)(void);
/* 下面的 time_init() 重复，添加了 hpet_enable 部分 */
void __init hpet_time_init(void)
{
	xtime.tv_sec = get_cmos_time();
	xtime.tv_nsec = (INITIAL_JIFFIES % HZ) * (NSEC_PER_SEC / HZ);
	set_normalized_timespec(&wall_to_monotonic,
		-xtime.tv_sec, -xtime.tv_nsec);

	if (hpet_enable() >= 0) {
		printk("Using HPET for base-timer\n");
	}

	cur_timer = select_timer();
	printk(KERN_INFO "Using %s for high-res timesource\n",cur_timer->name);

	time_init_hook();
}
#endif

/* 时间度量初始化
 * 执行的操作和hpet_time_init一样
 * */
void __init time_init(void)
{
#ifdef CONFIG_HPET_TIMER
	if (is_hpet_capable()) {
		/*
		 * HPET初始化需要做内存映射io。所以，让我们在 mem_init() 之后做一个后期初始化。
		 */
		late_time_init = hpet_time_init;
		return;
	}
#endif
	// xtime是struct timespec，秒+纳秒
	xtime.tv_sec = get_cmos_time();		// 获取实时时钟RTC，BIOS提供
	// (-300 000毫秒 % 1000) * (1000 000 000/1000)
	xtime.tv_nsec = (INITIAL_JIFFIES % HZ) * (NSEC_PER_SEC / HZ);
	set_normalized_timespec(&wall_to_monotonic, -xtime.tv_sec, -xtime.tv_nsec);

	cur_timer = select_timer();		// 选择定时器
	printk(KERN_INFO "Using %s for high-res timesource\n",cur_timer->name);

	time_init_hook();
}
