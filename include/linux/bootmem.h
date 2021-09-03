/*
 * Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 */
#ifndef _LINUX_BOOTMEM_H
#define _LINUX_BOOTMEM_H

#include <asm/pgtable.h>
#include <asm/dma.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/mmzone.h>

/*
 *  simple boot-time physical memory area allocator.
 */

extern unsigned long max_low_pfn;
extern unsigned long min_low_pfn;

/*
 * highest page
 */
extern unsigned long max_pfn;

/*
 * node_bootmem_map 是一个映射指针——这些位代表节点上的所有物理内存页（包括空洞）。
 *
 * 在启动过程期间，尽管内存管理尚未初始化，但内核仍然需要分配内存以创建各种数据结构。
 * bootmem分配器用于在启动阶段早期分配内存。显然，对该分配器的需求集中于简单性方面，
 * 而不是性能和通用性。因此内核开发者决定实现一个最先适配（first-fit）分配器
 * 用于在启动阶段管理内存，这是可能想到的最简单方式。
 *
 * 该分配器使用一个位图来管理页，位图比特位的数目与系统中物理内存页的数目相同。
 * 比特位为1，表示已用页；比特位为0，表示空闲页。在需要分配内存时，分配器逐位
 * 扫描位图，直至找到一个能提供足够连续页的位置，即所谓的最先最佳（first-best）或
 * 最先适配位置。
 *
 * 该过程不是很高效，因为每次分配都必须从头扫描比特链。因此在内核完全初始化之后，不能将
 * 该分配器用于内存管理。伙伴系统（连同slab、slub或slob分配器）是一个好得多的备选方案。
 *
 * 即使最先适配分配器也必须管理一些数据。内核（为系统中的每个结点都）提供了一个
 * bootmem_data结构的实例，用于该用途。当然，该结构所需的内存无法动态分配，
 * 必须在编译时分配给内核。在UMA系统上该分配的实现与CPU无关（NUMA系统
 * 采用了特定于体系结构的解决方案）。
 *
 * 内存不连续的系统可能需要多个bootmem分配器。一个典型的例子是NUMA计算机，其中每个
 * 结点注册了一个bootmem分配器，但如果物理地址空间中散布着空洞，也可以为每个连续内存
 * 区注册一个bootmem分配器。
 *
 * 注册新的自举分配器可使用init_bootmem_core，所有注册的分配器保存在一个链表中，
 * 表头是全局变量bdata_list。
 */
typedef struct bootmem_data {
    /* node_boot_start保存了系统中第一个页的物理地址
     * 第一个页框的起始地址,应当为物理地址0x0000 0000
     * */
	unsigned long node_boot_start;
	/* node_low_pfn是可以直接管理的物理地址空间中最后一页的编号。换句话说，即ZONE_NORMAL的结束页。
	 * 应当为页框号0x38000
	 * */
	unsigned long node_low_pfn;
	/* 位图的起始位置的线性地址
	 * node_bootmem_map是指向存储分配位图的内存区的指针。在IA-32系统上，
	 * 用于该用途的内存区紧接着内核映像之后。对应的地址保存在_end变量后，
	 * 该变量在链接期间自动地插入到内核映像中。
	 * */
	void *node_bootmem_map;
	/* last_pos是上一次分配的页的编号。如果没有请求分配整个页，则last_offset用作该页
	 * 内部的偏移量。这使得bootmem分配器可以分配小于一整页的内存区（伙伴系统无法做到这一点）。
	 * */
	unsigned long last_offset;
	unsigned long last_pos;
	/* last_success指定位图中上一次成功分配内存的位置，新的分配将由此开始。
	 * 尽管这使得最先适配算法稍快了一点，但仍然无法真正代替更复杂的技术。
	 * */
	unsigned long last_success;	/* 上一个分配点，加快搜索速度 */
} bootmem_data_t;

extern unsigned long __init bootmem_bootmap_pages (unsigned long);
extern unsigned long __init init_bootmem (unsigned long addr, unsigned long memend);
extern void __init free_bootmem (unsigned long addr, unsigned long size);
extern void * __init __alloc_bootmem (unsigned long size, unsigned long align, unsigned long goal);
#ifndef CONFIG_HAVE_ARCH_BOOTMEM_NODE
extern void __init reserve_bootmem (unsigned long addr, unsigned long size);
// 各种分配内存接口
#define alloc_bootmem(x) \
	__alloc_bootmem((x), SMP_CACHE_BYTES, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low(x) \
	__alloc_bootmem((x), SMP_CACHE_BYTES, 0)
#define alloc_bootmem_pages(x) \
	__alloc_bootmem((x), PAGE_SIZE, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low_pages(x) \
	__alloc_bootmem((x), PAGE_SIZE, 0)
#endif /* !CONFIG_HAVE_ARCH_BOOTMEM_NODE */
extern unsigned long __init free_all_bootmem (void);

extern unsigned long __init init_bootmem_node (pg_data_t *pgdat, unsigned long freepfn, unsigned long startpfn, unsigned long endpfn);
extern void __init reserve_bootmem_node (pg_data_t *pgdat, unsigned long physaddr, unsigned long size);
extern void __init free_bootmem_node (pg_data_t *pgdat, unsigned long addr, unsigned long size);
extern unsigned long __init free_all_bootmem_node (pg_data_t *pgdat);
extern void * __init __alloc_bootmem_node (pg_data_t *pgdat, unsigned long size, unsigned long align, unsigned long goal);
#ifndef CONFIG_HAVE_ARCH_BOOTMEM_NODE
#define alloc_bootmem_node(pgdat, x) \
	__alloc_bootmem_node((pgdat), (x), SMP_CACHE_BYTES, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_pages_node(pgdat, x) \
	__alloc_bootmem_node((pgdat), (x), PAGE_SIZE, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low_pages_node(pgdat, x) \
	__alloc_bootmem_node((pgdat), (x), PAGE_SIZE, 0)
#endif /* !CONFIG_HAVE_ARCH_BOOTMEM_NODE */

extern unsigned long __initdata nr_kernel_pages;
extern unsigned long __initdata nr_all_pages;

extern void *__init alloc_large_system_hash(const char *tablename,
					    unsigned long bucketsize,
					    unsigned long numentries,
					    int scale,
					    int flags,
					    unsigned int *_hash_shift,
					    unsigned int *_hash_mask,
					    unsigned long limit);

#define HASH_HIGHMEM	0x00000001	/* Consider highmem? */
#define HASH_EARLY	0x00000002	/* 在早期启动期间分配？ */

/* Only NUMA needs hash distribution.
 * IA64 is known to have sufficient vmalloc space.
 */
#if defined(CONFIG_NUMA) && defined(CONFIG_IA64)
#define HASHDIST_DEFAULT 1
#else
#define HASHDIST_DEFAULT 0
#endif
extern int __initdata hashdist;		/* Distribute hashes across NUMA nodes? */


#endif /* _LINUX_BOOTMEM_H */
