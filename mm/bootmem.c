/*
 *  linux/mm/bootmem.c
 *
 *  Copyright (C) 1999 Ingo Molnar
 *  Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 *
 *  simple boot-time physical memory area allocator and
 *  free memory collector. It's used to deal with reserved
 *  system memory and memory holes as well.
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <asm/dma.h>
#include <asm/io.h>
#include "internal.h"

/*
 * Access to this subsystem has to be serialized externally. (this is
 * true for the boot process anyway)
 */
unsigned long max_low_pfn;
unsigned long min_low_pfn;
unsigned long max_pfn;

EXPORT_SYMBOL(max_pfn);		/* This is exported so
				 * dma_get_required_mask(), which uses
				 * it, can be an inline function */

/* return the number of _pages_ that will be allocated for the boot bitmap */
unsigned long __init bootmem_bootmap_pages (unsigned long pages)
{
	unsigned long mapsize;

	mapsize = (pages+7)/8;
	mapsize = (mapsize + ~PAGE_MASK) & PAGE_MASK;
	mapsize >>= PAGE_SHIFT;

	return mapsize;
}

/*
 * 调用一次以设置分配器本身.
 *
 * init_bootmem_core的目的在于执行bootmem分配器的第一个初始化步骤。
 * 先前检测到的低端内存页帧的范围输入到相应的bootmem_data_t实例中，
 * 这里是contig_bootmem_data。最初在位图contig_bootmem_data->node_bootmem_map中，
 * 所有的页都标记为已用。由于init_bootmem_core是一个体系结构无关的函数，
 * 它尚无法知道哪些页可用，哪些页不能使用。因为体系结构方面的原因，有些页需要特殊的处理，
 * 例如IA-32系统上的0页。有些页则已经使用，例如内核映像占用的页。
 * 实际可用的页必须由体系结构相关的代码显式标记出来。
 */
static unsigned long __init init_bootmem_core (pg_data_t *pgdat,
	unsigned long mapstart, unsigned long start, unsigned long end)
{
	bootmem_data_t *bdata = pgdat->bdata;       // pgdat->bdata为&contig_bootmem_data
	unsigned long mapsize = ((end - start)+7)/8;    // mapsize为要创建的位图的大小,单位字节，每位对应一个页框

    // pgdat_list为pg_data_t的头结点
	pgdat->pgdat_next = pgdat_list;
	pgdat_list = pgdat;

	mapsize = (mapsize + (sizeof(long) - 1UL)) & ~(sizeof(long) - 1UL);   // 向上4字节对齐，结果28672
	bdata->node_bootmem_map = phys_to_virt(mapstart << PAGE_SHIFT); // node_bootmem_map为位图的起始位置的线性地址
	bdata->node_boot_start = (start << PAGE_SHIFT);
	bdata->node_low_pfn = end;

	/*
	 * 最初所有页面都是保留的 - setup_arch() 必须明确注册空闲 RAM 区域。
	 * 把位图中的每一位都置1，表示为占用状态。在接下去的函数中，会把内核可以使用的页框号对应的位置0
	 */
	memset(bdata->node_bootmem_map, 0xff, mapsize);

	return mapsize;
}

/*
 * Marks a particular physical memory range as unallocatable. Usable RAM
 * might be used for boot-time allocations - or it might get added
 * to the free page pool later on.
 */
static void __init reserve_bootmem_core(bootmem_data_t *bdata, unsigned long addr, unsigned long size)
{
	unsigned long i;
	/*
	 * round up, partially reserved pages are considered
	 * fully reserved.
	 */
	unsigned long sidx = (addr - bdata->node_boot_start)/PAGE_SIZE;
	unsigned long eidx = (addr + size - bdata->node_boot_start + 
							PAGE_SIZE-1)/PAGE_SIZE;
	unsigned long end = (addr + size + PAGE_SIZE-1)/PAGE_SIZE;

	BUG_ON(!size);
	BUG_ON(sidx >= eidx);
	BUG_ON((addr >> PAGE_SHIFT) >= bdata->node_low_pfn);
	BUG_ON(end > bdata->node_low_pfn);

	for (i = sidx; i < eidx; i++)
		if (test_and_set_bit(i, bdata->node_bootmem_map)) {
#ifdef CONFIG_DEBUG_BOOTMEM
			printk("hm, page %08lx reserved twice.\n", i*PAGE_SIZE);
#endif
		}
}

