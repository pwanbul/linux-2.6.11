#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <linux/sched.h>
#include <linux/errno.h>

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/mmzone.h>
#include <linux/rbtree.h>
#include <linux/prio_tree.h>
#include <linux/fs.h>

struct mempolicy;
struct anon_vma;

#ifndef CONFIG_DISCONTIGMEM          /* Don't use mapnrs, do it properly */
extern unsigned long max_mapnr;
#endif

extern unsigned long num_physpages;
extern void * high_memory;
extern unsigned long vmalloc_earlyreserve;
extern int page_cluster;

#ifdef CONFIG_SYSCTL
extern int sysctl_legacy_va_layout;
#else
#define sysctl_legacy_va_layout 0
#endif

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/atomic.h>

#ifndef MM_VM_SIZE
#define MM_VM_SIZE(mm)	((TASK_SIZE + PGDIR_SIZE - 1) & PGDIR_MASK)
#endif

#define nth_page(page,n) pfn_to_page(page_to_pfn((page)) + (n))

/*
 * Linux kernel virtual memory manager primitives.
 * The idea being to have a "virtual" mm in the same way
 * we have a virtual fs - giving a cleaner interface to the
 * mm details, and allowing different kinds of memory mappings
 * (from shared memory to executable loading to arbitrary
 * mmap() functions).
 */

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
struct vm_area_struct {
	struct mm_struct * vm_mm;	/* 反向指针 The address space we belong to. */
	unsigned long vm_start;		/* VMA起始地址 Our start address within vm_mm. */
	unsigned long vm_end;		/* VMA结束地址的后一个字节 The first byte after our end address
					   within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next;	// VMA链表指针域，按地址大小排序

	pgprot_t vm_page_prot;		/* VMA的访问权限 Access permissions of this VMA. */
	unsigned long vm_flags;		/* Flags, listed below. */

	struct rb_node vm_rb;		

	/*
	 * For areas with an address space and backing store,
	 * linkage into the address_space->i_mmap prio tree, or
	 * linkage to the list of like vmas hanging off its node, or
	 * linkage of vma in the address_space->i_mmap_nonlinear list.
	 * 优先树
	 */
	union {
		struct {
			struct list_head list;
			void *parent;	/* aligns with prio_tree_node parent */
			struct vm_area_struct *head;
		} vm_set;

		struct raw_prio_tree_node prio_tree_node;
	} shared;

	/*
	 * 在文件页面之一的COW之后，文件的MAP_PRIVATE vma可以在i_mmap树和anon_vma列表中。
	 * MAP_SHARED vma只能在i_mmap树中。匿名MAP_PRIVATE、堆栈或 brk vma（带有 NULL 文件）
	 * 只能在 anon_vma 列表中。
	 */
	struct list_head anon_vma_node;	/* FPRA反向映射：链入anon_vma中的链表 */
	struct anon_vma *anon_vma;	/* FPRA反向映射：指向anon_vma */

	/* Function pointers to deal with this struct. */
	struct vm_operations_struct * vm_ops;	// 处理此结构的函数指针

	/* 关于我们后备存储的信息： */
	unsigned long vm_pgoff;		/* 以 PAGE_SIZE 为单位的偏移量（在 vm_file 内），而不是 PAGE_CACHE_SIZE */
	struct file * vm_file;		/* File we map to (can be NULL). */
	void * vm_private_data;		/* was vm_pte (shared mem) */
	unsigned long vm_truncate_count;/* truncate_count or restart_addr */

#ifndef CONFIG_MMU
	atomic_t vm_usage;		/* refcount (VMAs shared if !MMU) */
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *vm_policy;	/* NUMA policy for the VMA */
#endif
};

/*
 * This struct defines the per-mm list of VMAs for uClinux. If CONFIG_MMU is
 * disabled, then there's a single shared list of VMAs maintained by the
 * system, and mm's subscribe to these individually
 */
struct vm_list_struct {
	struct vm_list_struct	*next;
	struct vm_area_struct	*vma;
};

#ifndef CONFIG_MMU
extern struct rb_root nommu_vma_tree;
extern struct rw_semaphore nommu_vma_sem;

extern unsigned int kobjsize(const void *objp);
#endif

/*
 * vm_flags..
 */
