#ifndef _LINUX_RMAP_H
#define _LINUX_RMAP_H
/*
 * mm/rmap.c 中反向映射函数的声明
 */

#include <linux/config.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>

/*
 * anon_vma 包含一个私有“相关”vmas 列表，
 * 用于扫描指向此 anon_vma 的匿名页面是否需要取消映射：
 * 列表中的 vmas 将通过分叉或拆分来关联。
 *
 * 由于 vmas 在拆分和合并时来来去去（尤其是在 mprotect 中），
 * 因此匿名页面的映射字段不能直接指向 vma：
 * 相反，它指向 anon_vma，在其列表中可以轻松链接或取消链接相关的 vmas。
 *
 * 在取消链接列表中的最后一个 vma 之后，
 * 我们必须对 anon_vma 对象本身进行垃圾收集：
 * 我们保证，一旦 anon_vma 列表为空，就没有页面可以指向此 anon_vma。
 */
struct anon_vma {
	spinlock_t lock;	/* 序列化对 vma 列表的访问 */
	struct list_head head;	/* 私有“相关”虚拟机列表*/
};

#ifdef CONFIG_MMU

extern kmem_cache_t *anon_vma_cachep;
/* 分配anon_vma */
static inline struct anon_vma *anon_vma_alloc(void)
{
	return kmem_cache_alloc(anon_vma_cachep, SLAB_KERNEL);
}

/* 释放anon_vma */
static inline void anon_vma_free(struct anon_vma *anon_vma)
{
	kmem_cache_free(anon_vma_cachep, anon_vma);
}

/* 对vma的anon_vma上锁 */
static inline void anon_vma_lock(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma = vma->anon_vma;
	if (anon_vma)
		spin_lock(&anon_vma->lock);
}

/* 对vma的anon_vma解锁 */
static inline void anon_vma_unlock(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma = vma->anon_vma;
	if (anon_vma)
		spin_unlock(&anon_vma->lock);
}

/*
 * anon_vma helper functions.
 */
void anon_vma_init(void);	/* create anon_vma_cachep */
int  anon_vma_prepare(struct vm_area_struct *);
void __anon_vma_merge(struct vm_area_struct *, struct vm_area_struct *);
void anon_vma_unlink(struct vm_area_struct *);
void anon_vma_link(struct vm_area_struct *);
void __anon_vma_link(struct vm_area_struct *);

/*
 * rmap interfaces called when adding or removing pte of page
 */
void page_add_anon_rmap(struct page *, struct vm_area_struct *, unsigned long);
void page_add_file_rmap(struct page *);
void page_remove_rmap(struct page *);

/**
 * page_dup_rmap - duplicate pte mapping to a page
 * @page:	the page to add the mapping to
 *
 * For copy_page_range only: minimal extract from page_add_rmap,
 * avoiding unnecessary tests (already checked) so it's quicker.
 */
static inline void page_dup_rmap(struct page *page)
{
	atomic_inc(&page->_mapcount);
}

/*
 * Called from mm/vmscan.c to handle paging out
 */
int page_referenced(struct page *, int is_locked, int ignore_token);
int try_to_unmap(struct page *);

/*
 * Used by swapoff to help locate where page is expected in vma.
 */
unsigned long page_address_in_vma(struct page *, struct vm_area_struct *);

#else	/* !CONFIG_MMU */

#define anon_vma_init()		do {} while (0)
#define anon_vma_prepare(vma)	(0)
#define anon_vma_link(vma)	do {} while (0)

#define page_referenced(page,l,i) TestClearPageReferenced(page)
#define try_to_unmap(page)	SWAP_FAIL

#endif	/* CONFIG_MMU */

/*
 * try_to_unmap 的返回值
 */
#define SWAP_SUCCESS	0		// 成功
#define SWAP_AGAIN	1		// 稍后尝试
#define SWAP_FAIL	2		// 失败

#endif	/* _LINUX_RMAP_H */
