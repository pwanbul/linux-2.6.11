#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/cache.h>
#include <linux/threads.h>
#include <linux/numa.h>
#include <asm/atomic.h>

/* 空闲内存管理 - 分区伙伴分配器. */
#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define MAX_ORDER 11
#else
#define MAX_ORDER CONFIG_FORCE_MAX_ZONEORDER
#endif

/* 伙伴系统空闲链表 */
struct free_area {
	struct list_head	free_list;		// 头结点
	unsigned long		nr_free;		// 链表中的节点数
};

struct pglist_data;

/*
 * zone->lock and zone->lru_lock are two of the hottest locks in the kernel.
 * So add a wild amount of padding here to ensure that they fall into separate
 * cachelines.  There are very few zone structures in the machine, so space
 * consumption is not a concern here.
 */
#if defined(CONFIG_SMP)
struct zone_padding {
	char x[0];
} ____cacheline_maxaligned_in_smp;
#define ZONE_PADDING(name)	struct zone_padding name;
#else
#define ZONE_PADDING(name)
#endif

struct per_cpu_pages {
	int count;		/* 列表中的页数 */
	int low;		/* 低水位，需要补充 */
	int high;		/* 高水位，需要减少页框数 */
	int batch;		/* 伙伴系统 add/remove 的块大小 */
	struct list_head list;	/* 页面列表 */
};
// per cpu 页面高速缓存
struct per_cpu_pageset {
	struct per_cpu_pages pcp[2];	/* 0: hot.  1: cold */
#ifdef CONFIG_NUMA
	unsigned long numa_hit;		/* allocated in intended node */
	unsigned long numa_miss;	/* allocated in non intended node */
	unsigned long numa_foreign;	/* was intended here, hit elsewhere */
	unsigned long interleave_hit; 	/* interleaver prefered this zone */
	unsigned long local_node;	/* allocation from local node */
	unsigned long other_node;	/* allocation from other node */
#endif
} ____cacheline_aligned_in_smp;

// 区域索引，在zone_table
#define ZONE_DMA		0
#define ZONE_NORMAL		1
#define ZONE_HIGHMEM		2

#define MAX_NR_ZONES		3	/* 将此与 ZONES_SHIFT 同步 */
#define ZONES_SHIFT		2	/* ceil(log2(MAX_NR_ZONES)) 向上取整*/


/*
 * 当内存分配必须符合特定限制（例如适合 DMA）时，调用者将在 gfp_mask 中的区域修饰符位中将提示传递给分配器。
 * 这些位用于选择符合请求限制的优先级排序的内存区域列表。
 * GFP_ZONEMASK 定义 gfp_mask 中的哪些位应被视为区域修饰符。区域修饰符位的每个有效组合都有一个对应的区域列表（在 node_zonelists 中）。
 * 因此，对于两个区域修改器，将有最多 4 (2 ** 2) 个区域列表，对于 3 个修改器，将有 8 (2** 3) 个区域列表。
 * GFP_ZONETYPES 定义了“区域修饰符空间”中区域修饰符可能组合的数量。
 */
#define GFP_ZONEMASK	0x03
/*
 * 作为优化，任何仅在没有设置其他区域修饰符位（单独）时才有效的区域修饰符位
 * 应放置在该字段的最高位。 这使我们能够减少区域列表的范围，从而节省空间。
 * 例如，在三个区域修饰符位的情况下，我们可能需要多达八个区域列表。
 * 如果最左边的区域修饰符是“孤独者”，那么最高的有效区域列表将是四个，允许我们只分配五个区域列表。
 * 当最左边的位不是“孤独者”时使用第一种形式，否则使用第二种形式。
 */
/* #define GFP_ZONETYPES	(GFP_ZONEMASK + 1) */		/* Non-loner,4 */
#define GFP_ZONETYPES	((GFP_ZONEMASK + 1) / 2 + 1)		/* Loner,3 */

/*
 * 在需要它的机器上（例如 PC），我们将物理内存划分为多个物理区域。在 PC 上我们有 3 个区域：
 *
 * ZONE_DMA	  < 16 MB	ISA DMA capable memory
 * ZONE_NORMAL	16-896 MB	direct mapped by the kernel
 * ZONE_HIGHMEM	 > 896 MB	only page cache and user processes
 * 由于硬件的限制，不能对所有的页框一视同仁，需要把有相同特性的页框分区
 * 各个分区没有物理上区别，只是逻辑上的用途不同
 * 分配页框时不能跨越分区
 */

struct zone {       // 内存区
	/* 页面分配器通常访问的字段 */
	unsigned long		free_pages;			// 空闲页框的数量

