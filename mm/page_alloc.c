/*
 *  linux/mm/page_alloc.c
 *
 *  Manages the free list, the system allocates free pages here.
 *  Note that kmalloc() lives in slab.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *  Reshaped it to be a zoned allocator, Ingo Molnar, Red Hat, 1999
 *  Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 *  Zone balancing, Kanoj Sarcar, SGI, Jan 2000
 *  Per cpu hot/cold page lists, bulk allocation, Martin J. Bligh, Sept 2002
 *          (lots of bits borrowed from Ingo Molnar & Andrew Morton)
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/pagevec.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/topology.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/nodemask.h>
#include <linux/vmalloc.h>

#include <asm/tlbflush.h>
#include "internal.h"

/* MCD - HACK: Find somewhere to initialize this EARLY, or make this initializer cleaner */
nodemask_t node_online_map = { { [0] = 1UL } };
nodemask_t node_possible_map = NODE_MASK_ALL;
struct pglist_data *pgdat_list;			// 全局唯一
unsigned long totalram_pages;
unsigned long totalhigh_pages;
long nr_swap_pages;
/*
 * results with 256, 32 in the lowmem_reserve sysctl:
 *	1G machine -> (16M dma, 800M-16M normal, 1G-800M high)
 *	1G machine -> (16M dma, 784M normal, 224M high)
 *	NORMAL allocation will leave 784M/256 of ram reserved in the ZONE_DMA
 *	HIGHMEM allocation will leave 224M/32 of ram reserved in ZONE_NORMAL
 *	HIGHMEM allocation will (224M+784M)/256 of ram reserved in ZONE_DMA
 */
int sysctl_lowmem_reserve_ratio[MAX_NR_ZONES-1] = { 256, 32 };

EXPORT_SYMBOL(totalram_pages);
EXPORT_SYMBOL(nr_swap_pages);

/*
 * Used by page_zone() to look up the address of the struct zone whose
 * id is encoded in the upper bits of page->flags
 */
struct zone *zone_table[1 << (ZONES_SHIFT + NODES_SHIFT)];      // zone_table是为了支持从页框描述符找到其所在的内存区
EXPORT_SYMBOL(zone_table);

static char *zone_names[MAX_NR_ZONES] = { "DMA", "Normal", "HighMem" };
int min_free_kbytes = 1024;			// 默认的"保留的页框池"大小，1024KB

unsigned long __initdata nr_kernel_pages;
unsigned long __initdata nr_all_pages;

/*
 * Temporary debugging check for pages not lying within a given zone.
 */
static int bad_range(struct zone *zone, struct page *page)
{
	if (page_to_pfn(page) >= zone->zone_start_pfn + zone->spanned_pages)
		return 1;
	if (page_to_pfn(page) < zone->zone_start_pfn)
		return 1;
#ifdef CONFIG_HOLES_IN_ZONE
	if (!pfn_valid(page_to_pfn(page)))
		return 1;
#endif
	if (zone != page_zone(page))
		return 1;
	return 0;
}

static void bad_page(const char *function, struct page *page)
{
	printk(KERN_EMERG "Bad page state at %s (in process '%s', page %p)\n",
		function, current->comm, page);
	printk(KERN_EMERG "flags:0x%0*lx mapping:%p mapcount:%d count:%d\n",
		(int)(2*sizeof(page_flags_t)), (unsigned long)page->flags,
		page->mapping, page_mapcount(page), page_count(page));
	printk(KERN_EMERG "Backtrace:\n");
	dump_stack();
	printk(KERN_EMERG "Trying to fix it up, but a reboot is needed\n");
	page->flags &= ~(1 << PG_private	|
			1 << PG_locked	|
			1 << PG_lru	|
			1 << PG_active	|
			1 << PG_dirty	|
			1 << PG_swapcache |
			1 << PG_writeback);
	set_page_count(page, 0);
	reset_page_mapcount(page);
	page->mapping = NULL;
	tainted |= TAINT_BAD_PAGE;
}

#ifndef CONFIG_HUGETLB_PAGE
#define prep_compound_page(page, order) do { } while (0)
#define destroy_compound_page(page, order) do { } while (0)
#else
/*
 * Higher-order pages are called "compound pages".  They are structured thusly:
 *
 * The first PAGE_SIZE page is called the "head page".
 *
 * The remaining PAGE_SIZE pages are called "tail pages".
 *
 * All pages have PG_compound set.  All pages have their ->private pointing at
 * the head page (even the head page has this).
 *
 * The first tail page's ->mapping, if non-zero, holds the address of the
 * compound page's put_page() function.
 *
 * The order of the allocation is stored in the first tail page's ->index
 * This is only for debug at present.  This usage means that zero-order pages
 * may not be compound.
 */
static void prep_compound_page(struct page *page, unsigned long order)
{
	int i;
	int nr_pages = 1 << order;

	page[1].mapping = NULL;
	page[1].index = order;
	for (i = 0; i < nr_pages; i++) {
		struct page *p = page + i;

		SetPageCompound(p);
		p->private = (unsigned long)page;
	}
}

static void destroy_compound_page(struct page *page, unsigned long order)
{
	int i;
	int nr_pages = 1 << order;

	if (!PageCompound(page))
		return;

	if (page[1].index != order)
		bad_page(__FUNCTION__, page);

	for (i = 0; i < nr_pages; i++) {
		struct page *p = page + i;

		if (!PageCompound(p))
			bad_page(__FUNCTION__, page);
		if (p->private != (unsigned long)page)
			bad_page(__FUNCTION__, page);
		ClearPageCompound(p);
	}
}
#endif		/* CONFIG_HUGETLB_PAGE */

/*
 * 伙伴系统中处理页面order的功能。
 * 当我们使用这些时，zone->lock 已经被获取了。
 * 所以，我们这里不需要原子 page->flags 操作。
 */
static inline unsigned long page_order(struct page *page) {
	return page->private;
}
// 设置页框的分配阶
static inline void set_page_order(struct page *page, int order) {
	page->private = order;
	__SetPagePrivate(page);
}
// 清除页框的分配阶
static inline void rmv_page_order(struct page *page)
{
	__ClearPagePrivate(page);
	page->private = 0;
}

/*
 * 此函数检查页面是否已经释放 && 释放buddy
 * 我们可以合并一个页面和它的伙伴，如果
 * (a) 他的伙伴已经释放 &&
 * (b) 伙伴在伙伴系统中 &&
 * (c) 一个页面和它的伙伴有相同的order.
 * 为了记录页面的order，我们使用 page->private 和 PG_private。
 *
 */
static inline int page_is_buddy(struct page *page, int order)
{
       if (PagePrivate(page)           &&
           (page_order(page) == order) &&
           !PageReserved(page)         &&
            page_count(page) == 0)
               return 1;
       return 0;
}

/*
 * Freeing function for a buddy system allocator.
 *
 * The concept of a buddy system is to maintain direct-mapped table
 * (containing bit values) for memory blocks of various "orders".
 * The bottom level table contains the map for the smallest allocatable
 * units of memory (here, pages), and each level above it describes
 * pairs of units from the levels below, hence, "buddies".
 * At a high level, all that happens here is marking the table entry
 * at the bottom level available, and propagating the changes upward
 * as necessary, plus some accounting needed to play nicely with other
 * parts of the VM system.
 * At each level, we keep a list of pages, which are heads of continuous
 * free pages of length of (1 << order) and marked with PG_Private.Page's
 * order is recorded in page->private field.
 * So when we are allocating or freeing one, we can derive the state of the
 * other.  That is, if we allocate a small block, and both were   
 * free, the remainder of the region must be split into blocks.   
 * If a block is freed, and its buddy is also free, then this
 * triggers coalescing into a block of larger size.            
 *
 * -- wli
 *
 * page: 要释放的页框
 * base: zone的页框数组
 * zone: 当前zone
 * order: 分配阶
 */

static inline void __free_pages_bulk (struct page *page, struct page *base, struct zone *zone, unsigned int order)
{
	unsigned long page_idx;
	struct page *coalesced;
	int order_size = 1 << order;		// 释放页框的数量

	if (unlikely(order))
		destroy_compound_page(page, order);

	page_idx = page - base;		// 页框描述符idx

	BUG_ON(page_idx & (order_size - 1));
	BUG_ON(bad_range(zone, page));

	zone->free_pages += order_size;		// 更新空闲页框数量
	// 合并buddy，向前合并或者向后合并
	while (order < MAX_ORDER-1) {		// MAX_ORDER为11，从当前order开始处理
		struct free_area *area;
		struct page *buddy;
		int buddy_idx;

		buddy_idx = (page_idx ^ (1 << order));		// 获得page_idx的伙伴
		buddy = base + buddy_idx;
		if (bad_range(zone, buddy))
			break;
		if (!page_is_buddy(buddy, order))		// buddy的order必须和page相同，buddy没有引用，释放在伙伴系统中
			break;
		/* 将buddy提升一级。 */
		list_del(&buddy->lru);		// 从页框高速缓存中删除
		area = zone->free_area + order;			// 定位空闲链表
		area->nr_free--;
		rmv_page_order(buddy);		// 清除buddy的order
		page_idx &= buddy_idx;		// 由于向前合并和向后合并，需要修正page_idx
		order++;
	}
	coalesced = base + page_idx;		// 合并后起始页框描述符
	set_page_order(coalesced, order);		// 更新order
	list_add(&coalesced->lru, &zone->free_area[order].free_list);
	zone->free_area[order].nr_free++;
}

