#ifndef _I386_CURRENT_H
#define _I386_CURRENT_H

#include <linux/thread_info.h>

struct task_struct;

static inline struct task_struct * get_current(void)
{
	return current_thread_info()->task;
}
 
#define current get_current()       // 当前进程的的进程描述符指针

#endif /* !(_I386_CURRENT_H) */