#define VM_READ		0x00000001	/* currently active flags */
#define VM_WRITE	0x00000002
#define VM_EXEC		0x00000004
#define VM_SHARED	0x00000008

#define VM_MAYREAD	0x00000010	/* limits for mprotect() etc */
#define VM_MAYWRITE	0x00000020
#define VM_MAYEXEC	0x00000040
#define VM_MAYSHARE	0x00000080

#define VM_GROWSDOWN	0x00000100	/* general info on the segment */
#define VM_GROWSUP	0x00000200
#define VM_SHM		0x00000400	/* shared memory area, don't swap out */
#define VM_DENYWRITE	0x00000800	/* ETXTBSY on write attempts.. */

#define VM_EXECUTABLE	0x00001000
#define VM_LOCKED	0x00002000
#define VM_IO           0x00004000	/* Memory mapped I/O or similar */

					/* Used by sys_madvise() */
#define VM_SEQ_READ	0x00008000	/* App will access data sequentially */
#define VM_RAND_READ	0x00010000	/* App will not benefit from clustered reads */

#define VM_DONTCOPY	0x00020000      /* Do not copy this vma on fork */
#define VM_DONTEXPAND	0x00040000	/* Cannot expand with mremap() */
#define VM_RESERVED	0x00080000	/* Don't unmap it from swap_out */
#define VM_ACCOUNT	0x00100000	/* Is a VM accounted object */
#define VM_HUGETLB	0x00400000	/* Huge TLB Page VM */
#define VM_NONLINEAR	0x00800000	/* Is non-linear (remap_file_pages) */

#ifndef VM_STACK_DEFAULT_FLAGS		/* arch can override this */
#define VM_STACK_DEFAULT_FLAGS VM_DATA_DEFAULT_FLAGS
#endif

#ifdef CONFIG_STACK_GROWSUP
#define VM_STACK_FLAGS	(VM_GROWSUP | VM_STACK_DEFAULT_FLAGS | VM_ACCOUNT)
#else
#define VM_STACK_FLAGS	(VM_GROWSDOWN | VM_STACK_DEFAULT_FLAGS | VM_ACCOUNT)
#endif

#define VM_READHINTMASK			(VM_SEQ_READ | VM_RAND_READ)
#define VM_ClearReadHint(v)		(v)->vm_flags &= ~VM_READHINTMASK
#define VM_NormalReadHint(v)		(!((v)->vm_flags & VM_READHINTMASK))
#define VM_SequentialReadHint(v)	((v)->vm_flags & VM_SEQ_READ)
#define VM_RandomReadHint(v)		((v)->vm_flags & VM_RAND_READ)

/*
 * mapping from the currently active vm_flags protection bits (the
 * low four bits) to a page protection mask..
 */
extern pgprot_t protection_map[16];


/*
 * These are the virtual MM functions - opening of an area, closing and
 * unmapping it (needed to keep files on disk up-to-date etc), pointer
 * to the functions called when a no-page or a wp-page exception occurs. 
 */
struct vm_operations_struct {
	void (*open)(struct vm_area_struct * area);
	void (*close)(struct vm_area_struct * area);
	struct page * (*nopage)(struct vm_area_struct * area, unsigned long address, int *type);
	int (*populate)(struct vm_area_struct * area, unsigned long address, unsigned long len, pgprot_t prot, unsigned long pgoff, int nonblock);
#ifdef CONFIG_NUMA
	int (*set_policy)(struct vm_area_struct *vma, struct mempolicy *new);
	struct mempolicy *(*get_policy)(struct vm_area_struct *vma,
					unsigned long addr);
#endif
};

struct mmu_gather;
struct inode;

#ifdef ARCH_HAS_ATOMIC_UNSIGNED
typedef unsigned page_flags_t;
#else
typedef unsigned long page_flags_t;
#endif

/*
 * 系统中的每个物理页面都有一个与之关联的结构页面，
 * 以跟踪我们目前正在使用该页面的任何内容。
 * 请注意，我们无法跟踪哪些任务正在使用页面。
 *
 * 物理页框描述符
 * 用来描述页框本身，而不描述其中的数据
 */