	/* pages_min 保留的页框池的页框数量
	 * pages_low 空闲页框的低水位，设置为pages_min的5/4
	 * pages_high 空闲页框的高水位，设置为pages_min的3/2
	 * */
	unsigned long		pages_min, pages_low, pages_high;
	/*
	 * 我们不知道我们将要分配的内存是可释放的还是最终会被释放，
	 * 因此为了避免完全浪费几 GB 的内存，我们必须保留一些较低的区域内存
	 * （否则我们有运行 OOM 的风险尽管在较高区域有大量可用的公羊，但在较低区域）。
	 * 如果 sysctl_lowmem_reserve_ratio sysctl 更改，则在运行时重新计算此数组。
	 *
	 * 为防止高位zone过量分配内存。
	 */
	unsigned long		lowmem_reserve[MAX_NR_ZONES];

	struct per_cpu_pageset	pageset[NR_CPUS];       // per cpu 页面高速页框，处理热页和冷页

	/*
	 * 不同大小的自由区域
	 */
	spinlock_t		lock;
	struct free_area	free_area[MAX_ORDER];       // 空闲页框块，11


	ZONE_PADDING(_pad1_)

	/* 页面回收扫描器通常访问的字段 */
	spinlock_t		lru_lock;	// 活动页链表、非活动页链表使用的自旋锁
	struct list_head	active_list;        // 活动页链表
	struct list_head	inactive_list;      // 非活动页链表
	unsigned long		nr_scan_active;		// 回收内存时需要扫描的活动页数目
	unsigned long		nr_scan_inactive;	// 回收内存时需要扫描的非活动页数目
	unsigned long		nr_active;			// 管理区的活动链表上的页数目
	unsigned long		nr_inactive;		// 管理区的非活动链表上的页数目
	unsigned long		pages_scanned;		/* 自上次回收以来的扫描计数器，回收页框时置0 */
	int			all_unreclaimable; /* 在管理区中填满不可回收页时此标志被置位 */

	/*
	 * prev_priority 持有该区域的扫描优先级。
	 * 它被定义为我们在之前的 try_to_free_pages() 或 balance_pgdat()
	 * 调用中实现回收目标的扫描优先级。
	 *
	 * 我们使用 prev_priority 作为衡量页面回收压力程度的衡量标准
	 * - 它推动了 swappiness 决定：是否取消映射映射的页面。
	 *
	 * temp_priority 用于记住此区域成功重新填充为
	 * free_pages == pages_high 时的扫描优先级。
	 *
	 * 即使在单处理器上访问这两个字段也非常活跃。但预计平均还可以。
	 */
	int temp_priority;		// 临时管理区的优先级（回收页框时使用)
	int prev_priority;		// 管理区优先级，范围在12和0之间


	ZONE_PADDING(_pad2_)
	/* 很少使用或主要阅读的字段 */

	/*
	 * wait_table		-- 保存哈希表的数组
	 * wait_table_size	-- 哈希表数组的大小
	 * wait_table_bits	-- wait_table_size == (1 << wait_table_bits)
	 *
	 * 所有这些的目的是跟踪等待页面可用的人员，并在可能的情况下使它们再次运行。
	 * 麻烦的是，这会消耗大量空间，尤其是当在给定时间页面上等待的内容很少时。
	 * 因此，我们不使用每页等待队列，而是使用等待队列哈希表。
	 *
	 * 桶规则是在发生碰撞时在同一个队列上休眠，并在移除时唤醒该等待队列中的所有对象。
	 * 当某个东西被唤醒时，它必须检查以确保其页面真正可用，这是一个雷鸣般的群体。
	 * 碰撞的代价是巨大的，但考虑到表的预期负载，它们应该很少见，
	 * 以至于被节省的空间所带来的好处所抵消。
	 *
	 * mmfilemap.c 中的 __wait_on_page_locked() 和 unlock_page() 是这些字段的主要使用者，
	 * 而 mmpage_alloc.c 中的 free_area_init_core() 执行它们的初始化。
	 */
	wait_queue_head_t	* wait_table;		// 进程等待队列的散列表，这些进程正在等待管理区中的某页
	unsigned long		wait_table_size;		// 等待队列散列表的大小
	unsigned long		wait_table_bits;		// 等待队列散列表数组大小，值为2^order

	/*
	 * Discontig 内存支持字段。
	 */
	struct pglist_data	*zone_pgdat;		// 反向指针