static inline void free_pages_check(const char *function, struct page *page)
{
	if (	page_mapped(page) ||
		page->mapping != NULL ||
		page_count(page) != 0 ||
		(page->flags & (
			1 << PG_lru	|
			1 << PG_private |
			1 << PG_locked	|
			1 << PG_active	|
			1 << PG_reclaim	|
			1 << PG_slab	|
			1 << PG_swapcache |
			1 << PG_writeback )))
		bad_page(function, page);
	if (PageDirty(page))
		ClearPageDirty(page);
}

/*
 * Frees a list of pages. 
 * 假设列表中的所有页面都在同一区域中，并且顺序相同。
 * count 是要释放的页数，或者列表中的所有页数为 0。
 *
 * 如果该区域之前处于“所有页面固定”状态，则查看此释放是否清除了该状态。
 *
 * 并清除区域的 pages_scanned 计数器，以阻止“所有页面都被固定”检测逻辑。
 */
static int free_pages_bulk(struct zone *zone, int count, struct list_head *list, unsigned int order)
{
	unsigned long flags;
	struct page *base, *page = NULL;
	int ret = 0;		// 统计释放的页框数

	base = zone->zone_mem_map;		// 当前zone的页框描述符数组
	spin_lock_irqsave(&zone->lock, flags);
	zone->all_unreclaimable = 0;
	zone->pages_scanned = 0;		// 扫描次数置0
	while (!list_empty(list) && count--) {		// 循环释放
		page = list_entry(list->prev, struct page, lru);
		/* 必须在 __free_pages_bulk 列表操作时将其删除 */
		list_del(&page->lru);		// 从链表移除
		__free_pages_bulk(page, base, zone, order);		// 移除的页框使用buddy system处理
		ret++;
	}
	spin_unlock_irqrestore(&zone->lock, flags);
	return ret;
}

void __free_pages_ok(struct page *page, unsigned int order)
{
	LIST_HEAD(list);
	int i;

	arch_free_page(page, order);		// 空定义

	mod_page_state(pgfree, 1 << order);

#ifndef CONFIG_MMU
	if (order > 0)
		for (i = 1 ; i < (1 << order) ; ++i)
			__put_page(page + i);
#endif

	for (i = 0 ; i < (1 << order) ; ++i)
		free_pages_check(__FUNCTION__, page + i);
	list_add(&page->lru, &list);
	kernel_map_pages(page, 1<<order, 0);		// debug
	free_pages_bulk(page_zone(page), 1, &list, order);
}


/*
 * The order of subdivision here is critical for the IO subsystem.
 * Please do not alter this order without good reasons and regression
 * testing. Specifically, as large blocks of memory are subdivided,
 * the order in which smaller blocks are delivered depends on the order
 * they're subdivided in this function. This is the primary factor
 * influencing the order in which pages are delivered to the IO
 * subsystem according to empirical testing, and this is also justified
 * by considering the behavior of a buddy system containing a single
 * large block of memory acted on by a series of small allocations.
 * This behavior is a critical factor in sglist merging's success.
 *
 * -- wli
 */
static inline struct page *
expand(struct zone *zone, struct page *page, int low, int high, struct free_area *area)
{
	unsigned long size = 1 << high;

	while (high > low) {
		area--;
		high--;
		size >>= 1;
		BUG_ON(bad_range(zone, &page[size]));
		list_add(&page[size].lru, &area->free_list);
		area->nr_free++;
		set_page_order(&page[size], high);
	}
	return page;
}

void set_page_refs(struct page *page, int order)
{
#ifdef CONFIG_MMU
	set_page_count(page, 1);
#else
	int i;

	/*
	 * We need to reference all the pages for this order, otherwise if
	 * anyone accesses one of the pages with (get/put) it will be freed.
	 * - eg: access_process_vm()
	 */
	for (i = 0; i < (1 << order); i++)
		set_page_count(page + i, 1);
#endif /* CONFIG_MMU */
}

/*
 * This page is about to be returned from the page allocator
 */
static void prep_new_page(struct page *page, int order)
{
	if (page->mapping || page_mapped(page) ||
	    (page->flags & (
			1 << PG_private	|
			1 << PG_locked	|
			1 << PG_lru	|
			1 << PG_active	|
			1 << PG_dirty	|
			1 << PG_reclaim	|
			1 << PG_swapcache |
			1 << PG_writeback )))
		bad_page(__FUNCTION__, page);

	page->flags &= ~(1 << PG_uptodate | 1 << PG_error |
			1 << PG_referenced | 1 << PG_arch_1 |
			1 << PG_checked | 1 << PG_mappedtodisk);
	page->private = 0;
	set_page_refs(page, order);
	kernel_map_pages(page, 1 << order, 1);
}

/* 
 * 努力从伙伴分配器中移除一个元素。打电话给我，区域->锁定已经举行。
 */
static struct page *__rmqueue(struct zone *zone, unsigned int order)
{
	struct free_area * area;
	unsigned int current_order;
	struct page *page;

	for (current_order = order; current_order < MAX_ORDER; ++current_order) {
		area = zone->free_area + current_order;
		if (list_empty(&area->free_list))
			continue;

		page = list_entry(area->free_list.next, struct page, lru);
		list_del(&page->lru);
		rmv_page_order(page);
		area->nr_free--;
		zone->free_pages -= 1UL << order;
		return expand(zone, page, order, current_order, area);
	}

	return NULL;
}

/* 
 * Obtain a specified number of elements from the buddy allocator, all under
 * a single hold of the lock, for efficiency.  Add them to the supplied list.
 * Returns the number of new pages which were placed at *list.
 */
static int rmqueue_bulk(struct zone *zone, unsigned int order, 
			unsigned long count, struct list_head *list)
{
	unsigned long flags;
	int i;
	int allocated = 0;
	struct page *page;
	
	spin_lock_irqsave(&zone->lock, flags);
	for (i = 0; i < count; ++i) {
		page = __rmqueue(zone, order);
		if (page == NULL)
			break;
		allocated++;
		list_add_tail(&page->lru, list);
	}
	spin_unlock_irqrestore(&zone->lock, flags);
	return allocated;
}

#if defined(CONFIG_PM) || defined(CONFIG_HOTPLUG_CPU)
static void __drain_pages(unsigned int cpu)
{
	struct zone *zone;
	int i;

	for_each_zone(zone) {
		struct per_cpu_pageset *pset;

		pset = &zone->pageset[cpu];
		for (i = 0; i < ARRAY_SIZE(pset->pcp); i++) {
			struct per_cpu_pages *pcp;

			pcp = &pset->pcp[i];
			pcp->count -= free_pages_bulk(zone, pcp->count,
						&pcp->list, 0);
		}
	}
}
#endif /* CONFIG_PM || CONFIG_HOTPLUG_CPU */

#ifdef CONFIG_PM

/* 标记空闲页 */
void mark_free_pages(struct zone *zone)
{
	unsigned long zone_pfn, flags;
	int order;
	struct list_head *curr;

	if (!zone->spanned_pages)
		return;

	spin_lock_irqsave(&zone->lock, flags);
	for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn)
		ClearPageNosaveFree(pfn_to_page(zone_pfn + zone->zone_start_pfn));

	// 从order11开始处理
	for (order = MAX_ORDER - 1; order >= 0; --order)
		list_for_each(curr, &zone->free_area[order].free_list) {
			unsigned long start_pfn, i;

			start_pfn = page_to_pfn(list_entry(curr, struct page, lru));

			for (i=0; i < (1<<order); i++)
				SetPageNosaveFree(pfn_to_page(start_pfn+i));
	}
	spin_unlock_irqrestore(&zone->lock, flags);
}

/*
 * Spill all of this CPU's per-cpu pages back into the buddy allocator.
 */
void drain_local_pages(void)
{
	unsigned long flags;

	local_irq_save(flags);	
	__drain_pages(smp_processor_id());
	local_irq_restore(flags);	
}
#endif /* CONFIG_PM */