struct page {
	page_flags_t flags;		/* 原子标志，一些可能异步更新，高位被编码存放其他数据*/
	atomic_t _count;		/* 引用计数器，若为-1则表示页框空闲；当有一个进程或内核数据结构占用时为0，...*/
	atomic_t _mapcount;		/* 页表项的引用计数，没有引用则为-1，0表示非共享，大于0表示共享 */
	unsigned long private;		/* Mapping-private opaque data:     私有数据使用
					 * usually used for buffer_heads
					 * if PagePrivate set; used for
					 * swp_entry_t if PageSwapCache
					 * 当页面空闲时，这表示伙伴系统中的order。
					 */
	struct address_space *mapping;	/*
 					 * 为NULL，表示属于交换高速缓存
 					 * 非NULL，若最低位为0，表示文件映射，
 					 * 指向文件的struct address_space
 					 * (struct address_space是4字节对齐的)；
 					 * 若最低位1，表示匿名映射，指向anon_vma
 					 *
 					 * 如果低位清零，则指向 inode address_space，或 NULL。
					 * 如果页面映射为匿名内存，则设置低位，并指向 anon_vma 对象：
					 * see PAGE_MAPPING_ANON below.
					 */
	pgoff_t index;			/* 我们在映射中的偏移量。 */
	struct list_head lru;		/* 页框高速缓存 分页列表，例如。受 zone->lru_lock 保护的 active_list!*/
	/*
	 * 在所有 RAM 都映射到内核地址空间的机器上，我们可以简单地计算虚拟地址。
	 * 在具有 highmem 的机器上，一些内存被动态映射到内核虚拟内存中，因此我们需要一个地方来存储该地址。
	 * 请注意，该字段在 x86 上可能是 16 位 ... ;)
	 *
	 * 乘法慢的架构可以在 asm/page.h 中定义 WANT_PAGE_VIRTUAL
	 */
#if defined(WANT_PAGE_VIRTUAL)
	/* 内核也可能会有虚拟地址，在highmem中为NULL*/
	void *virtual;			/* 内核虚拟地址（如果没有 kmapped，则为 NULL，即 highmem） */
#endif /* WANT_PAGE_VIRTUAL */
};

/*
 * FIXME: take this include out, include page-flags.h in
 * files which need it (119 of them)
 */
#include <linux/page-flags.h>

/*
 * 修改页面使用计数的方法。
 *
 * 什么对页面使用很重要：
 * - cache mapping   (page->mapping)
 * - private data    (page->private)
 * - 页映射在任务的页表中，每个映射单独计算
 *
 * 此外，许多内核例程在关键例程之前增加页面计数，因此它们可以确保页面不会从它们下面消失。
 *
 * 从 2.6.6（大约）开始，一个空闲页面有 ->_count = -1。
 * 这样我们就可以使用 atomic_add_negative(-1, page->_count)
 * 来检测页面何时变为空闲，并且我们还可以使用 atomic_inc_and_test 来自动
 * 检测何时我们只是尝试在其他 CPU 上抓取页面上的引用已经被认为是可释放的。
 *
 * 任何代码都不应该对这个内部细节做出假设！使用提供的保留旧规则的宏：
 * page_count(page) == 0 是一个已释放的页面。
 */

/*
 * 删除一个引用，如果逻辑引用计数为零（该页面没有用户），则返回 true
 */
#define put_page_testzero(p)				\
	({						\
		BUG_ON(page_count(p) == 0);		\
		atomic_add_negative(-1, &(p)->_count);	\
	})

/*
 * Grab a ref, return true if the page previously had a logical refcount of
 * zero.  ie: returns true if we just grabbed an already-deemed-to-be-free page
 */
#define get_page_testone(p)	atomic_inc_and_test(&(p)->_count)

#define set_page_count(p,v) 	atomic_set(&(p)->_count, v - 1)
#define __put_page(p)		atomic_dec(&(p)->_count)

extern void FASTCALL(__page_cache_release(struct page *));

#ifdef CONFIG_HUGETLB_PAGE

// 返回当前页框p被引用的次数，_count为0时表示被引用1次，因此需要加1
static inline int page_count(struct page *p)
{
	if (PageCompound(p))
		p = (struct page *)p->private;
	return atomic_read(&(p)->_count) + 1;
}

static inline void get_page(struct page *page)
{
	if (unlikely(PageCompound(page)))
		page = (struct page *)page->private;
	atomic_inc(&page->_count);
}

