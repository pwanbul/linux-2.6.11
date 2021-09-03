/*
 * highmem.h: virtual kernel memory mappings for high memory
 *
 * Used in CONFIG_HIGHMEM systems for memory pages which
 * are not addressable by direct kernel virtual addresses.
 *
 * Copyright (C) 1999 Gerhard Wichert, Siemens AG
 *		      Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with 
 * up to 16 Terabyte physical memory. With current x86 CPUs
 * we now support up to 64 Gigabytes physical RAM.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/threads.h>
#include <asm/kmap_types.h>
#include <asm/tlbflush.h>

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

extern pte_t *kmap_pte;
extern pgprot_t kmap_prot;
extern pte_t *pkmap_page_table;

extern void kmap_init(void);

/*
 * 现在我们只初始化一个 pte 表。
 * 它可以轻松扩展，后续的 pte 表必须分配在一个物理 RAM 块中。
 */
#ifdef CONFIG_X86_PAE
#define LAST_PKMAP 512
#else
#define LAST_PKMAP 1024     // PKMap的页框数量
#endif
/*
 * Ordering is:
 * FIXADDR_END          // 0xffffffff
 * 4K空间，用于返回错误码
 * FIXADDR_TOP          // 0xfffff000
 *          这些地址指向物理内存中的随机位置。相对于内核空间起始处的线性映射，在该映射内部的虚拟地址和
 *          物理地址之间的关联不是预设的，而可以自由定义，但定义后不能改变。固定映射区域会一直延伸到虚拟地址空间顶端。
 *          固定映射地址的优点在于，在编译时对此类地址的处理类似于常数，内核一启动即为其分配了物理地址。
 *          此类地址的解引用比普通指针要快速。内核会确保在上下文切换期间，对应于固定映射的页表项不会从TLB刷出，
 *          因此在访问固定映射的内存时，总是通过TLB高速缓存取得对应的物理地址。
 * 			fixed_addresses     // 固定映射区域
 * 			该区域可以通过enum fixed_addresses细分
 * FIXADDR_START        // 0xfffff000 - __end_of_permanent_fixed_addresses << PAGE_SHIFT
 * 			temp fixed addresses        // 临时固定映射区域
 * FIXADDR_BOOT_START       // 0xfffff000 - __end_of_fixed_addresses << PAGE_SHIFT
 *          永久映射用于将高端内存域中的非持久页映射到内核中
 * 			Persistent kmap area        // 永久映射区域
 * PKMAP_BASE			// (FIXADDR_BOOT_START - PAGE_SIZE*(1024 + 1)) & PMD_MASK
 * 8K缓冲区
 * VMALLOC_END      // 动态映射区域的结束地址，通过PKMAP_BASE-8K(启用highmem)，或者FIXADDR_START-8K
 * 			Vmalloc area            // 该区域用于物理上不连续的内核映射
 * VMALLOC_START        // 动态映射区域的起始地址，通过high_memory加出来的，8M对齐
 * 8M缓冲区
 * high_memory      // highmem起始地址869M
 */
#define PKMAP_BASE ( (FIXADDR_BOOT_START - PAGE_SIZE*(LAST_PKMAP + 1)) & PMD_MASK )
#define LAST_PKMAP_MASK (LAST_PKMAP-1)
#define PKMAP_NR(virt)  ((virt-PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)  (PKMAP_BASE + ((nr) << PAGE_SHIFT))

extern void * FASTCALL(kmap_high(struct page *page));
extern void FASTCALL(kunmap_high(struct page *page));

void *kmap(struct page *page);
void kunmap(struct page *page);
void *kmap_atomic(struct page *page, enum km_type type);
void kunmap_atomic(void *kvaddr, enum km_type type);
struct page *kmap_atomic_to_page(void *ptr);

#define flush_cache_kmaps()	do { } while (0)

#endif /* __KERNEL__ */

#endif /* _ASM_HIGHMEM_H */