static void zone_statistics(struct zonelist *zonelist, struct zone *z)
{
#ifdef CONFIG_NUMA
	unsigned long flags;
	int cpu;
	pg_data_t *pg = z->zone_pgdat;
	pg_data_t *orig = zonelist->zones[0]->zone_pgdat;
	struct per_cpu_pageset *p;

	local_irq_save(flags);
	cpu = smp_processor_id();
	p = &z->pageset[cpu];
	if (pg == orig) {
		z->pageset[cpu].numa_hit++;
	} else {
		p->numa_miss++;
		zonelist->zones[0]->pageset[cpu].numa_foreign++;
	}
	if (pg == NODE_DATA(numa_node_id()))
		p->local_node++;
	else
		p->other_node++;
	local_irq_restore(flags);
#endif
}

/*
 * Free a 0-order page
 */
static void FASTCALL(free_hot_cold_page(struct page *page, int cold));
static void fastcall free_hot_cold_page(struct page *page, int cold)		// 0:hot,1:cold
{
	struct zone *zone = page_zone(page);		// 反向查找页框描述符所在zone
	struct per_cpu_pages *pcp;			// per cpu 页框高速缓存
	unsigned long flags;

	arch_free_page(page, 0);		// 空定义

	kernel_map_pages(page, 1, 0);		// debug
	inc_page_state(pgfree);		// ???
	if (PageAnon(page))
		page->mapping = NULL;
	free_pages_check(__FUNCTION__, page);		// 检查页面，有问题打印堆栈
	pcp = &zone->pageset[get_cpu()].pcp[cold];
	local_irq_save(flags);
	if (pcp->count >= pcp->high)		// 当页框数大于等于高水位时，移除页框
		pcp->count -= free_pages_bulk(zone, pcp->batch, &pcp->list, 0);
	list_add(&page->lru, &pcp->list);		// 当前页框加入高速缓存中
	pcp->count++;
	local_irq_restore(flags);
	put_cpu();
}

void fastcall free_hot_page(struct page *page)
{
	free_hot_cold_page(page, 0);
}
	
void fastcall free_cold_page(struct page *page)
{
	free_hot_cold_page(page, 1);
}

static inline void prep_zero_page(struct page *page, int order, int gfp_flags)
{
	int i;

	BUG_ON((gfp_flags & (__GFP_WAIT | __GFP_HIGHMEM)) == __GFP_HIGHMEM);
	for(i = 0; i < (1 << order); i++)
		clear_highpage(page + i);
}

/*
 * Really, prep_compound_page() should be called from __rmqueue_bulk().  But
 * we cheat by calling it from here, in the order > 0 path.  Saves a branch
 * or two.
 */
static struct page *
buffered_rmqueue(struct zone *zone, int order, int gfp_flags)
{
	unsigned long flags;
	struct page *page = NULL;
	int cold = !!(gfp_flags & __GFP_COLD);

	// 只有分配单个页框时，才会用到per cpu页框高速缓存
	if (order == 0) {
		struct per_cpu_pages *pcp;

		pcp = &zone->pageset[get_cpu()].pcp[cold];
		local_irq_save(flags);
		// 页框数低于最低水位，需要重伙伴系统的空闲链表中补充
		if (pcp->count <= pcp->low)
			pcp->count += rmqueue_bulk(zone, 0, pcp->batch, &pcp->list);
		if (pcp->count) {
			page = list_entry(pcp->list.next, struct page, lru);
			list_del(&page->lru);
			pcp->count--;
		}
		local_irq_restore(flags);
		put_cpu();
	}

	if (page == NULL) {
		spin_lock_irqsave(&zone->lock, flags);
		page = __rmqueue(zone, order);
		spin_unlock_irqrestore(&zone->lock, flags);
	}

	if (page != NULL) {
		BUG_ON(bad_range(zone, page));
		mod_page_state_zone(zone, pgalloc, 1 << order);
		prep_new_page(page, order);

		if (gfp_flags & __GFP_ZERO)
			prep_zero_page(page, order, gfp_flags);

		if (order && (gfp_flags & __GFP_COMP))
			prep_compound_page(page, order);
	}
	return page;
}

/*
 * 如果空闲页面高于“标记”，则返回 1。这考虑了分配的顺序。
 *
 * zone: 当前zone
 * order:分配阶
 * mark：水位标记
 * classzone_idx：zone编号
 *
 * z, order, z->pages_low, classzone_idx, 0, 0
 */
int zone_watermark_ok(struct zone *z, int order, unsigned long mark,
		int classzone_idx, int can_try_harder, int gfp_high)
{
	/* 计算出的free_pages中保存的分配之后剩余的页框数 */
	long min = mark, free_pages = z->free_pages - (1 << order) + 1;
	int o;

	if (gfp_high)
		min -= min / 2;
	if (can_try_harder)
		min -= min / 4;

	if (free_pages <= min + z->lowmem_reserve[classzone_idx])
		return 0;
	for (o = 0; o < order; o++) {
		/* 在下一个order中，该order的页面变得不可用 */
		free_pages -= z->free_area[o].nr_free << o;

		/* 需要更少的高阶页面才能free */
		min >>= 1;

		if (free_pages <= min)
			return 0;
	}
	return 1;
}

/*
 * This is the 'heart' of the zoned buddy allocator.
 * 伙伴系统核心
 * 共有6个接口：
 * 有些接口不能够用于分配高于896M的物理地址
 * - alloc_pages		最终接口，—>alloc_pages_node->__alloc_pages
 * - alloc_page
 * - get_zeroed_page
 * - __get_free_pages
 * - __get_free_page
 * - __get_dma_pages
 *
 * gfp_mask: 修改器
 * order: 分配阶
 * zonelist: 备选列表
 */
struct page * fastcall __alloc_pages(unsigned int gfp_mask, unsigned int order, struct zonelist *zonelist)
{
	const int wait = gfp_mask & __GFP_WAIT;
	struct zone **zones, *z;
	struct page *page;
	struct reclaim_state reclaim_state;
	struct task_struct *p = current;
	int i;
	int classzone_idx;
	int do_retry;
	int can_try_harder;
	int did_some_progress;

	might_sleep_if(wait);		// debug

	/*
	 * 如果调用者不能运行直接回收，或者调用者有实时调度策略，调用者可能会更多地进入页面保留
	 */
	can_try_harder = (unlikely(rt_task(p)) && !in_interrupt()) || !wait;

	zones = zonelist->zones;  /* 适用于 gfp_mask 的区域列表，zones为数组，大小不定，以NULL结尾 */

	if (unlikely(zones[0] == NULL)) {		// 正常不会成立
		/* 这是否应该发生?? */
		return NULL;
	}

	classzone_idx = zone_idx(zones[0]);		// 返回在node_zones中的下标

 restart:
	/* 遍历 zonelist 一次，寻找一个有足够空闲的 zone */
	for (i = 0; (z = zones[i]) != NULL; i++) {

		if (!zone_watermark_ok(z, order, z->pages_low, classzone_idx, 0, 0))
			continue;

		page = buffered_rmqueue(z, order, gfp_mask);
		if (page)
			goto got_pg;
	}

	for (i = 0; (z = zones[i]) != NULL; i++)
		wakeup_kswapd(z, order);

	/*
	 * 再次检查区域列表。让 __GFP_HIGH 和来自实时任务的分配更深入到储备中
	 */
	for (i = 0; (z = zones[i]) != NULL; i++) {
		if (!zone_watermark_ok(z, order, z->pages_min,
				       classzone_idx, can_try_harder,
				       gfp_mask & __GFP_HIGH))
			continue;

		page = buffered_rmqueue(z, order, gfp_mask);
		if (page)
			goto got_pg;
	}

	/* 此分配应允许将来释放内存. */
	if (((p->flags & PF_MEMALLOC) || unlikely(test_thread_flag(TIF_MEMDIE))) && !in_interrupt()) {
		/* go through the zonelist yet again, ignoring mins */
		for (i = 0; (z = zones[i]) != NULL; i++) {
			page = buffered_rmqueue(z, order, gfp_mask);
			if (page)
				goto got_pg;
		}
		goto nopage;
	}

	/* 原子分配——我们无法平衡任何东西 */
	if (!wait)
		goto nopage;

rebalance:
	cond_resched();

	/* 我们现在进入同步回收 */
	p->flags |= PF_MEMALLOC;
	reclaim_state.reclaimed_slab = 0;
	p->reclaim_state = &reclaim_state;

	did_some_progress = try_to_free_pages(zones, gfp_mask, order);

	p->reclaim_state = NULL;
	p->flags &= ~PF_MEMALLOC;

	cond_resched();

	if (likely(did_some_progress)) {
		/*
		 * Go through the zonelist yet one more time, keep
		 * very high watermark here, this is only to catch
		 * a parallel oom killing, we must fail if we're still
		 * under heavy pressure.
		 */
		for (i = 0; (z = zones[i]) != NULL; i++) {
			if (!zone_watermark_ok(z, order, z->pages_min,
					       classzone_idx, can_try_harder,
					       gfp_mask & __GFP_HIGH))
				continue;

			page = buffered_rmqueue(z, order, gfp_mask);
			if (page)
				goto got_pg;
		}
	} else if ((gfp_mask & __GFP_FS) && !(gfp_mask & __GFP_NORETRY)) {
		/*
		 *再次遍历zonelist，这里保持非常高的watermark，这只是为了捕获一个并行的oom killing，如果我们仍然承受着沉重的压力，我们必须失败。
		 */
		for (i = 0; (z = zones[i]) != NULL; i++) {
			if (!zone_watermark_ok(z, order, z->pages_high,
					       classzone_idx, 0, 0))
				continue;

			page = buffered_rmqueue(z, order, gfp_mask);
			if (page)
				goto got_pg;
		}

		out_of_memory(gfp_mask);
		goto restart;
	}