void put_page(struct page *page);

#else		/* CONFIG_HUGETLB_PAGE */

#define page_count(p)		(atomic_read(&(p)->_count) + 1)

static inline void get_page(struct page *page)
{
	atomic_inc(&page->_count);
}

static inline void put_page(struct page *page)
{
	if (!PageReserved(page) && put_page_testzero(page))
		__page_cache_release(page);
}

#endif		/* CONFIG_HUGETLB_PAGE */

/*
 * Multiple processes may "see" the same page. E.g. for untouched
 * mappings of /dev/null, all processes see the same page full of
 * zeroes, and text pages of executables and shared libraries have
 * only one copy in memory, at most, normally.
 *
 * 对于非保留页面，page_count(page) 表示引用计数。
 *   page_count() == 0 意味着该页面是空闲的
 *   page_count() == 1 表示该页面仅用于一个目的（例如，一个进程的私有数据页面）。
 *
 * 页面可用于 kmalloc() 或任何其他执行 __get_free_page() 的人。
 * 在这种情况下，page_count() 至少为 1，所有其他字段均未使用，但应为 0 或 NULL。
 * 此页面的管理是使用它的人的责任。
 *
 * 其他页面（我们可以称它们为“进程页面”）完全由 Linux 内存管理器管理：
 * IO、缓冲区、交换等。以下讨论仅适用于它们。
 *
 * 一个页面可能属于一个inode的内存映射。在这种情况下，
 * page->mapping是指向inode的指针，page->index是该页的文件偏移量，
 * 以PAGE_CACHE_SIZE为单位。
 *
 * 一个页面包含一个不透明的“私有”成员，它属于页面的地址空间。
 * 通常，这是页面磁盘缓冲区循环列表的地址。
 *
 * 对于属于 inode 的页面，page_count() 是附加的数量，
 * 如果“private”包含某些内容，则加 1，如果页面缓存本身，则加 1。
 *
 * 属于一个 inode 的所有页面都在这些双向链表中：
 * mapping->clean_pages, mapping->dirty_pages and mapping->locked_pages;
 * using the page->list list_head. These fields are also used for
 * freelist managemet (when page_count()==0).
 *
 * 如果存在，还有一个每个映射的基数树映射索引到内存中的页面。该树的根为 mapping->root。
 *
 * 所有进程页面都可以做IO：
 * - inode pages may need to be read from disk,
 * - inode pages which have been modified and are MAP_SHARED may need
 *   to be written to disk,
 * - private pages which have been modified may need to be swapped out
 *   to swap space and (later) to be read back into memory.
 */

/*
 * The zone field is never updated after free_area_init_core()
 * sets it, so none of the operations on it need to be atomic.
 * We'll have up to (MAX_NUMNODES * MAX_NR_ZONES) zones total,
 * so we use (MAX_NODES_SHIFT + MAX_ZONES_SHIFT) here to get enough bits.
 */
#define NODEZONE_SHIFT (sizeof(page_flags_t)*8 - MAX_NODES_SHIFT - MAX_ZONES_SHIFT)
#define NODEZONE(node, zone)	((node << ZONES_SHIFT) | zone)      // ZONES_SHIFT为2

static inline unsigned long page_zonenum(struct page *page)
{
	return (page->flags >> NODEZONE_SHIFT) & (~(~0UL << ZONES_SHIFT));
}
static inline unsigned long page_to_nid(struct page *page)
{
	return (page->flags >> (NODEZONE_SHIFT + ZONES_SHIFT));
}

struct zone;
extern struct zone *zone_table[];

// 通过页框描述符查找其所在的内存区域
static inline struct zone *page_zone(struct page *page)
{
	return zone_table[page->flags >> NODEZONE_SHIFT];
}

// zone_table的数组下标NODEZONE(nid, zone)，被编码到flag中高位中
static inline void set_page_zone(struct page *page, unsigned long nodezone_num)
{
	page->flags &= ~(~0UL << NODEZONE_SHIFT);		// 高NODEZONE_SHIFT清0
	page->flags |= nodezone_num << NODEZONE_SHIFT;
}