// 释放内存，addr为起始物理地址，size为大小
static void __init free_bootmem_core(bootmem_data_t *bdata, unsigned long addr, unsigned long size)
{
	unsigned long i;
	unsigned long start;
	/*
	 * 四舍五入可用内存的末尾，部分空闲页面被认为是保留的。
	 */
	unsigned long sidx;
	unsigned long eidx = (addr + size - bdata->node_boot_start)/PAGE_SIZE;  // 可用的页面数
	unsigned long end = (addr + size)/PAGE_SIZE;        // 总的页面数

	BUG_ON(!size);
	BUG_ON(end > bdata->node_low_pfn);

	if (addr < bdata->last_success)
		bdata->last_success = addr;

	/*
	 * 将地址的开头四舍五入。
	 */
	start = (addr + PAGE_SIZE-1) / PAGE_SIZE;
	sidx = start - (bdata->node_boot_start/PAGE_SIZE);

	for (i = sidx; i < eidx; i++) {
		if (unlikely(!test_and_clear_bit(i, bdata->node_bootmem_map)))
			BUG();
	}
}

/*
 * 我们“合并”后续分配以节省空间。如果由于存在物理RAM空间碎片
 * 的盒子的大小限制而无法满足分配，我们可能会“丢失”页面的
 * 一部分——在这些情况下（主要是大内存盒子），这不是问题。
 *
 * 在低内存盒上，我们在 100% 的情况下都能做到。
 *
 * alignment必须是2的幂值。
 *
 * 注意：此函数_不可_重入。
 */