	/*
	 * Don't let big-order allocations loop unless the caller explicitly
	 * requests that.  Wait for some write requests to complete then retry.
	 *
	 * In this implementation, __GFP_REPEAT means __GFP_NOFAIL for order
	 * <= 3, but that may not be true in other implementations.
	 */
	do_retry = 0;
	if (!(gfp_mask & __GFP_NORETRY)) {
		if ((order <= 3) || (gfp_mask & __GFP_REPEAT))
			do_retry = 1;
		if (gfp_mask & __GFP_NOFAIL)
			do_retry = 1;
	}
	if (do_retry) {
		blk_congestion_wait(WRITE, HZ/50);
		goto rebalance;
	}

nopage:
	if (!(gfp_mask & __GFP_NOWARN) && printk_ratelimit()) {
		printk(KERN_WARNING "%s: page allocation failure."
			" order:%d, mode:0x%x\n",
			p->comm, order, gfp_mask);
		dump_stack();
	}
	return NULL;
got_pg:
	zone_statistics(zonelist, z);
	return page;
}

EXPORT_SYMBOL(__alloc_pages);

/*
 * Common helper functions.
 * 结合了alloc_pages和page_address,返回一个页框的虚拟地址
 */
fastcall unsigned long __get_free_pages(unsigned int gfp_mask, unsigned int order)
{
	struct page * page;
	page = alloc_pages(gfp_mask, order);
	if (!page)
		return 0;
	return (unsigned long) page_address(page);          // 返回page的虚拟地址
}

EXPORT_SYMBOL(__get_free_pages);

// 分配一个填充的0的页框
fastcall unsigned long get_zeroed_page(unsigned int gfp_mask)
{
	struct page * page;

	/*
	 * get_zeroed_page() returns a 32-bit address, which cannot represent
	 * a highmem page
	 */
	BUG_ON(gfp_mask & __GFP_HIGHMEM);

	page = alloc_pages(gfp_mask | __GFP_ZERO, 0);
	if (page)
		return (unsigned long) page_address(page);
	return 0;
}

EXPORT_SYMBOL(get_zeroed_page);

void __pagevec_free(struct pagevec *pvec)
{
	int i = pagevec_count(pvec);

	while (--i >= 0)
		free_hot_cold_page(pvec->pages[i], pvec->cold);
}

// 释放页框，其他接口都是对该接口的封装
fastcall void __free_pages(struct page *, unsigned int order)
{	// 释放是必须是可换出的，并且引用计数_count为-1
	if (!PageReserved(page) && put_page_testzero(page)) {
		if (order == 0)
			free_hot_page(page);		// 加入页框高速缓存，hot
		else
			__free_pages_ok(page, order);		// 不加入页框高速缓存，直接到buddy system合并
	}
}

EXPORT_SYMBOL(__free_pages);

/* 释放页框，order为分配阶，0表示2^0，即一个页框，...
 * 释放内存，共有4个接口
 * 下面2个接口需要传入虚拟地址
 * - free_pages
 * - free_page
 * 下面的2个接口是需要传入页框描述符地址
 * - __free_page
 * - __free_pages		最终接口
 * */
fastcall void free_pages(unsigned long addr, unsigned int order)
{
	if (addr != 0) {
		BUG_ON(!virt_addr_valid((void *)addr));
		__free_pages(virt_to_page((void *)addr), order);
	}
}

EXPORT_SYMBOL(free_pages);

/*
 * Total amount of free (allocatable) RAM:
 */
unsigned int nr_free_pages(void)
{
	unsigned int sum = 0;
	struct zone *zone;

	for_each_zone(zone)
		sum += zone->free_pages;

	return sum;
}

EXPORT_SYMBOL(nr_free_pages);

#ifdef CONFIG_NUMA
unsigned int nr_free_pages_pgdat(pg_data_t *pgdat)
{
	unsigned int i, sum = 0;

	for (i = 0; i < MAX_NR_ZONES; i++)
		sum += pgdat->node_zones[i].free_pages;

	return sum;
}
#endif

static unsigned int nr_free_zone_pages(int offset)
{
	pg_data_t *pgdat;
	unsigned int sum = 0;

	// 遍历所有内存节点
	for_each_pgdat(pgdat) {
		struct zonelist *zonelist = pgdat->node_zonelists + offset;
		struct zone **zonep = zonelist->zones;
		struct zone *zone;

		for (zone = *zonep++; zone; zone = *zonep++) {
			unsigned long size = zone->present_pages;
			unsigned long high = zone->pages_high;
			if (size > high)
				sum += size - high;
		}
	}

	return sum;
}

/*
 * Amount of free RAM allocatable within ZONE_DMA and ZONE_NORMAL
 */
unsigned int nr_free_buffer_pages(void)
{
	return nr_free_zone_pages(GFP_USER & GFP_ZONEMASK);
}

/*
 * Amount of free RAM allocatable within all zones
 */
unsigned int nr_free_pagecache_pages(void)
{
	return nr_free_zone_pages(GFP_HIGHUSER & GFP_ZONEMASK);
}

#ifdef CONFIG_HIGHMEM
unsigned int nr_free_highpages (void)
{
	pg_data_t *pgdat;
	unsigned int pages = 0;

	for_each_pgdat(pgdat)
		pages += pgdat->node_zones[ZONE_HIGHMEM].free_pages;

	return pages;
}
#endif

#ifdef CONFIG_NUMA
static void show_node(struct zone *zone)
{
	printk("Node %d ", zone->zone_pgdat->node_id);
}
#else
#define show_node(zone)	do { } while (0)
#endif

/*
 * Accumulate the page_state information across all CPUs.
 * The result is unavoidably approximate - it can change
 * during and after execution of this function.
 */
static DEFINE_PER_CPU(struct page_state, page_states) = {0};

atomic_t nr_pagecache = ATOMIC_INIT(0);
EXPORT_SYMBOL(nr_pagecache);
#ifdef CONFIG_SMP
DEFINE_PER_CPU(long, nr_pagecache_local) = 0;
#endif

void __get_page_state(struct page_state *ret, int nr)
{
	int cpu = 0;

	memset(ret, 0, sizeof(*ret));

	cpu = first_cpu(cpu_online_map);
	while (cpu < NR_CPUS) {
		unsigned long *in, *out, off;

		in = (unsigned long *)&per_cpu(page_states, cpu);

		cpu = next_cpu(cpu, cpu_online_map);

		if (cpu < NR_CPUS)
			prefetch(&per_cpu(page_states, cpu));

		out = (unsigned long *)ret;
		for (off = 0; off < nr; off++)
			*out++ += *in++;
	}
}

void get_page_state(struct page_state *ret)
{
	int nr;

	nr = offsetof(struct page_state, GET_PAGE_STATE_LAST);
	nr /= sizeof(unsigned long);

	__get_page_state(ret, nr + 1);
}

void get_full_page_state(struct page_state *ret)
{
	__get_page_state(ret, sizeof(*ret) / sizeof(unsigned long));
}

unsigned long __read_page_state(unsigned offset)
{
	unsigned long ret = 0;
	int cpu;

	for_each_online_cpu(cpu) {
		unsigned long in;

		in = (unsigned long)&per_cpu(page_states, cpu) + offset;
		ret += *((unsigned long *)in);
	}
	return ret;
}

void __mod_page_state(unsigned offset, unsigned long delta)
{
	unsigned long flags;
	void* ptr;

	local_irq_save(flags);
	ptr = &__get_cpu_var(page_states);
	*(unsigned long*)(ptr + offset) += delta;
	local_irq_restore(flags);
}

EXPORT_SYMBOL(__mod_page_state);

void __get_zone_counts(unsigned long *active, unsigned long *inactive,
			unsigned long *free, struct pglist_data *pgdat)
{
	struct zone *zones = pgdat->node_zones;
	int i;

	*active = 0;
	*inactive = 0;
	*free = 0;
	for (i = 0; i < MAX_NR_ZONES; i++) {
		*active += zones[i].nr_active;
		*inactive += zones[i].nr_inactive;
		*free += zones[i].free_pages;
	}
}

void get_zone_counts(unsigned long *active,
		unsigned long *inactive, unsigned long *free)
{
	struct pglist_data *pgdat;

	*active = 0;
	*inactive = 0;
	*free = 0;
	for_each_pgdat(pgdat) {
		unsigned long l, m, n;
		__get_zone_counts(&l, &m, &n, pgdat);
		*active += l;
		*inactive += m;
		*free += n;
	}
}