#ifndef CONFIG_DISCONTIGMEM
/* 结构页数组 - 对于 discontigmem 使用 pgdat->lmem_map */
extern struct page *mem_map;
#endif

static inline void *lowmem_page_address(struct page *page)
{
	return __va(page_to_pfn(page) << PAGE_SHIFT);
}

#if defined(CONFIG_HIGHMEM) && !defined(WANT_PAGE_VIRTUAL)
#define HASHED_PAGE_VIRTUAL
#endif

#if defined(WANT_PAGE_VIRTUAL)
#define page_address(page) ((page)->virtual)
#define set_page_address(page, address)			\
	do {						\
		(page)->virtual = (address);		\
	} while(0)
#define page_address_init()  do { } while(0)
#endif

#if defined(HASHED_PAGE_VIRTUAL)
void *page_address(struct page *page);
void set_page_address(struct page *page, void *virtual);
void page_address_init(void);
#endif

#if !defined(HASHED_PAGE_VIRTUAL) && !defined(WANT_PAGE_VIRTUAL)
#define page_address(page) lowmem_page_address(page)
#define set_page_address(page, address)  do { } while(0)
#define page_address_init()  do { } while(0)
#endif

/*
 * 在映射到用户虚拟内存区域的匿名页面上，
 * page->mapping 指向它的 anon_vma，而不是指向 struct address_space；
 * 设置 PAGE_MAPPING_ANON 位以区分它。
 *
 * 请注意，令人困惑的是，“page_mapping”指的是从磁盘映射
 * 页面的 inode address_space；而“page_mapped”是指页面被映射到的用户虚拟地址空间。
 */
#define PAGE_MAPPING_ANON	1

extern struct address_space swapper_space;
static inline struct address_space *page_mapping(struct page *page)
{
	struct address_space *mapping = page->mapping;

	if (unlikely(PageSwapCache(page)))
		mapping = &swapper_space;
	else if (unlikely((unsigned long)mapping & PAGE_MAPPING_ANON))
		mapping = NULL;
	return mapping;
}
/* page是否用作匿名映射 */
static inline int PageAnon(struct page *page)
{
	return ((unsigned long)page->mapping & PAGE_MAPPING_ANON) != 0;
}

/*
 * Return the pagecache index of the passed page.  Regular pagecache pages
 * use ->index whereas swapcache pages use ->private
 */
static inline pgoff_t page_index(struct page *page)
{
	if (unlikely(PageSwapCache(page)))
		return page->private;
	return page->index;
}

/*
 * atomic page->_mapcount 和 _count 一样，
 * 从 -1 开始：这样就可以使用 atomic_inc_and_test
 * 和 atomic_add_negative(-1) 跟踪从它到它的转换。
 */
static inline void reset_page_mapcount(struct page *page)
{
	atomic_set(&(page)->_mapcount, -1);
}

/* 返回页框的引用计数
 * */
static inline int page_mapcount(struct page *page)
{
	return atomic_read(&(page)->_mapcount) + 1;
}

/*
 * 如果此页面映射到分页表，则返回 true
 */
static inline int page_mapped(struct page *page)
{
	return atomic_read(&(page)->_mapcount) >= 0;
}

/*
 * Error return values for the *_nopage functions
 */
#define NOPAGE_SIGBUS	(NULL)
#define NOPAGE_OOM	((struct page *) (-1))

/*
 * Different kinds of faults, as returned by handle_mm_fault().
 * Used to decide whether a process gets delivered SIGBUS or
 * just gets major/minor fault counters bumped up.
 */
#define VM_FAULT_OOM	(-1)
#define VM_FAULT_SIGBUS	0
#define VM_FAULT_MINOR	1
#define VM_FAULT_MAJOR	2

#define offset_in_page(p)	((unsigned long)(p) & ~PAGE_MASK)

extern void show_free_areas(void);

#ifdef CONFIG_SHMEM
struct page *shmem_nopage(struct vm_area_struct *vma,
			unsigned long address, int *type);
int shmem_set_policy(struct vm_area_struct *vma, struct mempolicy *new);
struct mempolicy *shmem_get_policy(struct vm_area_struct *vma,
					unsigned long addr);
