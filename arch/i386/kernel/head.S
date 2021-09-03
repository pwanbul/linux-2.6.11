/*
 *  linux/arch/i386/kernel/head.S -- the 32-bit startup code.
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Enhanced CPU detection and feature setting code by Mike Jagdis
 *  and Martin Mares, November 1997.
 */

.text
#include <linux/config.h>
#include <linux/threads.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/desc.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/asm_offsets.h>
#include <asm/setup.h>

/*
 * References to members of the new_cpu_data structure.
 */

#define X86		new_cpu_data+CPUINFO_x86
#define X86_VENDOR	new_cpu_data+CPUINFO_x86_vendor
#define X86_MODEL	new_cpu_data+CPUINFO_x86_model
#define X86_MASK	new_cpu_data+CPUINFO_x86_mask
#define X86_HARD_MATH	new_cpu_data+CPUINFO_hard_math
#define X86_CPUID	new_cpu_data+CPUINFO_cpuid_level
#define X86_CAPABILITY	new_cpu_data+CPUINFO_x86_capability
#define X86_VENDOR_ID	new_cpu_data+CPUINFO_x86_vendor_id

/*
 * This is how much memory *in addition to the memory covered up to
 * and including _end* we need mapped initially.  We need one bit for
 * each possible page, but only in low memory, which means
 * 2^32/4096/8 = 128K worst case (4G/4G split.)
 *
 * Modulo rounding, each megabyte assigned here requires a kilobyte of
 * memory, which is currently unreclaimed.
 *
 * This should be a multiple of a page.
 */
#define INIT_MAP_BEYOND_END	(128*1024)


/*
 * 32位内核入口点；仅由引导CPU使用。
 * 进入时，％esi指向实模式代码作为32位指针。
 * CS和DS必须是4GB的平面段，但是我们不依赖
 * 任何特定的GDT布局，因为我们会尽快加载自己的内容。
 */
// startup_32
ENTRY(startup_32)

