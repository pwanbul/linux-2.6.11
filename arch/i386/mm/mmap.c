/*
 *  linux/arch/i386/mm/mmap.c
 *
 *  flexible mmap layout support
 *
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * Started by Ingo Molnar <mingo@elte.hu>
 */

#include <linux/personality.h>
#include <linux/mm.h>

/*
 * Top of mmap area (just below the process stack).
 *
 * Leave an at least ~128 MB hole.
 */
#define MIN_GAP (128*1024*1024)		// 空洞至少128MB
#define MAX_GAP (TASK_SIZE/6*5)		// 空洞最多2560MB

static inline unsigned long mmap_base(struct mm_struct *mm)
{
	// 注意这里计算的不是栈大小
	unsigned long gap = current->signal->rlim[RLIMIT_STACK].rlim_cur;

	if (gap < MIN_GAP)
		gap = MIN_GAP;
	else if (gap > MAX_GAP)
		gap = MAX_GAP;

	return TASK_SIZE - (gap & PAGE_MASK);
}

/* 考虑ASLR机制, 2.6.11没有ASLR
 * static inline unsigned long mmap_base(struct mm_struct *mm)
{
	unsigned long gap = current->signal->rlim[RLIMIT_STACK].rlim_cur;
	unsigned long random_factor = 0;
	if (current->flags & PF_RANDOMIZE)
		random_factor = get_random_int() % (1024*1024);		// 最大1MB
	if (gap < MIN_GAP)
		gap = MIN_GAP;
	else if (gap > MAX_GAP)
		gap = MAX_GAP;
	return PAGE_ALIGN(TASK_SIZE -gap -random_factor);
}
 */


/*
 * 此函数在创建新进程VM映像期间很早就被调用，
 * 用于设置要使用的VM布局函数：
 */
void arch_pick_mmap_layout(struct mm_struct *mm)
{
	/*
	 * Fall back to the standard layout if the personality
	 * bit is set, or if the expected stack growth is unlimited:
	 */
	if (sysctl_legacy_va_layout ||		// 内核参数/proc/sys/vm/legacy_va_layout，经典布局
			(current->personality & ADDR_COMPAT_LAYOUT) ||
			current->signal->rlim[RLIMIT_STACK].rlim_cur == RLIM_INFINITY) {// resource limit机制，不限制栈的大小
		// mmap vam 向上增长
		mm->mmap_base = TASK_UNMAPPED_BASE;
		mm->get_unmapped_area = arch_get_unmapped_area;		// 获取mmap空间
		mm->unmap_area = arch_unmap_area;				// 释放mmap空间
	} else {
		// mmap vam 向下增长
		mm->mmap_base = mmap_base(mm);		// 决定起始地址
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;
		mm->unmap_area = arch_unmap_area_topdown;
	}
}
