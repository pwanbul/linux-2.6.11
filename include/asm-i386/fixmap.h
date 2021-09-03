/*
 * fixmap.h: compile-time virtual memory allocation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ingo Molnar
 *
 * Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 */

#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

#include <linux/config.h>

/* used by vmalloc.c, vsyscall.lds.S.
 *
 * Leave one empty page between vmalloc'ed areas and
 * the start of the fixmap.
 */
#define __FIXADDR_TOP	0xfffff000

#ifndef __ASSEMBLY__
#include <linux/kernel.h>
#include <asm/acpi.h>
#include <asm/apicdef.h>
#include <asm/page.h>
#ifdef CONFIG_HIGHMEM
#include <linux/threads.h>
#include <asm/kmap_types.h>
#endif

/*
 * 在这里，我们定义了所有编译时“特殊”虚拟地址。
 *重点是在编译时有一个常量地址，但只在引导过程中设置物理地址。
 * 我们从虚拟内存的末尾（0xfffff000）向后分配这些特殊地址。
 * 这也让我们可以做故障安全的 vmalloc()，
 * 我们可以保证这些特殊地址和 vmalloc() ed 地址永远不会重叠。
 *
 * 这些“编译时分配”内存缓冲区是固定大小的 4k 页。
 * （或更大，如果使用的增量大于 1）使用 fixmap_set(idx,phys)
 * 将物理内存与 fixmap 索引相关联。
 *
 * 此类缓冲区的 TLB 条目不会在任务切换之间刷新。
 */
enum fixed_addresses {
	FIX_HOLE,                       // 0
	FIX_VSYSCALL,                   // 1
#ifdef CONFIG_X86_LOCAL_APIC
	FIX_APIC_BASE,	/* local (CPU) APIC) -- required for SMP or not */
#endif
#ifdef CONFIG_X86_IO_APIC
	FIX_IO_APIC_BASE_0,
	FIX_IO_APIC_BASE_END = FIX_IO_APIC_BASE_0 + MAX_IO_APICS-1,
#endif
#ifdef CONFIG_X86_VISWS_APIC
	FIX_CO_CPU,	/* Cobalt timer */
	FIX_CO_APIC,	/* Cobalt APIC Redirection Table */ 
	FIX_LI_PCIA,	/* Lithium PCI Bridge A */
	FIX_LI_PCIB,	/* Lithium PCI Bridge B */
#endif
#ifdef CONFIG_X86_F00F_BUG
	FIX_F00F_IDT,	/* Virtual mapping for IDT */
#endif
#ifdef CONFIG_X86_CYCLONE_TIMER
	FIX_CYCLONE_TIMER, /*cyclone timer register*/
#endif 
#ifdef CONFIG_HIGHMEM       // 配置的high memory
	FIX_KMAP_BEGIN,	/* reserved pte's for temporary kernel mappings */      // 2
	FIX_KMAP_END = FIX_KMAP_BEGIN+(KM_TYPE_NR*NR_CPUS)-1,       //  FIX_KMAP_BEGIN + (13*NR_CPUS)-1, 14
#endif
#ifdef CONFIG_ACPI_BOOT
	FIX_ACPI_BEGIN,
	FIX_ACPI_END = FIX_ACPI_BEGIN + FIX_ACPI_PAGES - 1,
#endif
#ifdef CONFIG_PCI_MMCONFIG
	FIX_PCIE_MCFG,
#endif
	__end_of_permanent_fixed_addresses,     // 假设除了CONFIG_HIGHMEM之外，其他都没配置，那么等于 15
	/* temporary boot-time mappings, used before ioremap() is functional */
#define NR_FIX_BTMAPS	16
	FIX_BTMAP_END = __end_of_permanent_fixed_addresses,     // 15
	FIX_BTMAP_BEGIN = FIX_BTMAP_END + NR_FIX_BTMAPS - 1,        // 30
	FIX_WP_TEST,        // 41
	__end_of_fixed_addresses        // 42
};

extern void __set_fixmap (enum fixed_addresses idx,
					unsigned long phys, pgprot_t flags);

#define set_fixmap(idx, phys) \
		__set_fixmap(idx, phys, PAGE_KERNEL)
/*
 * Some hardware wants to get fixmapped without caching.
 */
#define set_fixmap_nocache(idx, phys) \
		__set_fixmap(idx, phys, PAGE_KERNEL_NOCACHE)

#define clear_fixmap(idx) \
		__set_fixmap(idx, 0, __pgprot(0))

#define FIXADDR_TOP	((unsigned long)__FIXADDR_TOP)      // 0xfffff000

#define __FIXADDR_SIZE	(__end_of_permanent_fixed_addresses << PAGE_SHIFT)
#define __FIXADDR_BOOT_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START		(FIXADDR_TOP - __FIXADDR_SIZE)
#define FIXADDR_BOOT_START	(FIXADDR_TOP - __FIXADDR_BOOT_SIZE)

#define __fix_to_virt(x)	(FIXADDR_TOP - ((x) << PAGE_SHIFT))
#define __virt_to_fix(x)	((FIXADDR_TOP - ((x)&PAGE_MASK)) >> PAGE_SHIFT)

/*
 * This is the range that is readable by user mode, and things
 * acting like user mode such as get_user_pages.
 */
#define FIXADDR_USER_START	(__fix_to_virt(FIX_VSYSCALL))
#define FIXADDR_USER_END	(FIXADDR_USER_START + PAGE_SIZE)


extern void __this_fixmap_does_not_exist(void);

/*
 * “索引到地址”翻译。如果有人试图直接使用 idx 而不进行翻译，
 * 我们会使用 NULL-deference kernel oops 来捕获错误。
 * 传入索引的非法范围也被捕获。
 *
 * idx在enum fixed_address中定义，转换为addr
 */
static __always_inline unsigned long fix_to_virt(const unsigned int idx)
{
	/*
	 * this branch gets completely eliminated after inlining,
	 * except when someone tries to use fixaddr indices in an
	 * illegal way. (such as mixing up address types or using
	 * out-of-range indices).
	 *
	 * If it doesn't get removed, the linker will complain
	 * loudly with a reasonably clear error message..
	 */
	if (idx >= __end_of_fixed_addresses)
		__this_fixmap_does_not_exist();

        return __fix_to_virt(idx);
}

static inline unsigned long virt_to_fix(const unsigned long vaddr)
{
	BUG_ON(vaddr >= FIXADDR_TOP || vaddr < FIXADDR_START);
	return __virt_to_fix(vaddr);
}

#endif /* !__ASSEMBLY__ */
#endif