	struct page		*zone_mem_map;		// 当前zone第一个页框的页框描述符的地址,同mem_map
	/* zone_start_pfn == zone_start_paddr >> PAGE_SHIFT */
	unsigned long		zone_start_pfn;		// 当前zone起始页框号,0

	unsigned long		spanned_pages;	/* 总页框数，包括hole */
	unsigned long		present_pages;	/* present页框数，不含hole */

	/*
	 * 很少使用的领域：
	 */
	char			*name;		// "DMA, NORMAL, HIGHMEM"
} ____cacheline_maxaligned_in_smp;


/*
 * The "priority" of VM scanning is how much of the queues we will scan in one
 * go. A value of 12 for DEF_PRIORITY implies that we will scan 1/4096th of the
 * queues ("queue_length >> 12") during an aging round.
 */
#define DEF_PRIORITY 12

/*
 * 一个分配请求对区域列表进行操作。 zonelist 是一个区域列表，
 * 第一个是分配的“目标”，其他区域是后备区域，优先级递减。
 *
 * 现在一个 zonelist 占用的内存比 cacheline 还少。
 * 除了启动之外我们从不修改它，并且只使用了几个索引，
 * 因此尽管 zonelist 表相对较大，但该构造的缓存占用空间非常小。
 */
struct zonelist {       // 备用列表
	struct zone *zones[MAX_NUMNODES * MAX_NR_ZONES + 1]; // NULL分隔
};


/*
 * pg_data_t 结构用于带有 CONFIG_DISCONTIGMEM 的机器（主要是 NUMA 机器？）
 * 来表示比 zone 表示的更高级别的内存区域。
 *
 * 在 NUMA 机器上，每个 NUMA 节点都有一个 pg_data_t 来描述它的内存布局。
 *
 * 内存统计信息和页面替换数据结构以每个区域为基础进行维护。
 */
struct bootmem_data;
// 内存结点(物理内存)通过该结构定义
typedef struct pglist_data {		// 页框列表数据，是不是这样命名的？？
	/*
	 * 0: ZONE_DMA
	 * 1: ZONE_NORMAL
	 * 2: ZONE_HIGHMEM
	 * */
	struct zone node_zones[MAX_NR_ZONES];      // 3种内存区域,最多3个，不足用0填充

	/* 后备区域列表 3，注意设置区域修改器时的查找顺序，见alloc_pages */
	struct zonelist node_zonelists[GFP_ZONETYPES];
	int nr_zones;       // 结点中不同内存域的数目

	struct page *node_mem_map;      // 指向page实例数组的指针，数组元素的指向页框的指针，用于描述结点的所有物理内存页，它包含了结点中所有内存域的页
	struct bootmem_data *bdata; // 指向自举内存分配器数据结构的实例
	unsigned long node_start_pfn;   // 该NUMA结点第一个页帧的逻辑编号

	/* node_present_pages指定了结点中页帧的数目，而node_spanned_pages则给出了该结点以页帧为单位计算的长度。
	 * 二者的值不一定相同，因为结点中可能有一些空洞，并不对应真正的页帧
	 * */
	unsigned long node_present_pages; /* 物理内存页的总数，单位为页框，不含洞 1M */
	unsigned long node_spanned_pages; /* 物理内存页的总数，单位为页框，包含洞在内 1M */

	int node_id;        // 是全局结点ID,系统中的NUMA结点都从0开始编号。

	struct pglist_data *pgdat_next; // 单向链表，连接到下一个内存结点，系统中所有结点都通过单链表连接起来，其末尾通过空指针标记

	wait_queue_head_t kswapd_wait;    // 页换出进程使用的等待队列
	struct task_struct *kswapd;     // 指向页换出进程的进程描述符
	int kswapd_max_order;       // kswapd将要创建的空闲块的大小取对数的值
} pg_data_t;

#define node_present_pages(nid)	(NODE_DATA(nid)->node_present_pages)
#define node_spanned_pages(nid)	(NODE_DATA(nid)->node_spanned_pages)

extern struct pglist_data *pgdat_list;

void __get_zone_counts(unsigned long *active, unsigned long *inactive,
			unsigned long *free, struct pglist_data *pgdat);
void get_zone_counts(unsigned long *active, unsigned long *inactive,
			unsigned long *free);
void build_all_zonelists(void);
void wakeup_kswapd(struct zone *zone, int order);
int zone_watermark_ok(struct zone *z, int order, unsigned long mark,
		int alloc_type, int can_try_harder, int gfp_high);

/*
 * zone_idx() 为 ZONE_DMA 区域返回 0，为 ZONE_NORMAL 区域返回 1，等等。
 */
#define zone_idx(zone)		((zone) - (zone)->zone_pgdat->node_zones)