static void * __init
__alloc_bootmem_core(struct bootmem_data *bdata, unsigned long size,
		unsigned long align, unsigned long goal)
{
	unsigned long offset, remaining_size, areasize, preferred;
	unsigned long i, start = 0, incr, eidx;
	void *ret;

	if(!size) {
		printk("__alloc_bootmem_core(): zero-sized request\n");
		BUG();
	}
	BUG_ON(align & (align-1));

	eidx = bdata->node_low_pfn - (bdata->node_boot_start >> PAGE_SHIFT);
	offset = 0;
	if (align && (bdata->node_boot_start & (align - 1UL)) != 0)
		offset = (align - (bdata->node_boot_start & (align - 1UL)));
	offset >>= PAGE_SHIFT;

	/*
	 * We try to allocate bootmem pages above 'goal'
	 * first, then we try to allocate lower pages.
	 */
	if (goal && (goal >= bdata->node_boot_start) && 
	    ((goal >> PAGE_SHIFT) < bdata->node_low_pfn)) {
		preferred = goal - bdata->node_boot_start;

		if (bdata->last_success >= preferred)
			preferred = bdata->last_success;
	} else
		preferred = 0;

	preferred = ((preferred + align - 1) & ~(align - 1)) >> PAGE_SHIFT;
	preferred += offset;
	areasize = (size+PAGE_SIZE-1)/PAGE_SIZE;
	incr = align >> PAGE_SHIFT ? : 1;

restart_scan:
	for (i = preferred; i < eidx; i += incr) {
		unsigned long j;
		i = find_next_zero_bit(bdata->node_bootmem_map, eidx, i);
		i = ALIGN(i, incr);
		if (test_bit(i, bdata->node_bootmem_map))
			continue;
		for (j = i + 1; j < i + areasize; ++j) {
			if (j >= eidx)
				goto fail_block;
			if (test_bit (j, bdata->node_bootmem_map))
				goto fail_block;
		}
		start = i;
		goto found;
	fail_block:
		i = ALIGN(j, incr);
	}

	if (preferred > offset) {
		preferred = offset;
		goto restart_scan;
	}
	return NULL;

found:
	bdata->last_success = start << PAGE_SHIFT;
	BUG_ON(start >= eidx);

	/*
	 * Is the next page of the previous allocation-end the start
	 * of this allocation's buffer? If yes then we can 'merge'
	 * the previous partial page with this allocation.
	 */
	if (align < PAGE_SIZE &&
	    bdata->last_offset && bdata->last_pos+1 == start) {
		offset = (bdata->last_offset+align-1) & ~(align-1);
		BUG_ON(offset > PAGE_SIZE);
		remaining_size = PAGE_SIZE-offset;
		if (size < remaining_size) {
			areasize = 0;
			/* last_pos unchanged */
			bdata->last_offset = offset+size;
			ret = phys_to_virt(bdata->last_pos*PAGE_SIZE + offset +
						bdata->node_boot_start);
		} else {
			remaining_size = size - remaining_size;
			areasize = (remaining_size+PAGE_SIZE-1)/PAGE_SIZE;
			ret = phys_to_virt(bdata->last_pos*PAGE_SIZE + offset +
						bdata->node_boot_start);
			bdata->last_pos = start+areasize-1;
			bdata->last_offset = remaining_size;
		}
		bdata->last_offset &= ~PAGE_MASK;
	} else {
		bdata->last_pos = start + areasize - 1;
		bdata->last_offset = size & ~PAGE_MASK;
		ret = phys_to_virt(start * PAGE_SIZE + bdata->node_boot_start);
	}

	/*
	 * Reserve the area now:
	 */
	for (i = start; i < start+areasize; i++)
		if (unlikely(test_and_set_bit(i, bdata->node_bootmem_map)))
			BUG();
	memset(ret, 0, size);
	return ret;
}
/* free_all_bootmem释放的不是已经申请的内存，
 * 而是bootmem没分配出去的内存，
 * 调用free_all_bootmem以后
 * bootmem就把自身也释放掉了，不可用了，
 * 之后会用更高级的内存管理系统。
 * */
static unsigned long __init free_all_bootmem_core(pg_data_t *pgdat)
{
	struct page *page;
	bootmem_data_t *bdata = pgdat->bdata;
	unsigned long i, count, total = 0;
	unsigned long idx;
	unsigned long *map; 
	int gofast = 0;

	BUG_ON(!bdata->node_bootmem_map);

	count = 0;
	/* first extant page of the node */
	page = virt_to_page(phys_to_virt(bdata->node_boot_start));
	idx = bdata->node_low_pfn - (bdata->node_boot_start >> PAGE_SHIFT);
	map = bdata->node_bootmem_map;
	/* Check physaddr is O(LOG2(BITS_PER_LONG)) page aligned */
	if (bdata->node_boot_start == 0 ||
	    ffs(bdata->node_boot_start) - PAGE_SHIFT > ffs(BITS_PER_LONG))
		gofast = 1;
	for (i = 0; i < idx; ) {
		unsigned long v = ~map[i / BITS_PER_LONG];
		if (gofast && v == ~0UL) {
			int j, order;

			count += BITS_PER_LONG;
			__ClearPageReserved(page);
			order = ffs(BITS_PER_LONG) - 1;
			set_page_refs(page, order);
			for (j = 1; j < BITS_PER_LONG; j++) {
				if (j + 16 < BITS_PER_LONG)
					prefetchw(page + j + 16);
				__ClearPageReserved(page + j);
			}
			__free_pages(page, order);
			i += BITS_PER_LONG;
			page += BITS_PER_LONG;
		} else if (v) {
			unsigned long m;
			for (m = 1; m && i < idx; m<<=1, page++, i++) {
				if (v & m) {
					count++;
					__ClearPageReserved(page);
					set_page_refs(page, 0);
					__free_page(page);
				}
			}
		} else {
			i+=BITS_PER_LONG;
			page += BITS_PER_LONG;
		}
	}
	total += count;

	/*
	 * Now free the allocator bitmap itself, it's not
	 * needed anymore:
	 */
	page = virt_to_page(bdata->node_bootmem_map);
	count = 0;
	for (i = 0; i < ((bdata->node_low_pfn-(bdata->node_boot_start >> PAGE_SHIFT))/8 + PAGE_SIZE-1)/PAGE_SIZE; i++,page++) {
		count++;
		__ClearPageReserved(page);
		set_page_count(page, 1);
		__free_page(page);
	}
	total += count;
	bdata->node_bootmem_map = NULL;

	return total;
}