int shmem_lock(struct file *file, int lock, struct user_struct *user);
#else
#define shmem_nopage filemap_nopage
#define shmem_lock(a, b, c) 	({0;})	/* always in memory, no need to lock */
#define shmem_set_policy(a, b)	(0)
#define shmem_get_policy(a, b)	(NULL)
#endif
struct file *shmem_file_setup(char *name, loff_t size, unsigned long flags);

int shmem_zero_setup(struct vm_area_struct *);

static inline int can_do_mlock(void)
{
	if (capable(CAP_IPC_LOCK))
		return 1;
	if (current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur != 0)
		return 1;
	return 0;
}
extern int user_shm_lock(size_t, struct user_struct *);
extern void user_shm_unlock(size_t, struct user_struct *);

/*
 * Parameter block passed down to zap_pte_range in exceptional cases.
 */
struct zap_details {
	struct vm_area_struct *nonlinear_vma;	/* Check page->index if set */
	struct address_space *check_mapping;	/* Check page->mapping if set */
	pgoff_t	first_index;			/* Lowest page->index to unmap */
	pgoff_t last_index;			/* Highest page->index to unmap */
	spinlock_t *i_mmap_lock;		/* For unmap_mapping_range: */
	unsigned long break_addr;		/* Where unmap_vmas stopped */
	unsigned long truncate_count;		/* Compare vm_truncate_count */
};

void zap_page_range(struct vm_area_struct *vma, unsigned long address,
		unsigned long size, struct zap_details *);
int unmap_vmas(struct mmu_gather **tlbp, struct mm_struct *mm,
		struct vm_area_struct *start_vma, unsigned long start_addr,
		unsigned long end_addr, unsigned long *nr_accounted,
		struct zap_details *);
void clear_page_range(struct mmu_gather *tlb, unsigned long addr, unsigned long end);
int copy_page_range(struct mm_struct *dst, struct mm_struct *src,
			struct vm_area_struct *vma);
int zeromap_page_range(struct vm_area_struct *vma, unsigned long from,
			unsigned long size, pgprot_t prot);
void unmap_mapping_range(struct address_space *mapping,
		loff_t const holebegin, loff_t const holelen, int even_cows);

static inline void unmap_shared_mapping_range(struct address_space *mapping,
		loff_t const holebegin, loff_t const holelen)
{
	unmap_mapping_range(mapping, holebegin, holelen, 0);
}

extern int vmtruncate(struct inode * inode, loff_t offset);
extern pud_t *FASTCALL(__pud_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address));
extern pmd_t *FASTCALL(__pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address));
extern pte_t *FASTCALL(pte_alloc_kernel(struct mm_struct *mm, pmd_t *pmd, unsigned long address));
extern pte_t *FASTCALL(pte_alloc_map(struct mm_struct *mm, pmd_t *pmd, unsigned long address));
extern int install_page(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, struct page *page, pgprot_t prot);
extern int install_file_pte(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, unsigned long pgoff, pgprot_t prot);
extern int handle_mm_fault(struct mm_struct *mm,struct vm_area_struct *vma, unsigned long address, int write_access);
extern int make_pages_present(unsigned long addr, unsigned long end);
extern int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write);
void install_arg_page(struct vm_area_struct *, struct page *, unsigned long);

int get_user_pages(struct task_struct *tsk, struct mm_struct *mm, unsigned long start,
		int len, int write, int force, struct page **pages, struct vm_area_struct **vmas);

int __set_page_dirty_buffers(struct page *page);
int __set_page_dirty_nobuffers(struct page *page);
int redirty_page_for_writepage(struct writeback_control *wbc,
				struct page *page);
int FASTCALL(set_page_dirty(struct page *page));
int set_page_dirty_lock(struct page *page);
int clear_page_dirty_for_io(struct page *page);

extern unsigned long do_mremap(unsigned long addr,
			       unsigned long old_len, unsigned long new_len,
			       unsigned long flags, unsigned long new_addr);

/*
 * Prototype to add a shrinker callback for ageable caches.
 * 
 * These functions are passed a count `nr_to_scan' and a gfpmask.  They should
 * scan `nr_to_scan' objects, attempting to free them.
 *
 * The callback must the number of objects which remain in the cache.
 *
 * The callback will be passes nr_to_scan == 0 when the VM is querying the
 * cache size, so a fastpath for that case is appropriate.
 */