/**
 * for_each_pgdat - helper macro to iterate over all nodes
 * @pgdat - pointer to a pg_data_t variable
 *
 * Meant to help with common loops of the form
 * pgdat = pgdat_list;
 * while(pgdat) {
 * 	...
 * 	pgdat = pgdat->pgdat_next;
 * }
 */
#define for_each_pgdat(pgdat) \
	for (pgdat = pgdat_list; pgdat; pgdat = pgdat->pgdat_next)

/*
 * next_zone - helper magic for for_each_zone()
 * Thanks to William Lee Irwin III for this piece of ingenuity.
 */
static inline struct zone *next_zone(struct zone *zone)
{
	pg_data_t *pgdat = zone->zone_pgdat;

	if (zone < pgdat->node_zones + MAX_NR_ZONES - 1)
		zone++;
	else if (pgdat->pgdat_next) {
		pgdat = pgdat->pgdat_next;
		zone = pgdat->node_zones;
	} else
		zone = NULL;

	return zone;
}

/**
 * for_each_zone - helper macro to iterate over all memory zones
 * @zone - pointer to struct zone variable
 *
 * The user only needs to declare the zone variable, for_each_zone
 * fills it in. This basically means for_each_zone() is an
 * easier to read version of this piece of code:
 *
 * for (pgdat = pgdat_list; pgdat; pgdat = pgdat->node_next)
 * 	for (i = 0; i < MAX_NR_ZONES; ++i) {
 * 		struct zone * z = pgdat->node_zones + i;
 * 		...
 * 	}
 * }
 */
#define for_each_zone(zone) \
	for (zone = pgdat_list->node_zones; zone; zone = next_zone(zone))

static inline int is_highmem_idx(int idx)
{
	return (idx == ZONE_HIGHMEM);
}

static inline int is_normal_idx(int idx)
{
	return (idx == ZONE_NORMAL);
}
/**
 * is_highmem - helper function to quickly check if a struct zone is a 
 *              highmem zone or not.  This is an attempt to keep references
 *              to ZONE_{DMA/NORMAL/HIGHMEM/etc} in general code to a minimum.
 * @zone - pointer to struct zone variable
 */
static inline int is_highmem(struct zone *zone)
{
	return zone == zone->zone_pgdat->node_zones + ZONE_HIGHMEM;
}

static inline int is_normal(struct zone *zone)
{
	return zone == zone->zone_pgdat->node_zones + ZONE_NORMAL;
}

/* These two functions are used to setup the per zone pages min values */
struct ctl_table;
struct file;
int min_free_kbytes_sysctl_handler(struct ctl_table *, int, struct file *, 
					void __user *, size_t *, loff_t *);
extern int sysctl_lowmem_reserve_ratio[MAX_NR_ZONES-1];
int lowmem_reserve_ratio_sysctl_handler(struct ctl_table *, int, struct file *,
					void __user *, size_t *, loff_t *);

#include <linux/topology.h>
/* Returns the number of the current Node. */
#define numa_node_id()		(cpu_to_node(_smp_processor_id()))

#ifndef CONFIG_DISCONTIGMEM

extern struct pglist_data contig_page_data;
#define NODE_DATA(nid)		(&contig_page_data)     // UMA中只有一个结点，即contig_page_data
#define NODE_MEM_MAP(nid)	mem_map
#define MAX_NODES_SHIFT		1
#define pfn_to_nid(pfn)		(0)

#else /* CONFIG_DISCONTIGMEM */

#include <asm/mmzone.h>

#if BITS_PER_LONG == 32 || defined(ARCH_HAS_ATOMIC_UNSIGNED)
/*
 * with 32 bit page->flags field, we reserve 8 bits for node/zone info.
 * there are 3 zones (2 bits) and this leaves 8-2=6 bits for nodes.
 */
#define MAX_NODES_SHIFT		6
#elif BITS_PER_LONG == 64
/*
 * with 64 bit flags field, there's plenty of room.
 */
#define MAX_NODES_SHIFT		10
#endif

#endif /* !CONFIG_DISCONTIGMEM */

#if NODES_SHIFT > MAX_NODES_SHIFT
#error NODES_SHIFT > MAX_NODES_SHIFT
#endif

/* There are currently 3 zones: DMA, Normal & Highmem, thus we need 2 bits */
#define MAX_ZONES_SHIFT		2

#if ZONES_SHIFT > MAX_ZONES_SHIFT
#error ZONES_SHIFT > MAX_ZONES_SHIFT
#endif

#endif /* !__ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _LINUX_MMZONE_H */