// 非连续内存
unsigned long __init init_bootmem_node (pg_data_t *pgdat, unsigned long freepfn, unsigned long startpfn, unsigned long endpfn)
{
	return(init_bootmem_core(pgdat, freepfn, startpfn, endpfn));
}

void __init reserve_bootmem_node (pg_data_t *pgdat, unsigned long physaddr, unsigned long size)
{
	reserve_bootmem_core(pgdat->bdata, physaddr, size);
}

void __init free_bootmem_node (pg_data_t *pgdat, unsigned long physaddr, unsigned long size)
{
	free_bootmem_core(pgdat->bdata, physaddr, size);
}

unsigned long __init free_all_bootmem_node (pg_data_t *pgdat)
{
	return(free_all_bootmem_core(pgdat));
}

// 连续内存
unsigned long __init init_bootmem (unsigned long start, unsigned long pages)
{
	max_low_pfn = pages;    // 结束页框，896MB
	min_low_pfn = start;    // 起始页框，pg1后面
	return(init_bootmem_core(NODE_DATA(0), start, 0, pages));       // NODE_DATA(0):contig_page_data
}

#ifndef CONFIG_HAVE_ARCH_BOOTMEM_NODE
void __init reserve_bootmem (unsigned long addr, unsigned long size)
{
	reserve_bootmem_core(NODE_DATA(0)->bdata, addr, size);
}
#endif /* !CONFIG_HAVE_ARCH_BOOTMEM_NODE */

// 释放内存页面
void __init free_bootmem (unsigned long addr, unsigned long size)
{
	free_bootmem_core(NODE_DATA(0)->bdata, addr, size);
}

unsigned long __init free_all_bootmem (void)
{
	return(free_all_bootmem_core(NODE_DATA(0)));
}

// 分配内存，size为大小，align为对齐要求，goal为分配内存的区域
void * __init __alloc_bootmem (unsigned long size, unsigned long align, unsigned long goal)
{
	pg_data_t *pgdat = pgdat_list;		// 配置为连续内存时，链表中只有一个元素
	void *ptr;

	for_each_pgdat(pgdat)
		if ((ptr = __alloc_bootmem_core(pgdat->bdata, size,
						align, goal)))
			return(ptr);

	/*
	 * Whoops, we cannot satisfy the allocation request.
	 */
	printk(KERN_ALERT "bootmem alloc of %lu bytes failed!\n", size);
	panic("Out of memory");
	return NULL;
}

/* __alloc_bootmem_node -
 * @pgdat 内存节点
 * @size 页框跨度数+1
 * @align 对齐
 * @goal
 * */
void * __init __alloc_bootmem_node (pg_data_t *pgdat, unsigned long size, unsigned long align, unsigned long goal)
{
	void *ptr;

	ptr = __alloc_bootmem_core(pgdat->bdata, size, align, goal);
	if (ptr)
		return (ptr);

	return __alloc_bootmem(size, align, goal);
}