/*
 * Set segments to known values.
 */
	cld
	lgdt boot_gdt_descr - __PAGE_OFFSET
	movl $(__BOOT_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	movl %eax,%fs
	movl %eax,%gs

/*
 * Clear BSS first so that there are no surprises...
 * No need to cld as DF is already clear from cld above...
 */
	xorl %eax,%eax
	movl $__bss_start - __PAGE_OFFSET,%edi
	movl $__bss_stop - __PAGE_OFFSET,%ecx
	subl %edi,%ecx
	shrl $2,%ecx
	rep ; stosl

/*
 * 初始化页表。这将创建一个PDE和一组页表，它们位于_end之后。
 * 变量init_pg_tables_end被设置为指向第一个“安全”位置。
 * 映射在虚拟地址 0（身份映射）和PAGE_OFFSET处创建，
 * 最多可达 _end+sizeof（页表）+INIT_MAP_BEYOND_END。
 *
 * 警告：不要在此代码中使用 %esi 或堆栈。但是，如果您确实需要， %esp 可以用作 GPR...
 *
 * 从linux-2.4内核开始，在建立临时页表的时候，一般的教科书都说是映射了8M的物理内存，但是为什么是映射8M呢？
 * 当时网上有资料说，8M足够了，但为什么就足够了，一直没有彻底搞清楚，今天又重新分析这部分的代码(linux-2.6.24)。
 * 在建立临时页表时到底映射多大的内存取决于以下几个方面：
 * 1. 保护模式下内核的尺寸： 毫无疑问内核代码必须被映射
 * 2. 临时页表所占的空间尺寸： 假设临时页表映射整个4G的线性地址空间，那么：
 *      页面个数 = 4G/4k = 1M个页面
 *      每个页面对应一个页表项，占4个字节,那么总共占有4M的空间
 * 3. bootmem allocator是用来在真正的页表建立好之前用于内存管理的，他用一个位图表来管理整个内存,
 *    每一bit代表一个页框，假设有4G的物理内存，那么1M个页面共占有空间 = 1M/8 = 128K。
 * 4. 由于对齐占有的空间（可以忽略，感觉k的数量级吧）
 *
 * 综合上述几个方面，需要映射的物理内存大约等于：
 *      保护模式内核尺寸+临时页表占用空间尺寸+bootmem allocator位图表尺寸 =
 *      现在内核大约4M + 最大4M + 128K = 8M +　128K
 */
// __PAGE_OFFSET 是 0xc000 0000，所以 page_pde_offset 是 0xc00
page_pde_offset = (__PAGE_OFFSET >> 20);

    // __PAGE_OFFSET 是 3G，pg0 是虚拟地址，减去 __PAGE_OFFSET 后就得到了
    // pg0 的物理地址。我们把 pg0 的物理地址放在了 edi 寄存器里
	movl $(pg0 - __PAGE_OFFSET), %edi
	// 同理，这里把 swapper_pg_dir 的物理地址放在 edx
	movl $(swapper_pg_dir - __PAGE_OFFSET), %edx
	// page directory/table entry 的低 12 位都是一些标志物，各个位代表的含义
    // 读者可以参考 https://wiki.osdev.org/Paging 或者书中的第 52 页
	movl $0x007, %eax			/* 0x007 = PRESENT+RW+USER */
10:
	// 下面这两行代码对熟悉 C 语言的读者可能会造成一定的困扰。如果从 C 语言的角度
    // 来看，它们是把地址 &pg0 + 7 放到了 swapper_pg_dir 的第一项；但问题在于，
    // 为什么要 +7？
    // 其实这里的 7 和前面那个 7 一样，指的是页目录项的标志物 PRESENT+RW+USER，
    // pg0 的地址是 4K 对齐的，这意味着他的地址的低 12 位都为 0，加上 7 以后，刚
    // 好就是我们所需要的页目录项的值。
	leal 0x007(%edi),%ecx			/* Create PDE entry */
	movl %ecx,(%edx)			/* Store identity PDE entry */

	// 书里有说明，我们要把 0x0000 0000 ~ 0x007f ffff 和 0xc000 0000 ~ 0xc07f ffff
    // 都映射到物理地址 0x0000 0000 ~ 0x007f ffff，下面这一行设置的 0xc000 0000
    // 对应的页目录项。
    // 这里的问题在于，按照书里的说明，我们应该设置的是第 0x300 项，这里是加上的却是 0xc00。
    // 这里需要提一下平时用 C 语言时编译器帮我们做的事。当我们写下 int *p = NULL; p+2
    // 的时候，编译器知道 int 是 4 个字节，所以 p+2 会汇编代码里面是 +8。
    // 一个 PDE 也是 32 位，所以真正的偏移量是 0x300 << 2 = 0xc00
	movl %ecx,page_pde_offset(%edx)		/* Store kernel PDE entry */
	// edx + 4 以后，就是下一个页目录项了，下个循环将会继续初始化（一共两个页目录项）
	addl $4,%edx
    // 一个页表有 1024 个页表项，这里初始化一个在接下来的循环里面用到的计数器
	movl $1024, %ecx        // loop用到的计数器
11:
    // stosl 把 %eax 的内容复制到物理地址 ES:EDI，也就是 pg0 处；并且 %edi + 4
	stosl
	// 加上 0x1000 后，%eax 指向下一个页
	addl $0x1000,%eax
	// %ecx -= 1，如果 %ecx 不为 0，跳转到 11 处。这里总共会循环 1024 次，初始化 1024 个页表项。
	loop 11b
	/* End condition: we must map up to and including INIT_MAP_BEYOND_END (128*1024) 128个页表，每个页表1024项，每项4字节 */
	/* bytes beyond the end of our own page tables; the +0x007 is the attribute bits */
	leal (INIT_MAP_BEYOND_END+0x007)(%edi),%ebp
	cmpl %ebp,%eax
	jb 10b
	// 到这里的时候，%edi 的值是我们映射的最后一个页表项的地址，这里我们把它存到变量
    // init_pg_tables_end 里。init_pg_tables_end 在 setup.c 里定义
	movl %edi,(init_pg_tables_end - __PAGE_OFFSET)      // init_pg_tables_end中保存的地址为 _end+0x2000

#ifdef CONFIG_SMP
	xorl %ebx,%ebx				/* This is the boot CPU (BSP) */
	jmp 3f

/*
 * Non-boot CPU entry point; entered from trampoline.S
 * We can't lgdt here, because lgdt itself uses a data segment, but
 * we know the trampoline has already loaded the boot_gdt_table GDT
 * for us.
 */
ENTRY(startup_32_smp)
	cld
	movl $(__BOOT_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	movl %eax,%fs
	movl %eax,%gs

/*
 *	New page tables may be in 4Mbyte page mode and may
 *	be using the global pages. 
 *
 *	NOTE! If we are on a 486 we may have no cr4 at all!
 *	So we do not try to touch it unless we really have
 *	some bits in it to set.  This won't work if the BSP
 *	implements cr4 but this AP does not -- very unlikely
 *	but be warned!  The same applies to the pse feature
 *	if not equally supported. --macro
 *
 *	NOTE! We have to correct for the fact that we're
 *	not yet offset PAGE_OFFSET..
 */
#define cr4_bits mmu_cr4_features-__PAGE_OFFSET
	movl cr4_bits,%edx
	andl %edx,%edx
	jz 6f
	movl %cr4,%eax		# Turn on paging options (PSE,PAE,..)
	orl %edx,%eax
	movl %eax,%cr4

	btl $5, %eax		# check if PAE is enabled
	jnc 6f

	/* Check if extended functions are implemented */
	movl $0x80000000, %eax
	cpuid
	cmpl $0x80000000, %eax
	jbe 6f
	mov $0x80000001, %eax
	cpuid
	/* Execute Disable bit supported? */
	btl $20, %edx
	jnc 6f

	/* Setup EFER (Extended Feature Enable Register) */
	movl $0xc0000080, %ecx
	rdmsr

	btsl $11, %eax
	/* Make changes effective */
	wrmsr

6:
	/* This is a secondary processor (AP) */
	xorl %ebx,%ebx
	incl %ebx

3:
#endif /* CONFIG_SMP */

/*
 * 启用分页
 */
	movl $swapper_pg_dir-__PAGE_OFFSET,%eax     // 获取swapper_pg_dir的物理地址，存入eax中
	movl %eax,%cr3		/* 设置页表指针.. */
	movl %cr0,%eax
	orl $0x80000000,%eax
	movl %eax,%cr0		/* ..并设置分页（PG）位 */
	ljmp $__BOOT_CS,$1f	/* 清除预取并规范化 %eip，ljmp实际上会做两件事情，__BOOT_CS放到%cs寄存器的同时，1f处地址放到%eip里面*/
1:
	/* Set up the stack pointer */
	lss stack_start,%esp

/*
 * 初始化 eflags。一些 BIOS 的保留位如 NT 设置。如果跟踪此代码，这会使调试器感到困惑。
 * XXX - best to initialize before switching to protected mode.
 */
	pushl $0
	popfl

#ifdef CONFIG_SMP
	andl %ebx,%ebx
	jz  1f				/* Initial CPU cleans BSS */
	jmp checkCPUtype
1:
#endif /* CONFIG_SMP */

/*
 * 启动系统32位设置。我们需要重新做一些在16位模式下完成的“真实”操作。
 */
	call setup_idt

/*
 * 复制启动参数。
 * 注意： %esi 仍然具有指向实模式数据的指针。
 */
	movl $boot_params,%edi
	movl $(PARAM_SIZE/4),%ecx
	cld
	rep
	movsl   // 数据传送指令,16位模式下： 源地址是DS:SI,目的地址是ES:DI
	movl boot_params+NEW_CL_POINTER,%esi
	andl %esi,%esi
	jnz 2f			# New command line protocol
	cmpw $(OLD_CL_MAGIC),OLD_CL_MAGIC_ADDR
	jne 1f
	movzwl OLD_CL_OFFSET,%esi
	addl $(OLD_CL_BASE_ADDR),%esi
2:
	movl $saved_command_line,%edi
	movl $(COMMAND_LINE_SIZE/4),%ecx
	rep
	movsl
1:
checkCPUtype:

	movl $-1,X86_CPUID		#  -1 for no CPUID initially

/* check if it is 486 or 386. */
/*
 * XXX - this does a lot of unnecessary setup.  Alignment checks don't
 * apply at our cpl of 0 and the stack ought to be aligned already, and
 * we don't need to preserve eflags.
 */

	movb $3,X86		# at least 386
	pushfl			# push EFLAGS
	popl %eax		# get EFLAGS
	movl %eax,%ecx		# save original EFLAGS
	xorl $0x240000,%eax	# flip AC and ID bits in EFLAGS
	pushl %eax		# copy to EFLAGS
	popfl			# set EFLAGS
	pushfl			# get new EFLAGS
	popl %eax		# put it in eax
	xorl %ecx,%eax		# change in flags
	pushl %ecx		# restore original EFLAGS
	popfl
	testl $0x40000,%eax	# check if AC bit changed
	je is386

	movb $4,X86		# at least 486
	testl $0x200000,%eax	# check if ID bit changed
	je is486

	/* get vendor info */
	xorl %eax,%eax			# call CPUID with 0 -> return vendor ID
	cpuid
	movl %eax,X86_CPUID		# save CPUID level
	movl %ebx,X86_VENDOR_ID		# lo 4 chars
	movl %edx,X86_VENDOR_ID+4	# next 4 chars
	movl %ecx,X86_VENDOR_ID+8	# last 4 chars

	orl %eax,%eax			# do we have processor info as well?
	je is486

	movl $1,%eax		# Use the CPUID instruction to get CPU type
	cpuid
	movb %al,%cl		# save reg for future use
	andb $0x0f,%ah		# mask processor family
	movb %ah,X86
	andb $0xf0,%al		# mask model
	shrb $4,%al
	movb %al,X86_MODEL
	andb $0x0f,%cl		# mask mask revision
	movb %cl,X86_MASK
	movl %edx,X86_CAPABILITY

is486:	movl $0x50022,%ecx	# set AM, WP, NE and MP
	jmp 2f

is386:	movl $2,%ecx		# set MP
2:	movl %cr0,%eax
	andl $0x80000011,%eax	# Save PG,PE,ET
	orl %ecx,%eax
	movl %eax,%cr0

	call check_x87
	incb ready
	lgdt cpu_gdt_descr
	lidt idt_descr
	ljmp $(__KERNEL_CS),$1f
1:	movl $(__KERNEL_DS),%eax	# reload all the segment registers
	movl %eax,%ss			# after changing gdt.

	movl $(__USER_DS),%eax		# DS/ES contains default USER segment
	movl %eax,%ds
	movl %eax,%es

	xorl %eax,%eax			# Clear FS/GS and LDT
	movl %eax,%fs
	movl %eax,%gs
	lldt %ax
	cld			# gcc2 wants the direction flag cleared at all times
#ifdef CONFIG_SMP
	movb ready, %cl	
	cmpb $1,%cl
	je 1f			# the first CPU calls start_kernel
				# all other CPUs call initialize_secondary
	call initialize_secondary
	jmp L6
1:
#endif /* CONFIG_SMP */
	call start_kernel       // UP中调用start_kernel
L6:
	jmp L6			# main should never return here, but
				# just in case, we know what happens.

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
check_x87:
	movb $0,X86_HARD_MATH
	clts
	fninit
	fstsw %ax
	cmpb $0,%al
	je 1f
	movl %cr0,%eax		/* no coprocessor: have to set bits */
	xorl $4,%eax		/* set EM */
	movl %eax,%cr0
	ret
	ALIGN
1:	movb $1,X86_HARD_MATH
	.byte 0xDB,0xE4		/* fsetpm for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It doesn't actually load
 *  idt - that can be done only after paging has been enabled
 *  and the kernel moved to PAGE_OFFSET. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok.
 *
 *  Warning: %esi is live across this function.
 */
setup_idt:
	lea ignore_int,%edx
	movl $(__KERNEL_CS << 16),%eax
	movw %dx,%ax		/* selector = 0x0010 = cs */
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */

	lea idt_table,%edi
	mov $256,%ecx
rp_sidt:
	movl %eax,(%edi)
	movl %edx,4(%edi)
	addl $8,%edi
	dec %ecx
	jne rp_sidt
	ret

/* This is the default interrupt "handler" :-) */
	ALIGN
ignore_int:
	cld
	pushl %eax
	pushl %ecx
	pushl %edx
	pushl %es
	pushl %ds
	movl $(__KERNEL_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	pushl 16(%esp)
	pushl 24(%esp)
	pushl 32(%esp)
	pushl 40(%esp)
	pushl $int_msg
	call printk
	addl $(5*4),%esp
	popl %ds
	popl %es
	popl %edx
	popl %ecx
	popl %eax
	iret

/*
 * Real beginning of normal "text" segment
 */
ENTRY(stext)
ENTRY(_stext)

/*
 * BSS section
 */
.section ".bss.page_aligned","w"
ENTRY(swapper_pg_dir)       // 生成1024个4字节的空间，每个空间用0填充
	.fill 1024,4,0
ENTRY(empty_zero_page)      // 零页存放的是系统启动参数和命令行参数
	.fill 4096,1,0

/*
 * This starts the data section.
 */
.data


ENTRY(stack_start)
	.long init_thread_union+THREAD_SIZE
	.long __BOOT_DS

ready:	.byte 0

int_msg:
	.asciz "Unknown interrupt or fault at EIP %p %p %p\n"

/*
 * The IDT and GDT 'descriptors' are a strange 48-bit object
 * only used by the lidt and lgdt instructions. They are not
 * like usual segment descriptors - they consist of a 16-bit
 * segment size, and 32-bit linear address value:
 */

.globl boot_gdt_descr
.globl idt_descr
.globl cpu_gdt_descr

	ALIGN
# early boot GDT descriptor (must use 1:1 address mapping)
	.word 0				# 32 bit align gdt_desc.address
boot_gdt_descr:
	.word __BOOT_DS+7
	.long boot_gdt_table - __PAGE_OFFSET

	.word 0				# 32-bit align idt_desc.address
idt_descr:
	.word IDT_ENTRIES*8-1		# idt contains 256 entries
	.long idt_table

# boot GDT descriptor (later on used by CPU#0):
	.word 0				# 32 bit align gdt_desc.address
cpu_gdt_descr:
	.word GDT_ENTRIES*8-1
	.long cpu_gdt_table

	.fill NR_CPUS-1,8,0		# space for the other GDT descriptors

/*
 * The boot_gdt_table must mirror the equivalent in setup.S and is
 * used only for booting.
 */
	.align L1_CACHE_BYTES
ENTRY(boot_gdt_table)
	.fill GDT_ENTRY_BOOT_CS,8,0
	.quad 0x00cf9a000000ffff	/* kernel 4GB code at 0x00000000 */
	.quad 0x00cf92000000ffff	/* kernel 4GB data at 0x00000000 */

/*
 * The Global Descriptor Table contains 28 quadwords, per-CPU.
 */
	.align PAGE_SIZE_asm
ENTRY(cpu_gdt_table)
	.quad 0x0000000000000000	/* NULL descriptor */
	.quad 0x0000000000000000	/* 0x0b reserved */
	.quad 0x0000000000000000	/* 0x13 reserved */
	.quad 0x0000000000000000	/* 0x1b reserved */
	.quad 0x0000000000000000	/* 0x20 unused */
	.quad 0x0000000000000000	/* 0x28 unused */
	.quad 0x0000000000000000	/* 0x33 TLS entry 1 */
	.quad 0x0000000000000000	/* 0x3b TLS entry 2 */
	.quad 0x0000000000000000	/* 0x43 TLS entry 3 */
	.quad 0x0000000000000000	/* 0x4b reserved */
	.quad 0x0000000000000000	/* 0x53 reserved */
	.quad 0x0000000000000000	/* 0x5b reserved */

	.quad 0x00cf9a000000ffff	/* 0x60 kernel 4GB code at 0x00000000 */
	.quad 0x00cf92000000ffff	/* 0x68 kernel 4GB data at 0x00000000 */
	.quad 0x00cffa000000ffff	/* 0x73 user 4GB code at 0x00000000 */
	.quad 0x00cff2000000ffff	/* 0x7b user 4GB data at 0x00000000 */

	.quad 0x0000000000000000	/* 0x80 TSS descriptor */
	.quad 0x0000000000000000	/* 0x88 LDT descriptor */

	/* Segments used for calling PnP BIOS */
	.quad 0x00c09a0000000000	/* 0x90 32-bit code */
	.quad 0x00809a0000000000	/* 0x98 16-bit code */
	.quad 0x0080920000000000	/* 0xa0 16-bit data */
	.quad 0x0080920000000000	/* 0xa8 16-bit data */
	.quad 0x0080920000000000	/* 0xb0 16-bit data */
	/*
	 * The APM segments have byte granularity and their bases
	 * and limits are set at run time.
	 */
	.quad 0x00409a0000000000	/* 0xb8 APM CS    code */
	.quad 0x00009a0000000000	/* 0xc0 APM CS 16 code (16 bit) */
	.quad 0x0040920000000000	/* 0xc8 APM DS    data */

	.quad 0x0000000000000000	/* 0xd0 - unused */
	.quad 0x0000000000000000	/* 0xd8 - unused */
	.quad 0x0000000000000000	/* 0xe0 - unused */
	.quad 0x0000000000000000	/* 0xe8 - unused */
	.quad 0x0000000000000000	/* 0xf0 - unused */
	.quad 0x0000000000000000	/* 0xf8 - GDT entry 31: double-fault TSS */

