#ifndef __LINUX_GFP_H
#define __LINUX_GFP_H

#include <linux/mmzone.h>
#include <linux/stddef.h>
#include <linux/linkage.h>
#include <linux/config.h>

struct vm_area_struct;

/*
 * GFP bitmasks..
 * get free page
 *
 */
/* Zone modifiers in GFP_ZONEMASK (see linux/mmzone.h - low two bits) */
/* 区域修饰符，ZONE_NORMAL为默认区域
 * 如果指定__GFP_DMA，则只能在dma中分配
 * 如果指定__GFP_HIGHMEM，按highmem，normal，mda的顺序找
 * 如果指定没有指定，按normal，mda的顺序找
 * */
#define __GFP_DMA	0x01   // 所请求的页框必须处于ZONE_DMA管理区。等价于GFP_DMA
#define __GFP_HIGHMEM	0x02   // 所请求的页框处于ZONE_HIGHMEM管理区

/*
 * Action modifiers - doesn't change the zoning
 *
 * __GFP_REPEAT: Try hard to allocate the memory, but the allocation attempt
 * _might_ fail.  This depends upon the particular VM implementation.
 *
 * __GFP_NOFAIL: The VM implementation _must_ retry infinitely: the caller
 * cannot handle allocation failures.
 *
 * __GFP_NORETRY: The VM implementation must not retry indefinitely.
 */
// 行为修饰符
#define __GFP_WAIT	0x10	/* 允许内核对等待空闲页框的当前进程进行阻塞 */
#define __GFP_HIGH	0x20	/* 允许内核访问保留的页框池 */
#define __GFP_IO	0x40	/* 允许内核在低端内存页上执行I/O传输以释放页框 */
#define __GFP_FS	0x80	/* 如果清0，则不允许内核执行依赖于文件系统的操作 */
#define __GFP_COLD	0x100	/* 所请求的页框可能为“冷的”（参见稍后的“每CPU页框高速缓存”一节)  */
#define __GFP_NOWARN	0x200	/* 一次内存分配失败将不会产生警告 */
#define __GFP_REPEAT	0x400	/* 信息内核重试内存分配直到成功 */
#define __GFP_NOFAIL	0x800	/* 与__GFP_REPEAT相同 */
#define __GFP_NORETRY	0x1000	/* 一次内存分配失败后不再重试 */
#define __GFP_NO_GROW	0x2000	/* slab分配器不允许增大slab高速缓存（参见稍后的“slab分配器”一节) */
#define __GFP_COMP	0x4000	/*  属于扩展页的页框（参见第二章的“扩展分页”一节） */
#define __GFP_ZERO	0x8000	/* 任何返回的页框必须被填满0 */

#define __GFP_BITS_SHIFT 16	/* Room for 16 __GFP_FOO bits */
#define __GFP_BITS_MASK ((1 << __GFP_BITS_SHIFT) - 1)

/* if you forget to add the bitmask here kernel will crash, period */
#define GFP_LEVEL_MASK (__GFP_WAIT|__GFP_HIGH|__GFP_IO|__GFP_FS| \
			__GFP_COLD|__GFP_NOWARN|__GFP_REPEAT| \
			__GFP_NOFAIL|__GFP_NORETRY|__GFP_NO_GROW|__GFP_COMP)
// 常用配置
#define GFP_ATOMIC	(__GFP_HIGH)
#define GFP_NOIO	(__GFP_WAIT)
#define GFP_NOFS	(__GFP_WAIT | __GFP_IO)
#define GFP_KERNEL	(__GFP_WAIT | __GFP_IO | __GFP_FS)
#define GFP_USER	(__GFP_WAIT | __GFP_IO | __GFP_FS)
#define GFP_HIGHUSER	(__GFP_WAIT | __GFP_IO | __GFP_FS | __GFP_HIGHMEM)

/* Flag - indicates that the buffer will be suitable for DMA.  Ignored on some
   platforms, used as appropriate on others */

#define GFP_DMA		__GFP_DMA


/*
 * There is only one page-allocator function, and two main namespaces to
 * it. The alloc_page*() variants return 'struct page *' and as such
 * can allocate highmem pages, the *get*page*() variants return
 * virtual kernel addresses to the allocated page(s).
 */

/*
 * We get the zone list from the current node and the gfp_mask.
 * This zone list contains a maximum of MAXNODES*MAX_NR_ZONES zones.
 *
 * For the normal case of non-DISCONTIGMEM systems the NODE_DATA() gets
 * optimized to &contig_page_data at compile-time.
 */

#ifndef HAVE_ARCH_FREE_PAGE
static inline void arch_free_page(struct page *page, int order) { }
#endif

extern struct page *
FASTCALL(__alloc_pages(unsigned int, unsigned int, struct zonelist *));

static inline struct page *alloc_pages_node(int nid, unsigned int gfp_mask,
						unsigned int order)
{
	if (unlikely(order >= MAX_ORDER))		// 分配阶最多10，即1024个连续页框
		return NULL;
	/* gfp_mask中zone modifiers可以指定zone
	 * __GFP_DMA	0x01
	 * __GFP_HIGHMEM	0x02
	 * GFP_ZONEMASK为0x03
	 * */
	return __alloc_pages(gfp_mask, order, NODE_DATA(nid)->node_zonelists + (gfp_mask & GFP_ZONEMASK));
}

#ifdef CONFIG_NUMA      // 配置NUMA
extern struct page *alloc_pages_current(unsigned gfp_mask, unsigned order);

static inline struct page *alloc_pages(unsigned int gfp_mask, unsigned int order)
{
	if (unlikely(order >= MAX_ORDER))
		return NULL;

	return alloc_pages_current(gfp_mask, order);
}
extern struct page *alloc_page_vma(unsigned gfp_mask,
			struct vm_area_struct *vma, unsigned long addr);
#else   // !NUMA
// 分配连续地1<<order个页框，返回第一个页框描述符的地址
#define alloc_pages(gfp_mask, order) \
		alloc_pages_node(numa_node_id(), gfp_mask, order)
#define alloc_page_vma(gfp_mask, vma, addr) alloc_pages(gfp_mask, 0)
#endif
#define alloc_page(gfp_mask) alloc_pages(gfp_mask, 0)		// 分配一个页框

extern unsigned long FASTCALL(__get_free_pages(unsigned int gfp_mask, unsigned int order));
extern unsigned long FASTCALL(get_zeroed_page(unsigned int gfp_mask));

#define __get_free_page(gfp_mask) \     // 分配一个页框,返回一个页框的虚拟地址
		__get_free_pages((gfp_mask),0)

#define __get_dma_pages(gfp_mask, order) \
		__get_free_pages((gfp_mask) | GFP_DMA,(order))

extern void FASTCALL(__free_pages(struct page *page, unsigned int order));
extern void FASTCALL(free_pages(unsigned long addr, unsigned int order));
extern void FASTCALL(free_hot_page(struct page *page));
extern void FASTCALL(free_cold_page(struct page *page));

#define __free_page(page) __free_pages((page), 0)
#define free_page(addr) free_pages((addr),0)

void page_alloc_init(void);

#endif /* __LINUX_GFP_H */