void si_meminfo(struct sysinfo *val)
{
	val->totalram = totalram_pages;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = nr_blockdev_pages();
#ifdef CONFIG_HIGHMEM
	val->totalhigh = totalhigh_pages;
	val->freehigh = nr_free_highpages();
#else
	val->totalhigh = 0;
	val->freehigh = 0;
#endif
	val->mem_unit = PAGE_SIZE;
}

EXPORT_SYMBOL(si_meminfo);

#ifdef CONFIG_NUMA
void si_meminfo_node(struct sysinfo *val, int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);

	val->totalram = pgdat->node_present_pages;
	val->freeram = nr_free_pages_pgdat(pgdat);
	val->totalhigh = pgdat->node_zones[ZONE_HIGHMEM].present_pages;
	val->freehigh = pgdat->node_zones[ZONE_HIGHMEM].free_pages;
	val->mem_unit = PAGE_SIZE;
}
#endif

#define K(x) ((x) << (PAGE_SHIFT-10))

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 */
void show_free_areas(void)
{
	struct page_state ps;
	int cpu, temperature;
	unsigned long active;
	unsigned long inactive;
	unsigned long free;
	struct zone *zone;

	for_each_zone(zone) {
		show_node(zone);
		printk("%s per-cpu:", zone->name);

		if (!zone->present_pages) {
			printk(" empty\n");
			continue;
		} else
			printk("\n");

		for (cpu = 0; cpu < NR_CPUS; ++cpu) {
			struct per_cpu_pageset *pageset;

			if (!cpu_possible(cpu))
				continue;

			pageset = zone->pageset + cpu;

			for (temperature = 0; temperature < 2; temperature++)
				printk("cpu %d %s: low %d, high %d, batch %d\n",
					cpu,
					temperature ? "cold" : "hot",
					pageset->pcp[temperature].low,
					pageset->pcp[temperature].high,
					pageset->pcp[temperature].batch);
		}
	}

	get_page_state(&ps);
	get_zone_counts(&active, &inactive, &free);

	printk("\nFree pages: %11ukB (%ukB HighMem)\n",
		K(nr_free_pages()),
		K(nr_free_highpages()));

	printk("Active:%lu inactive:%lu dirty:%lu writeback:%lu "
		"unstable:%lu free:%u slab:%lu mapped:%lu pagetables:%lu\n",
		active,
		inactive,
		ps.nr_dirty,
		ps.nr_writeback,
		ps.nr_unstable,
		nr_free_pages(),
		ps.nr_slab,
		ps.nr_mapped,
		ps.nr_page_table_pages);

	for_each_zone(zone) {
		int i;

		show_node(zone);
		printk("%s"
			" free:%lukB"
			" min:%lukB"
			" low:%lukB"
			" high:%lukB"
			" active:%lukB"
			" inactive:%lukB"
			" present:%lukB"
			" pages_scanned:%lu"
			" all_unreclaimable? %s"
			"\n",
			zone->name,
			K(zone->free_pages),
			K(zone->pages_min),
			K(zone->pages_low),
			K(zone->pages_high),
			K(zone->nr_active),
			K(zone->nr_inactive),
			K(zone->present_pages),
			zone->pages_scanned,
			(zone->all_unreclaimable ? "yes" : "no")
			);
		printk("lowmem_reserve[]:");
		for (i = 0; i < MAX_NR_ZONES; i++)
			printk(" %lu", zone->lowmem_reserve[i]);
		printk("\n");
	}

	for_each_zone(zone) {
 		unsigned long nr, flags, order, total = 0;

		show_node(zone);
		printk("%s: ", zone->name);
		if (!zone->present_pages) {
			printk("empty\n");
			continue;
		}

		spin_lock_irqsave(&zone->lock, flags);
		for (order = 0; order < MAX_ORDER; order++) {
			nr = zone->free_area[order].nr_free;
			total += nr << order;
			printk("%lu*%lukB ", nr, K(1UL) << order);
		}
		spin_unlock_irqrestore(&zone->lock, flags);
		printk("= %lukB\n", K(total));
	}

	show_swap_cache_info();
}

/*
 * 构建分配回退区域列表
 * 按照k: ZONE_NORMAL、ZONE_DMA、ZONE_HIGHMEM的顺序初始化
 * */
static int __init build_zonelists_node(pg_data_t *pgdat, struct zonelist *zonelist, int j, int k)
{	// 注意，case后面没有break
	switch (k) {
		struct zone *zone;
	default:
		BUG();
	case ZONE_HIGHMEM:
		zone = pgdat->node_zones + ZONE_HIGHMEM;
		if (zone->present_pages) {
#ifndef CONFIG_HIGHMEM
			BUG();
#endif
			zonelist->zones[j++] = zone;
		}
	case ZONE_NORMAL:
		zone = pgdat->node_zones + ZONE_NORMAL;
		if (zone->present_pages)
			zonelist->zones[j++] = zone;
	case ZONE_DMA:
		zone = pgdat->node_zones + ZONE_DMA;
		if (zone->present_pages)
			zonelist->zones[j++] = zone;
	}

	return j;
}

#ifdef CONFIG_NUMA
#define MAX_NODE_LOAD (num_online_nodes())
static int __initdata node_load[MAX_NUMNODES];
/**
 * find_next_best_node - 找到应该出现在给定节点的后备列表中的下一个节点
 * @node: 我们要附加其后备列表的节点
 * @used_node_mask: 已使用节点的 nodemask_t
 *
 * 我们使用许多因素来确定哪个是应该出现在给定节点的后备列表中的下一个节点。
 * 该节点不应该已经出现在@node 的回退列表中，它应该是根据距离数组
 * （其中包含从每个节点到系统中每个节点的任意距离值）下一个最近的节点，
 * 并且还应该优先选择没有CPU，因为否则它们对它们的分配压力可能很小。
 *
 * 如果未找到节点，则返回 -1。
 */
static int __init find_next_best_node(int node, nodemask_t *used_node_mask)
{
	int i, n, val;
	int min_val = INT_MAX;
	int best_node = -1;

	for_each_online_node(i) {
		cpumask_t tmp;

		/* 从本地节点开始 */
		n = (node+i) % num_online_nodes();

		/* 不希望一个节点出现多次 */
		if (node_isset(n, *used_node_mask))
			continue;

		/* 如果我们还没有使用本地节点 */
		if (!node_isset(node, *used_node_mask)) {
			best_node = node;
			break;
		}

		/* 使用距离数组求距离 */
		val = node_distance(node, n);

		/* 优先考虑无cpu和未使用的节点 */
		tmp = node_to_cpumask(n);
		if (!cpus_empty(tmp))
			val += PENALTY_FOR_NODE_WITH_CPUS;		// 有cpu时+1

		/* 对负载较少的节点略有偏好 */
		val *= (MAX_NODE_LOAD*MAX_NUMNODES);
		val += node_load[n];

		if (val < min_val) {
			min_val = val;
			best_node = n;
		}
	}

	if (best_node >= 0)
		node_set(best_node, *used_node_mask);

	return best_node;
}

// 配置NUMA调用
// 该函数的任务是，在当前处理的结点和系统中其他结点的内存域之间建立一种等级次序。
static void __init build_zonelists(pg_data_t *pgdat)
{
	int i, j, k, node, local_node;
	int prev_node, load;
	struct zonelist *zonelist;
	nodemask_t used_mask;

	/* 初始化区域列表 */
	for (i = 0; i < GFP_ZONETYPES; i++) {   // GFP_ZONETYPES 3
		zonelist = (pgdat->node_zonelists) + i;
		memset(zonelist, 0, sizeof(*zonelist));
		zonelist->zones[0] = NULL;
	}

	/* NUMA 感知节点排序 */
	local_node = pgdat->node_id;		// 当前节点id
	load = num_online_nodes();		// 在线节点数量
	prev_node = local_node;
	nodes_clear(used_mask);		// 处理各个节点时会清空
	while ((node = find_next_best_node(local_node, &used_mask)) >= 0) {
		/*
		 * 我们不想对特定节点施加压力。
		 * 因此对相同距离组中的第一个节点添加惩罚以使其循环。
		 */
		if (node_distance(local_node, node) != node_distance(local_node, prev_node))
			node_load[node] += load;
		prev_node = node;
		load--;
		for (i = 0; i < GFP_ZONETYPES; i++) {
			zonelist = pgdat->node_zonelists + i;
			for (j = 0; zonelist->zones[j] != NULL; j++);

			k = ZONE_NORMAL;
			if (i & __GFP_HIGHMEM)
				k = ZONE_HIGHMEM;
			if (i & __GFP_DMA)
				k = ZONE_DMA;

	 		j = build_zonelists_node(NODE_DATA(node), zonelist, j, k);
			zonelist->zones[j] = NULL;
		}
	}
}