typedef int (*shrinker_t)(int nr_to_scan, unsigned int gfp_mask);

/*
 * Add an aging callback.  The int is the number of 'seeks' it takes
 * to recreate one of the objects that these functions age.
 */

#define DEFAULT_SEEKS 2
struct shrinker;
extern struct shrinker *set_shrinker(int, shrinker_t);
extern void remove_shrinker(struct shrinker *shrinker);

/*
 * On a two-level or three-level page table, this ends up being trivial. Thus
 * the inlining and the symmetry break with pte_alloc_map() that does all
 * of this out-of-line.
 */
/*
 * The following ifdef needed to get the 4level-fixup.h header to work.
 * Remove it when 4level-fixup.h has been removed.
 */
#ifdef CONFIG_MMU
#ifndef __ARCH_HAS_4LEVEL_HACK 
static inline pud_t *pud_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address)
{
	if (pgd_none(*pgd))
		return __pud_alloc(mm, pgd, address);
	return pud_offset(pgd, address);
}

static inline pmd_t *pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address)
{
	if (pud_none(*pud))
		return __pmd_alloc(mm, pud, address);
	return pmd_offset(pud, address);
}
#endif
#endif /* CONFIG_MMU */

extern void free_area_init(unsigned long * zones_size);
extern void free_area_init_node(int nid, pg_data_t *pgdat,
	unsigned long * zones_size, unsigned long zone_start_pfn, 
	unsigned long *zholes_size);
extern void memmap_init_zone(unsigned long, int, unsigned long, unsigned long);
extern void mem_init(void);
extern void show_mem(void);
extern void si_meminfo(struct sysinfo * val);
extern void si_meminfo_node(struct sysinfo *val, int nid);

/* prio_tree.c */
void vma_prio_tree_add(struct vm_area_struct *, struct vm_area_struct *old);
void vma_prio_tree_insert(struct vm_area_struct *, struct prio_tree_root *);
void vma_prio_tree_remove(struct vm_area_struct *, struct prio_tree_root *);
struct vm_area_struct *vma_prio_tree_next(struct vm_area_struct *vma,
	struct prio_tree_iter *iter);

#define vma_prio_tree_foreach(vma, iter, root, begin, end)	\
	for (prio_tree_iter_init(iter, root, begin, end), vma = NULL;	\
		(vma = vma_prio_tree_next(vma, iter)); )

static inline void vma_nonlinear_insert(struct vm_area_struct *vma,
					struct list_head *list)
{
	vma->shared.vm_set.parent = NULL;
	list_add_tail(&vma->shared.vm_set.list, list);
}

/* mmap.c */
extern int __vm_enough_memory(long pages, int cap_sys_admin);
extern void vma_adjust(struct vm_area_struct *vma, unsigned long start,
	unsigned long end, pgoff_t pgoff, struct vm_area_struct *insert);
extern struct vm_area_struct *vma_merge(struct mm_struct *,
	struct vm_area_struct *prev, unsigned long addr, unsigned long end,
	unsigned long vm_flags, struct anon_vma *, struct file *, pgoff_t,
	struct mempolicy *);
extern struct anon_vma *find_mergeable_anon_vma(struct vm_area_struct *);
extern int split_vma(struct mm_struct *,
	struct vm_area_struct *, unsigned long addr, int new_below);
extern int insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void __vma_link_rb(struct mm_struct *, struct vm_area_struct *,
	struct rb_node **, struct rb_node *);
extern struct vm_area_struct *copy_vma(struct vm_area_struct **,
	unsigned long addr, unsigned long len, pgoff_t pgoff);
extern void exit_mmap(struct mm_struct *);

extern unsigned long get_unmapped_area(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);

extern unsigned long do_mmap_pgoff(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long pgoff);

static inline unsigned long do_mmap(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long offset)
{
	unsigned long ret = -EINVAL;
	if ((offset + PAGE_ALIGN(len)) < offset)
		goto out;
	if (!(offset & ~PAGE_MASK))
		ret = do_mmap_pgoff(file, addr, len, prot, flag, offset >> PAGE_SHIFT);
out:
	return ret;
}

extern int do_munmap(struct mm_struct *, unsigned long, size_t);

extern unsigned long do_brk(unsigned long, unsigned long);

