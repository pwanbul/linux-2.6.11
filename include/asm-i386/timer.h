#ifndef _ASMi386_TIMER_H
#define _ASMi386_TIMER_H
#include <linux/init.h>

/* 和定时器相关的硬件五花八门，通过下面的结构来抽象，每个
 * 硬件定时器都会有一个timer_ops对象，在select_timer()
 * 中选择一个最好的定时器对象，由cur_timer指向，
 * cur_timer初始化为timer_none，是一个空定时器
 *
 *
 * struct timer_ops - 用于定义定时器源，定时器对象
 *
 * @name: 定时器的名称。
 * @init: 探测并初始化定时器。将clock=覆盖字符串作为参数。 成功时返回 0，失败时返回任何其他内容。
 * @mark_offset: 由定时器中断调用。
 * @get_offset:  由 gettimeofday() 调用。返回自上次计时器中断以来的微秒数。
 * @monotonic_clock: 返回自计时器初始化以来的纳秒数。
 * @delay: 延迟这么多时钟周期。
 */
struct timer_opts {
	char* name;
	void (*mark_offset)(void);
	unsigned long (*get_offset)(void);
	unsigned long long (*monotonic_clock)(void);
	void (*delay)(unsigned long);
};

/* 定时选项 */
struct init_timer_opts {
	int (*init)(char *override);
	struct timer_opts *opts;
};

#define TICK_SIZE (tick_nsec / 1000)

extern struct timer_opts* __init select_timer(void);
extern void clock_fallback(void);
void setup_pit_timer(void);

/* Modifiers for buggy PIT handling */

extern int pit_latch_buggy;

extern struct timer_opts *cur_timer;
extern int timer_ack;

/* list of externed timers */
extern struct timer_opts timer_none;
extern struct timer_opts timer_pit;
extern struct init_timer_opts timer_pit_init;
extern struct init_timer_opts timer_tsc_init;
#ifdef CONFIG_X86_CYCLONE_TIMER
extern struct init_timer_opts timer_cyclone_init;
#endif

extern unsigned long calibrate_tsc(void);
extern void init_cpu_khz(void);
#ifdef CONFIG_HPET_TIMER
extern struct init_timer_opts timer_hpet_init;
extern unsigned long calibrate_tsc_hpet(unsigned long *tsc_hpet_quotient_ptr);
#endif

#ifdef CONFIG_X86_PM_TIMER
extern struct init_timer_opts timer_pmtmr_init;
#endif
#endif