#else	/* CONFIG_NUMA */

/* 配置UMA调用
 * 假设有3个节点，分配的结果为：
 * n0:
 * -0: n0/d0,n1/d1,n2/d2,n3/d3,NULL
 * -1: d0,d1,d2,d3,NULL
 * -2: h0/n0/d0,h1/n1/d1,h2/n2/d2,h3/n3/d3,NULL
 *
 * n1:
 * -0: n1/d1,n2/d2,n3/d3,n0/d0,NULL
 * -1: d1,d2,d3,d0,NULL
 * -2: h1/n1/d1,h2/n2/d2,h3/n3/d3,h0/n0/d0,NULL
 *
 * ....
 * */
static void __init build_zonelists(pg_data_t *pgdat)
{
	int i, j, k, node, local_node;

	local_node = pgdat->node_id;
	for (i = 0; i < GFP_ZONETYPES; i++) {		// GFP_ZONETYPES为3，表示后备区域列表的大小
		struct zonelist *zonelist;

		zonelist = (pgdat->node_zonelists) + i;
		memset(zonelist, 0, sizeof(*zonelist));

		j = 0;
		k = ZONE_NORMAL;		// 默认区域
		if (i & __GFP_HIGHMEM)
			k = ZONE_HIGHMEM;
		if (i & __GFP_DMA)
			k = ZONE_DMA;
		/* i:0		k:ZONE_NORMAL		j:0,1,2
		 * i:1		k:ZONE_DMA
		 * i:2		k:ZONE_HIGHMEM
		 * */
 		j = build_zonelists_node(pgdat, zonelist, j, k);		// 处理本地节点的后备列表
 		/*
 		 * 现在我们构建区域列表，以便它包含所有其他节点的区域。
 		 * 我们不想对特定节点施加压力，因此在为节点 N 构建区域时，
 		 * 我们确保紧跟在本地区域之后的区域是来自节点 N+1（模 N）的区域
 		 */
 		// 配置成UMA也可能会用到节点来管理
		for (node = local_node + 1; node < MAX_NUMNODES; node++) {
			if (!node_online(node))
				continue;
			j = build_zonelists_node(NODE_DATA(node), zonelist, j, k);
		}
		for (node = 0; node < local_node; node++) {
			if (!node_online(node))
				continue;
			j = build_zonelists_node(NODE_DATA(node), zonelist, j, k);
		}

		zonelist->zones[j] = NULL;
	}
}

#endif	/* CONFIG_NUMA */

void __init build_all_zonelists(void)
{
	int i;

	for_each_online_node(i)     // 遍历所以的online节点
		build_zonelists(NODE_DATA(i));      // 宏NODE_DATA取到对应内存结点pd_data_t
	printk("Built %i zonelists\n", num_online_nodes());
}

/*
 * Helper functions to size the waitqueue hash table.
 * Essentially these want to choose hash table sizes sufficiently
 * large so that collisions trying to wait on pages are rare.
 * But in fact, the number of active page waitqueues on typical
 * systems is ridiculously low, less than 200. So this is even
 * conservative, even though it seems large.
 *
 * The constant PAGES_PER_WAITQUEUE specifies the ratio of pages to
 * waitqueues, i.e. the size of the waitq table given the number of pages.
 */
#define PAGES_PER_WAITQUEUE	256

static inline unsigned long wait_table_size(unsigned long pages)
{
	unsigned long size = 1;

	pages /= PAGES_PER_WAITQUEUE;

	while (size < pages)
		size <<= 1;

	/*
	 * Once we have dozens or even hundreds of threads sleeping
	 * on IO we've got bigger problems than wait queue collision.
	 * Limit the size of the wait table to a reasonable size.
	 */
	size = min(size, 4096UL);

	return max(size, 4UL);
}

/*
 * This is an integer logarithm so that shifts can be used later
 * to extract the more random high bits from the multiplicative
 * hash function before the remainder is taken.
 */
static inline unsigned long wait_table_bits(unsigned long size)
{
	return ffz(~size);
}

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

/* 计算内存节点中 present和spanned的页框数
 * */
static void __init calculate_zone_totalpages(struct pglist_data *pgdat,
		unsigned long *zones_size, unsigned long *zholes_size)      // zholes_size:NULL
{
	unsigned long realtotalpages, totalpages = 0;
	int i;

	for (i = 0; i < MAX_NR_ZONES; i++)
		totalpages += zones_size[i];
	pgdat->node_spanned_pages = totalpages; // 内存节点的全部页框，含空洞

	realtotalpages = totalpages;
	if (zholes_size)
		for (i = 0; i < MAX_NR_ZONES; i++)
			realtotalpages -= zholes_size[i];       // 把内存节点中各个区域的空洞数减去
	pgdat->node_present_pages = realtotalpages;     // 真实的页框数据，即不含空洞的页框的数
	printk(KERN_DEBUG "On node %d totalpages: %lu\n", pgdat->node_id, realtotalpages);
}


/*
 * 最初所有页面都被保留
 * - 一旦早期启动过程完成，free_all_bootmem() 释放空闲页面。
 * 非原子初始化，单程。
 *
 * 初始化当前zone管理页框描述符
 * size: 当前zone的页框数量
 * nid:内存节点ID
 * zone: 当前zone指针
 * start_pfn：当前zone的起始页框号
 */
void __init memmap_init_zone(unsigned long size, int nid, unsigned long zone, unsigned long start_pfn)
{
	struct page *start = pfn_to_page(start_pfn);		// 通过mem_map找到页框描述符的地址
	struct page *page;

	for (page = start; page < (start + size); page++) {		// size为当前zone的页框数
		set_page_zone(page, NODEZONE(nid, zone));		// 设置flag，反引用zone
		set_page_count(page, 0);		// 引用次数设置为0
		reset_page_mapcount(page);		// 重置mapcount次数
		SetPageReserved(page);			// 设置为永不换出
		INIT_LIST_HEAD(&page->lru);
#ifdef WANT_PAGE_VIRTUAL
		/* The shift won't overflow because ZONE_NORMAL is below 4G. */
		if (!is_highmem_idx(zone))
			set_page_address(page, __va(start_pfn << PAGE_SHIFT));
#endif
		start_pfn++;
	}
}
/*
 * 初始化管理free_area的管理数组，为了实现buddy system
 * */
void zone_init_free_lists(struct pglist_data *pgdat, struct zone *zone, unsigned long size)
{
	int order;
	for (order = 0; order < MAX_ORDER ; order++) {		// MAX_ORDER为11
		INIT_LIST_HEAD(&zone->free_area[order].free_list);
		zone->free_area[order].nr_free = 0;
	}
}

#ifndef __HAVE_ARCH_MEMMAP_INIT
#define memmap_init(size, nid, zone, start_pfn) \
	memmap_init_zone((size), (nid), (zone), (start_pfn))
#endif

/*
 * Set up the zone data structures:
 *   - 标记所有页面保留
 *   - 将所有内存队列标记为空
 *   - 清除内存位图
 */
