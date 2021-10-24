#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/timer.h>

#ifdef CONFIG_HPET_TIMER
/*
 * HPET memory read is slower than tsc reads, but is more dependable as it
 * always runs at constant frequency and reduces complexity due to
 * cpufreq. So, we prefer HPET timer to tsc based one. Also, we cannot use
 * timer_pit when HPET is active. So, we default to timer_tsc.
 */
#endif
/* 计时器列表，按偏好排序，NULL 终止 */
static struct init_timer_opts* __initdata timers[] = {
#ifdef CONFIG_X86_CYCLONE_TIMER
	&timer_cyclone_init,
#endif
#ifdef CONFIG_HPET_TIMER
	&timer_hpet_init,
#endif
#ifdef CONFIG_X86_PM_TIMER
	&timer_pmtmr_init,
#endif
	&timer_tsc_init,
	&timer_pit_init,		// 这个必定有
	NULL,
};

static char clock_override[10] __initdata;

static int __init clock_setup(char* str)
{
	if (str)
		strlcpy(clock_override, str, sizeof(clock_override));
	return 1;
}
__setup("clock=", clock_setup);		// 设置内核参数


/* The chosen timesource has been found to be bad.
 * Fall back to a known good timesource (the PIT)
 */
void clock_fallback(void)
{
	cur_timer = &timer_pit;
}

/* 遍历计时器列表，返回第一个初始化成功的计时器。
 */
struct timer_opts* __init select_timer(void)
{
	int i = 0;
	
	/* 找到最喜欢的工作计时器 */
	while (timers[i]) {
		if (timers[i]->init)
			if (timers[i]->init(clock_override) == 0)
				return timers[i]->opts;
		++i;
	}
		
	panic("select_timer: Cannot find a suitable timer\n");
	return NULL;
}