/* filemap.c */
extern unsigned long page_unuse(struct page *);
extern void truncate_inode_pages(struct address_space *, loff_t);

/* generic vm_area_ops exported for stackable file systems */
extern struct page *filemap_nopage(struct vm_area_struct *, unsigned long, int *);
extern int filemap_populate(struct vm_area_struct *, unsigned long,
		unsigned long, pgprot_t, unsigned long, int);

/* mm/page-writeback.c */
int write_one_page(struct page *page, int wait);

/* readahead.c */
#define VM_MAX_READAHEAD	128	/* kbytes */
#define VM_MIN_READAHEAD	16	/* kbytes (includes current page) */
#define VM_MAX_CACHE_HIT    	256	/* max pages in a row in cache before
					 * turning readahead off */

int do_page_cache_readahead(struct address_space *mapping, struct file *filp,
			unsigned long offset, unsigned long nr_to_read);
int force_page_cache_readahead(struct address_space *mapping, struct file *filp,
			unsigned long offset, unsigned long nr_to_read);
unsigned long  page_cache_readahead(struct address_space *mapping,
			  struct file_ra_state *ra,
			  struct file *filp,
			  unsigned long offset,
			  unsigned long size);
void handle_ra_miss(struct address_space *mapping, 
		    struct file_ra_state *ra, pgoff_t offset);
unsigned long max_sane_readahead(unsigned long nr);

/* Do stack extension */
extern int expand_stack(struct vm_area_struct * vma, unsigned long address);

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
extern struct vm_area_struct * find_vma(struct mm_struct * mm, unsigned long addr);
extern struct vm_area_struct * find_vma_prev(struct mm_struct * mm, unsigned long addr,
					     struct vm_area_struct **pprev);

/* Look up the first VMA which intersects the interval start_addr..end_addr-1,
   NULL if none.  Assume start_addr < end_addr. */
static inline struct vm_area_struct * find_vma_intersection(struct mm_struct * mm, unsigned long start_addr, unsigned long end_addr)
{
	struct vm_area_struct * vma = find_vma(mm,start_addr);

	if (vma && end_addr <= vma->vm_start)
		vma = NULL;
	return vma;
}

static inline unsigned long vma_pages(struct vm_area_struct *vma)
{
	return (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
}

extern struct vm_area_struct *find_extend_vma(struct mm_struct *mm, unsigned long addr);

extern struct page * vmalloc_to_page(void *addr);
extern unsigned long vmalloc_to_pfn(void *addr);
extern struct page * follow_page(struct mm_struct *mm, unsigned long address,
		int write);
extern int check_user_page_readable(struct mm_struct *mm, unsigned long address);
int remap_pfn_range(struct vm_area_struct *, unsigned long,
		unsigned long, unsigned long, pgprot_t);

#ifdef CONFIG_PROC_FS
void __vm_stat_account(struct mm_struct *, unsigned long, struct file *, long);
#else
static inline void __vm_stat_account(struct mm_struct *mm,
			unsigned long flags, struct file *file, long pages)
{
}
#endif /* CONFIG_PROC_FS */

static inline void vm_stat_account(struct vm_area_struct *vma)
{
	__vm_stat_account(vma->vm_mm, vma->vm_flags, vma->vm_file,
							vma_pages(vma));
}

static inline void vm_stat_unaccount(struct vm_area_struct *vma)
{
	__vm_stat_account(vma->vm_mm, vma->vm_flags, vma->vm_file,
							-vma_pages(vma));
}

/* update per process rss and vm hiwater data */
extern void update_mem_hiwater(void);

#ifndef CONFIG_DEBUG_PAGEALLOC
static inline void
kernel_map_pages(struct page *page, int numpages, int enable)
{
}
#endif

extern struct vm_area_struct *get_gate_vma(struct task_struct *tsk);
#ifdef	__HAVE_ARCH_GATE_AREA
int in_gate_area_no_task(unsigned long addr);
int in_gate_area(struct task_struct *task, unsigned long addr);
#else
int in_gate_area_no_task(unsigned long addr);
#define in_gate_area(task, addr) ({(void)task; in_gate_area_no_task(addr);})
#endif	/* __HAVE_ARCH_GATE_AREA */

#endif /* __KERNEL__ */
#endif /* _LINUX_MM_H */