static void __init free_area_init_core(struct pglist_data *pgdat,
		unsigned long *zones_size, unsigned long *zholes_size)	// zholes_size:NULL
{
	unsigned long i, j;
	const unsigned long zone_required_alignment = 1UL << (MAX_ORDER-1);
	int cpu, nid = pgdat->node_id;
	unsigned long zone_start_pfn = pgdat->node_start_pfn;		// 0

	pgdat->nr_zones = 0;
	init_waitqueue_head(&pgdat->kswapd_wait);
	pgdat->kswapd_max_order = 0;


	// 初始化3个zone
	for (j = 0; j < MAX_NR_ZONES; j++) {		// MAX_NR_ZONES为3
		struct zone *zone = pgdat->node_zones + j;		// 获取当前pgdata中各个zone的指针
		unsigned long size, realsize;
		unsigned long batch;

		zone_table[NODEZONE(nid, j)] = zone;        // 初始化zone_table，为了支持从页框描述符找到其所在的内存区

		realsize = size = zones_size[j];		// 当前zone中管理的页框数量
		if (zholes_size)		// 没有hole
			realsize -= zholes_size[j];

		if (j == ZONE_DMA || j == ZONE_NORMAL)
			nr_kernel_pages += realsize;		// 低端内存页框数，0x38000000
		nr_all_pages += realsize;		// 总的页框数，1M

		// 正常情况下物理内存是没有hole的，所以下面两个值是一样的
		zone->spanned_pages = size;
		zone->present_pages = realsize;

		zone->name = zone_names[j];		// 名称，一个字符串
		spin_lock_init(&zone->lock);
		spin_lock_init(&zone->lru_lock);

		zone->zone_pgdat = pgdat;		// 反引用
		zone->free_pages = 0;		// 空闲页框

		zone->temp_priority = zone->prev_priority = DEF_PRIORITY;		// 扫描优先级

		/*
		 * 处理per cpu页框高速缓存
		 * per-cpu-pages 池设置为区域大小的 1000 左右。
		 * 但不超过 14 兆 - 超出 L2 缓存的大小没有意义。
		 *
		 * 好的，所以我们不知道缓存有多大。所以猜。
		 */
		batch = zone->present_pages / 1024;		// batch为低于低水位时补充的大小，或高于高水位移除的大小
		if (batch * PAGE_SIZE > 256 * 1024)
			batch = (256 * 1024) / PAGE_SIZE;
		batch /= 4;		/* We effectively *= 4 below */
		if (batch < 1)
			batch = 1;

		for (cpu = 0; cpu < NR_CPUS; cpu++) {
			struct per_cpu_pages *pcp;

			pcp = &zone->pageset[cpu].pcp[0];	/* hot */
			pcp->count = 0;
			pcp->low = 2 * batch;
			pcp->high = 6 * batch;
			pcp->batch = 1 * batch;
			INIT_LIST_HEAD(&pcp->list);

			pcp = &zone->pageset[cpu].pcp[1];	/* cold */
			pcp->count = 0;
			pcp->low = 0;
			pcp->high = 2 * batch;
			pcp->batch = 1 * batch;
			INIT_LIST_HEAD(&pcp->list);
		}
		printk(KERN_DEBUG "  %s zone: %lu pages, LIFO batch:%lu\n", zone_names[j], realsize, batch);
		INIT_LIST_HEAD(&zone->active_list);
		INIT_LIST_HEAD(&zone->inactive_list);

		zone->nr_scan_active = 0;
		zone->nr_scan_inactive = 0;
		zone->nr_active = 0;
		zone->nr_inactive = 0;
		if (!size)
			continue;

		/*
		 * 每页等待队列机制使用每个区域的散列等待队列。
		 */
		zone->wait_table_size = wait_table_size(size);
		zone->wait_table_bits =
			wait_table_bits(zone->wait_table_size);
		zone->wait_table = (wait_queue_head_t *)
			alloc_bootmem_node(pgdat, zone->wait_table_size
						* sizeof(wait_queue_head_t));

		for(i = 0; i < zone->wait_table_size; ++i)
			init_waitqueue_head(zone->wait_table + i);

		pgdat->nr_zones = j+1;

		zone->zone_mem_map = pfn_to_page(zone_start_pfn);
		zone->zone_start_pfn = zone_start_pfn;

		if ((zone_start_pfn) & (zone_required_alignment-1))
			printk(KERN_CRIT "BUG: wrong zone alignment, it will crash\n");

		memmap_init(size, nid, j, zone_start_pfn);		// 初始化当前zone中管理的页框

		zone_start_pfn += size;

		zone_init_free_lists(pgdat, zone, zone->spanned_pages);
	}
}

// 为节点分配页框数组
void __init node_alloc_mem_map(struct pglist_data *pgdat)
{
	unsigned long size;

	size = (pgdat->node_spanned_pages + 1) * sizeof(struct page);
	pgdat->node_mem_map = alloc_bootmem_node(pgdat, size);		// 为页框数组分配内存

	// 没有配置非连续内存
	#ifndef CONFIG_DISCONTIGMEM
	mem_map = contig_page_data.node_mem_map;		// 初始化页框数组指针
    #endif
}

void __init free_area_init_node(int nid, struct pglist_data *pgdat,
		unsigned long *zones_size, unsigned long node_start_pfn,    // node_start_pfn:0
		unsigned long *zholes_size)     // zholes_size:NULL
{
	pgdat->node_id = nid;       // 内存节点编号，0
	pgdat->node_start_pfn = node_start_pfn;     // 内存节点起始页框号，0
	calculate_zone_totalpages(pgdat, zones_size, zholes_size);

	if (!pfn_to_page(node_start_pfn))		// 为页框数组分配内存
		node_alloc_mem_map(pgdat);

	free_area_init_core(pgdat, zones_size, zholes_size);
}

// 没有配置非连续内存
#ifndef CONFIG_DISCONTIGMEM
static bootmem_data_t contig_bootmem_data;
struct pglist_data contig_page_data = { .bdata = &contig_bootmem_data };

EXPORT_SYMBOL(contig_page_data);

void __init free_area_init(unsigned long *zones_size)       // 连续内存
{
	free_area_init_node(0, &contig_page_data, zones_size, __pa(PAGE_OFFSET) >> PAGE_SHIFT, NULL);
}
#endif

#ifdef CONFIG_PROC_FS

#include <linux/seq_file.h>

static void *frag_start(struct seq_file *m, loff_t *pos)
{
	pg_data_t *pgdat;
	loff_t node = *pos;

	for (pgdat = pgdat_list; pgdat && node; pgdat = pgdat->pgdat_next)
		--node;

	return pgdat;
}

static void *frag_next(struct seq_file *m, void *arg, loff_t *pos)
{
	pg_data_t *pgdat = (pg_data_t *)arg;

	(*pos)++;
	return pgdat->pgdat_next;
}

static void frag_stop(struct seq_file *m, void *arg)
{
}

/* 
 * This walks the free areas for each zone.
 */
static int frag_show(struct seq_file *m, void *arg)
{
	pg_data_t *pgdat = (pg_data_t *)arg;
	struct zone *zone;
	struct zone *node_zones = pgdat->node_zones;
	unsigned long flags;
	int order;

	for (zone = node_zones; zone - node_zones < MAX_NR_ZONES; ++zone) {
		if (!zone->present_pages)
			continue;

		spin_lock_irqsave(&zone->lock, flags);
		seq_printf(m, "Node %d, zone %8s ", pgdat->node_id, zone->name);
		for (order = 0; order < MAX_ORDER; ++order)
			seq_printf(m, "%6lu ", zone->free_area[order].nr_free);
		spin_unlock_irqrestore(&zone->lock, flags);
		seq_putc(m, '\n');
	}
	return 0;
}

struct seq_operations fragmentation_op = {
	.start	= frag_start,
	.next	= frag_next,
	.stop	= frag_stop,
	.show	= frag_show,
};

static char *vmstat_text[] = {
	"nr_dirty",
	"nr_writeback",
	"nr_unstable",
	"nr_page_table_pages",
	"nr_mapped",
	"nr_slab",

	"pgpgin",
	"pgpgout",
	"pswpin",
	"pswpout",
	"pgalloc_high",

	"pgalloc_normal",
	"pgalloc_dma",
	"pgfree",
	"pgactivate",
	"pgdeactivate",

	"pgfault",
	"pgmajfault",
	"pgrefill_high",
	"pgrefill_normal",
	"pgrefill_dma",

	"pgsteal_high",
	"pgsteal_normal",
	"pgsteal_dma",
	"pgscan_kswapd_high",
	"pgscan_kswapd_normal",

	"pgscan_kswapd_dma",
	"pgscan_direct_high",
	"pgscan_direct_normal",
	"pgscan_direct_dma",
	"pginodesteal",

	"slabs_scanned",
	"kswapd_steal",
	"kswapd_inodesteal",
	"pageoutrun",
	"allocstall",

	"pgrotated",
};

static void *vmstat_start(struct seq_file *m, loff_t *pos)
{
	struct page_state *ps;

	if (*pos >= ARRAY_SIZE(vmstat_text))
		return NULL;

	ps = kmalloc(sizeof(*ps), GFP_KERNEL);
	m->private = ps;
	if (!ps)
		return ERR_PTR(-ENOMEM);
	get_full_page_state(ps);
	ps->pgpgin /= 2;		/* sectors -> kbytes */
	ps->pgpgout /= 2;
	return (unsigned long *)ps + *pos;
}

static void *vmstat_next(struct seq_file *m, void *arg, loff_t *pos)
{
	(*pos)++;
	if (*pos >= ARRAY_SIZE(vmstat_text))
		return NULL;
	return (unsigned long *)m->private + *pos;
}

static int vmstat_show(struct seq_file *m, void *arg)
{
	unsigned long *l = arg;
	unsigned long off = l - (unsigned long *)m->private;

	seq_printf(m, "%s %lu\n", vmstat_text[off], *l);
	return 0;
}

static void vmstat_stop(struct seq_file *m, void *arg)
{
	kfree(m->private);
	m->private = NULL;
}

struct seq_operations vmstat_op = {
	.start	= vmstat_start,
	.next	= vmstat_next,
	.stop	= vmstat_stop,
	.show	= vmstat_show,
};

#endif /* CONFIG_PROC_FS */

#ifdef CONFIG_HOTPLUG_CPU
static int page_alloc_cpu_notify(struct notifier_block *self,
				 unsigned long action, void *hcpu)
{
	int cpu = (unsigned long)hcpu;
	long *count;
	unsigned long *src, *dest;

	if (action == CPU_DEAD) {
		int i;

		/* Drain local pagecache count. */
		count = &per_cpu(nr_pagecache_local, cpu);
		atomic_add(*count, &nr_pagecache);
		*count = 0;
		local_irq_disable();
		__drain_pages(cpu);

		/* Add dead cpu's page_states to our own. */
		dest = (unsigned long *)&__get_cpu_var(page_states);
		src = (unsigned long *)&per_cpu(page_states, cpu);

		for (i = 0; i < sizeof(struct page_state)/sizeof(unsigned long);
				i++) {
			dest[i] += src[i];
			src[i] = 0;
		}

		local_irq_enable();
	}
	return NOTIFY_OK;
}
#endif /* CONFIG_HOTPLUG_CPU */

void __init page_alloc_init(void)
{
	hotcpu_notifier(page_alloc_cpu_notify, 0);
}

/*
 * setup_per_zone_lowmem_reserve - called whenever
 *	sysctl_lower_zone_reserve_ratio changes.  Ensures that each zone
 *	has a correct pages reserved value, so an adequate number of
 *	pages are left in the zone after a successful __alloc_pages().
 */
static void setup_per_zone_lowmem_reserve(void)
{
	struct pglist_data *pgdat;
	int j, idx;

	for_each_pgdat(pgdat) {
		for (j = 0; j < MAX_NR_ZONES; j++) {
			struct zone * zone = pgdat->node_zones + j;
			unsigned long present_pages = zone->present_pages;

			zone->lowmem_reserve[j] = 0;

			for (idx = j-1; idx >= 0; idx--) {
				struct zone * lower_zone = pgdat->node_zones + idx;

				lower_zone->lowmem_reserve[j] = present_pages / sysctl_lowmem_reserve_ratio[idx];
				present_pages += lower_zone->present_pages;
			}
		}
	}
}

/*
 * setup_per_zone_pages_min - called when min_free_kbytes changes.  Ensures 
 *	that the pages_{min,low,high} values for each zone are set correctly 
 *	with respect to min_free_kbytes.
 */
static void setup_per_zone_pages_min(void)
{
	unsigned long pages_min = min_free_kbytes >> (PAGE_SHIFT - 10);
	unsigned long lowmem_pages = 0;
	struct zone *zone;
	unsigned long flags;

	/* Calculate total number of !ZONE_HIGHMEM pages */
	for_each_zone(zone) {
		if (!is_highmem(zone))
			lowmem_pages += zone->present_pages;
	}

	for_each_zone(zone) {
		spin_lock_irqsave(&zone->lru_lock, flags);
		if (is_highmem(zone)) {
			/*
			 * Often, highmem doesn't need to reserve any pages.
			 * But the pages_min/low/high values are also used for
			 * batching up page reclaim activity so we need a
			 * decent value here.
			 */
			int min_pages;

			min_pages = zone->present_pages / 1024;
			if (min_pages < SWAP_CLUSTER_MAX)
				min_pages = SWAP_CLUSTER_MAX;
			if (min_pages > 128)
				min_pages = 128;
			zone->pages_min = min_pages;
		} else {
			/* if it's a lowmem zone, reserve a number of pages 
			 * proportionate to the zone's size.
			 */
			zone->pages_min = (pages_min * zone->present_pages) / lowmem_pages;
		}

		/*
		 * When interpreting these watermarks, just keep in mind that:
		 * zone->pages_min == (zone->pages_min * 4) / 4;
		 */
		zone->pages_low   = (zone->pages_min * 5) / 4;
		zone->pages_high  = (zone->pages_min * 6) / 4;
		spin_unlock_irqrestore(&zone->lru_lock, flags);
	}
}

/*
 * Initialise min_free_kbytes.
 *
 * For small machines we want it small (128k min).  For large machines
 * we want it large (64MB max).  But it is not linear, because network
 * bandwidth does not increase linearly with machine size.  We use
 *
 * 	min_free_kbytes = 4 * sqrt(lowmem_kbytes), for better accuracy:
 *	min_free_kbytes = sqrt(lowmem_kbytes * 16)
 *
 * which yields
 *
 * 16MB:	512k
 * 32MB:	724k
 * 64MB:	1024k
 * 128MB:	1448k
 * 256MB:	2048k
 * 512MB:	2896k
 * 1024MB:	4096k
 * 2048MB:	5792k
 * 4096MB:	8192k
 * 8192MB:	11584k
 * 16384MB:	16384k
 */
static int __init init_per_zone_pages_min(void)
{
	unsigned long lowmem_kbytes;

	lowmem_kbytes = nr_free_buffer_pages() * (PAGE_SIZE >> 10);

	min_free_kbytes = int_sqrt(lowmem_kbytes * 16);
	if (min_free_kbytes < 128)
		min_free_kbytes = 128;
	if (min_free_kbytes > 65536)
		min_free_kbytes = 65536;

	setup_per_zone_pages_min();
	setup_per_zone_lowmem_reserve();
	return 0;
}
module_init(init_per_zone_pages_min)

/*
 * min_free_kbytes_sysctl_handler - just a wrapper around proc_dointvec() so 
 *	that we can call two helper functions whenever min_free_kbytes
 *	changes.
 */
int min_free_kbytes_sysctl_handler(ctl_table *table, int write, 
		struct file *file, void __user *buffer, size_t *length, loff_t *ppos)
{
	proc_dointvec(table, write, file, buffer, length, ppos);
	setup_per_zone_pages_min();
	return 0;
}

/*
 * lowmem_reserve_ratio_sysctl_handler - just a wrapper around
 *	proc_dointvec() so that we can call setup_per_zone_lowmem_reserve()
 *	whenever sysctl_lowmem_reserve_ratio changes.
 *
 * The reserve ratio obviously has absolutely no relation with the
 * pages_min watermarks. The lowmem reserve ratio can only make sense
 * if in function of the boot time zone sizes.
 */
int lowmem_reserve_ratio_sysctl_handler(ctl_table *table, int write,
		 struct file *file, void __user *buffer, size_t *length, loff_t *ppos)
{
	proc_dointvec_minmax(table, write, file, buffer, length, ppos);
	setup_per_zone_lowmem_reserve();
	return 0;
}

__initdata int hashdist = HASHDIST_DEFAULT;

#ifdef CONFIG_NUMA
static int __init set_hashdist(char *str)
{
	if (!str)
		return 0;
	hashdist = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("hashdist=", set_hashdist);
#endif

/*
 * 从 bootmem 分配一个大的系统哈希表
 * -假设哈希表必须包含精确的 2 次幂数量的条目
 * - limit 是哈希桶的数量，而不是总分配大小
 */
void *__init  alloc_large_system_hash(const char *tablename,
				     unsigned long bucketsize,			// sizeof(struct hlist_head)
				     unsigned long numentries,			// 桶的数量，如1024
				     int scale,
				     int flags,			// 早期或考虑highmem
				     unsigned int *_hash_shift,
				     unsigned int *_hash_mask,
				     unsigned long limit)
{
	unsigned long long max = limit;
	unsigned long log2qty, size;
	void *table = NULL;

	/* 允许内核 cmdline 有发言权 */
	if (!numentries) {
		/* 将适用的内存大小四舍五入到最接近的兆字节 */
		numentries = (flags & HASH_HIGHMEM) ? nr_all_pages : nr_kernel_pages;
		numentries += (1UL << (20 - PAGE_SHIFT)) - 1;
		numentries >>= 20 - PAGE_SHIFT;
		numentries <<= 20 - PAGE_SHIFT;

		/* limit to 1 bucket per 2^scale bytes of low memory */
		if (scale > PAGE_SHIFT)
			numentries >>= (scale - PAGE_SHIFT);
		else
			numentries <<= (PAGE_SHIFT - scale);
	}
	/* 四舍五入到最接近的 2 的幂 */
	numentries = 1UL << (long_log2(numentries) + 1);

	/* 默认情况下将分配大小限制为 1/16 总内存 */
	if (max == 0) {
		max = ((unsigned long long)nr_all_pages << PAGE_SHIFT) >> 4;
		do_div(max, bucketsize);
	}

	if (numentries > max)
		numentries = max;

	log2qty = long_log2(numentries);

	do {
		size = bucketsize << log2qty;
		if (flags & HASH_EARLY)
			table = alloc_bootmem(size);
		else if (hashdist)
			table = __vmalloc(size, GFP_ATOMIC, PAGE_KERNEL);
		else {
			unsigned long order;
			for (order = 0; ((1UL << order) << PAGE_SHIFT) < size; order++)
				;
			table = (void*) __get_free_pages(GFP_ATOMIC, order);
		}
	} while (!table && size > PAGE_SIZE && --log2qty);

	if (!table)
		panic("Failed to allocate %s hash table\n", tablename);

	printk("%s hash table entries: %d (order: %d, %lu bytes)\n",
	       tablename,
	       (1U << log2qty),
	       long_log2(size) - PAGE_SHIFT,
	       size);

	if (_hash_shift)
		*_hash_shift = log2qty;
	if (_hash_mask)
		*_hash_mask = (1 << log2qty) - 1;

	return table;
}
