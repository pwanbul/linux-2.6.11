/*
 *  linux/arch/i386/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *
 *  Memory region support
 *	David Parsons <orc@pell.chi.il.us>, July-August 1999
 *
 *  Added E820 sanitization routine (removes overlapping memory regions);
 *  Brian Moyle <bmoyle@mvista.com>, February 2001
 *
 * Moved CPU detection code to cpu/${cpu}.c
 *    Patrick Mochel <mochel@osdl.org>, March 2002
 *
 *  Provisions for empty E820 memory regions (reported by certain BIOSes).
 *  Alex Achenbach <xela@slit.de>, December 2002.
 *
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/apm_bios.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/mca.h>
#include <linux/root_dev.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/edd.h>
#include <video/edid.h>
#include <asm/e820.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/arch_hooks.h>
#include <asm/sections.h>
#include <asm/io_apic.h>
#include <asm/ist.h>
#include <asm/io.h>
#include "setup_arch_pre.h"
#include <bios_ebda.h>

/* 该值由早期引导代码设置为指向引导时间页表之后的值。
 * 它包含一个物理地址，并且不能在 .bss 段中！
 * */
unsigned long init_pg_tables_end __initdata = ~0UL;     // 注意取反

int disable_pse __initdata = 0;

/*
 * Machine setup..
 */

#ifdef CONFIG_EFI
int efi_enabled = 0;
EXPORT_SYMBOL(efi_enabled);
#endif

/* cpu data as detected by the assembly code in head.S */
struct cpuinfo_x86 new_cpu_data __initdata = { 0, 0, 0, 0, -1, 1, 0, 0, -1 };
/* common cpu data for all cpus */
struct cpuinfo_x86 boot_cpu_data = { 0, 0, 0, 0, -1, 1, 0, 0, -1 };

unsigned long mmu_cr4_features;
EXPORT_SYMBOL_GPL(mmu_cr4_features);

#ifdef	CONFIG_ACPI_INTERPRETER
	int acpi_disabled = 0;
#else
	int acpi_disabled = 1;
#endif
EXPORT_SYMBOL(acpi_disabled);

#ifdef	CONFIG_ACPI_BOOT
int __initdata acpi_force = 0;
extern acpi_interrupt_flags	acpi_sci_flags;
#endif

/* for MCA, but anyone else can use it if they want */
unsigned int machine_id;
unsigned int machine_submodel_id;
unsigned int BIOS_revision;
unsigned int mca_pentium_flag;

/* For PCI or other memory-mapped resources */
unsigned long pci_mem_start = 0x10000000;

/* Boot loader ID as an integer, for the benefit of proc_dointvec */
int bootloader_type;

/* user-defined highmem size */
static unsigned int highmem_pages = -1;

/*
 * Setup options
 */
struct drive_info_struct { char dummy[32]; } drive_info;
struct screen_info screen_info;
struct apm_info apm_info;
struct sys_desc_table_struct {
	unsigned short length;
	unsigned char table[0];
};
struct edid_info edid_info;
struct ist_info ist_info;
struct e820map e820;        // e820map实例

unsigned char aux_device_present;

extern void early_cpu_init(void);
extern void dmi_scan_machine(void);
extern void generic_apic_probe(char *);
extern int root_mountflags;

unsigned long saved_videomode;

#define RAMDISK_IMAGE_START_MASK  	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000	

static char command_line[COMMAND_LINE_SIZE];        //

unsigned char __initdata boot_params[PARAM_SIZE];       // 2048字节的启动参数

static struct resource data_resource = {
	.name	= "Kernel data",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource code_resource = {
	.name	= "Kernel code",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource system_rom_resource = {
	.name	= "System ROM",
	.start	= 0xf0000,
	.end	= 0xfffff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
};

static struct resource extension_rom_resource = {
	.name	= "Extension ROM",
	.start	= 0xe0000,
	.end	= 0xeffff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
};

static struct resource adapter_rom_resources[] = { {
	.name 	= "Adapter ROM",
	.start	= 0xc8000,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
} };

#define ADAPTER_ROM_RESOURCES \
	(sizeof adapter_rom_resources / sizeof adapter_rom_resources[0])

static struct resource video_rom_resource = {
	.name 	= "Video ROM",
	.start	= 0xc0000,
	.end	= 0xc7fff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
};

static struct resource video_ram_resource = {
	.name	= "Video RAM area",
	.start	= 0xa0000,
	.end	= 0xbffff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource standard_io_resources[] = { {
	.name	= "dma1",
	.start	= 0x0000,
	.end	= 0x001f,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "pic1",
	.start	= 0x0020,
	.end	= 0x0021,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name   = "timer0",
	.start	= 0x0040,
	.end    = 0x0043,
	.flags  = IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name   = "timer1",
	.start  = 0x0050,
	.end    = 0x0053,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "keyboard",
	.start	= 0x0060,
	.end	= 0x006f,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "dma page reg",
	.start	= 0x0080,
	.end	= 0x008f,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "pic2",
	.start	= 0x00a0,
	.end	= 0x00a1,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "dma2",
	.start	= 0x00c0,
	.end	= 0x00df,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "fpu",
	.start	= 0x00f0,
	.end	= 0x00ff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
} };

#define STANDARD_IO_RESOURCES \
	(sizeof standard_io_resources / sizeof standard_io_resources[0])

#define romsignature(x) (*(unsigned short *)(x) == 0xaa55)

static int __init romchecksum(unsigned char *rom, unsigned long length)
{
	unsigned char *p, sum = 0;

	for (p = rom; p < rom + length; p++)
		sum += *p;
	return sum == 0;
}

static void __init probe_roms(void)
{
	unsigned long start, length, upper;
	unsigned char *rom;
	int	      i;

	/* video rom */
	upper = adapter_rom_resources[0].start;
	for (start = video_rom_resource.start; start < upper; start += 2048) {
		rom = isa_bus_to_virt(start);
		if (!romsignature(rom))
			continue;

		video_rom_resource.start = start;

		/* 0 < length <= 0x7f * 512, historically */
		length = rom[2] * 512;

		/* if checksum okay, trust length byte */
		if (length && romchecksum(rom, length))
			video_rom_resource.end = start + length - 1;

		request_resource(&iomem_resource, &video_rom_resource);
		break;
	}

	start = (video_rom_resource.end + 1 + 2047) & ~2047UL;
	if (start < upper)
		start = upper;

	/* system rom */
	request_resource(&iomem_resource, &system_rom_resource);
	upper = system_rom_resource.start;

	/* check for extension rom (ignore length byte!) */
	rom = isa_bus_to_virt(extension_rom_resource.start);
	if (romsignature(rom)) {
		length = extension_rom_resource.end - extension_rom_resource.start + 1;
		if (romchecksum(rom, length)) {
			request_resource(&iomem_resource, &extension_rom_resource);
			upper = extension_rom_resource.start;
		}
	}

	/* check for adapter roms on 2k boundaries */
	for (i = 0; i < ADAPTER_ROM_RESOURCES && start < upper; start += 2048) {
		rom = isa_bus_to_virt(start);
		if (!romsignature(rom))
			continue;

		/* 0 < length <= 0x7f * 512, historically */
		length = rom[2] * 512;

		/* but accept any length that fits if checksum okay */
		if (!length || start + length > upper || !romchecksum(rom, length))
			continue;

		adapter_rom_resources[i].start = start;
		adapter_rom_resources[i].end = start + length - 1;
		request_resource(&iomem_resource, &adapter_rom_resources[i]);

		start = adapter_rom_resources[i++].end & ~2047UL;
	}
}

static void __init limit_regions(unsigned long long size)
{
	unsigned long long current_addr = 0;
	int i;

	if (efi_enabled) {
		for (i = 0; i < memmap.nr_map; i++) {
			current_addr = memmap.map[i].phys_addr +
				       (memmap.map[i].num_pages << 12);
			if (memmap.map[i].type == EFI_CONVENTIONAL_MEMORY) {
				if (current_addr >= size) {
					memmap.map[i].num_pages -=
						(((current_addr-size) + PAGE_SIZE-1) >> PAGE_SHIFT);
					memmap.nr_map = i + 1;
					return;
				}
			}
		}
	}
	for (i = 0; i < e820.nr_map; i++) {
		if (e820.map[i].type == E820_RAM) {
			current_addr = e820.map[i].addr + e820.map[i].size;
			if (current_addr >= size) {
				e820.map[i].size -= current_addr-size;
				e820.nr_map = i + 1;
				return;
			}
		}
	}
}

/* 把存储区域放到e820map 实例中，
 * 在此之前的操作都是在zero page上进行的
 * */
static void __init add_memory_region(unsigned long long start,
                                  unsigned long long size, int type)
{
	int x;

	if (!efi_enabled) {
       		x = e820.nr_map;

		if (x == E820MAX) {
		    printk(KERN_ERR "Ooops! Too many entries in the memory map!\n");
		    return;
		}

		e820.map[x].addr = start;
		e820.map[x].size = size;
		e820.map[x].type = type;
		e820.nr_map++;
	}
} /* add_memory_region */

#define E820_DEBUG	1

static void __init print_memory_map(char *who)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		printk(" %s: %016Lx - %016Lx ", who,
			e820.map[i].addr,
			e820.map[i].addr + e820.map[i].size);
		switch (e820.map[i].type) {
		case E820_RAM:	printk("(usable)\n");
				break;
		case E820_RESERVED:
				printk("(reserved)\n");
				break;
		case E820_ACPI:
				printk("(ACPI data)\n");
				break;
		case E820_NVS:
				printk("(ACPI NVS)\n");
				break;
		default:	printk("type %lu\n", e820.map[i].type);
				break;
		}
	}
}

/*
 * 清理 BIOS e820 映射。
 *
 * 一些 e820 响应包含重叠条目。下面用一张新的地图替换原来的 e820 地图，去除重叠。
 *
 */
struct change_member {
	struct e820entry *pbios; /* 指向原始 bios 条目的指针 */
	unsigned long long addr; /* 此更改点(change_point)的地址 */
};
struct change_member change_point_list[2*E820MAX] __initdata;
struct change_member *change_point[2*E820MAX] __initdata;
struct e820entry *overlap_list[E820MAX] __initdata; // 覆盖change_point
struct e820entry new_bios[E820MAX] __initdata;      // 新的e820map

static int __init sanitize_e820_map(struct e820entry * biosmap, char * pnr_map)
{
	struct change_member *change_tmp;
	unsigned long current_type, last_type;
	unsigned long long last_addr;
	int chgidx, still_changing;
	int overlap_entries;
	int new_bios_entry;
	int old_nr, new_nr, chg_nr;
	int i;

	/*
		从视觉上看，我们正在执行以下操作（1,2,3,4 = 内存类型）...

		Sample memory map (w/overlaps):
		   ____22__________________
		   ______________________4_
		   ____1111________________
		   _44_____________________
		   11111111________________
		   ____________________33__
		   ___________44___________
		   __________33333_________
		   ______________22________
		   ___________________2222_
		   _________111111111______
		   _____________________11_
		   _________________4______

		Sanitized equivalent (no overlap):
		   1_______________________
		   _44_____________________
		   ___1____________________
		   ____22__________________
		   ______11________________
		   _________1______________
		   __________3_____________
		   ___________44___________
		   _____________33_________
		   _______________2________
		   ________________1_______
		   _________________4______
		   ___________________2____
		   ____________________33__
		   ______________________4_
	*/

	/* 如果只有一个内存区域，请不要打扰，至少2个，0-640K和1M以上 */
	if (*pnr_map < 2)
		return -1;

	old_nr = *pnr_map;

	/* 如果我们在bios地图中发现任何不合理的地址，请出手 */
	for (i=0; i<old_nr; i++)
		if (biosmap[i].addr + biosmap[i].size < biosmap[i].addr)
			return -1;

	/* 为初始变化点（change_point）信息创建指针（用于排序） */
	for (i=0; i < 2*old_nr; i++)        // 每一条记录需要两个change_point
		change_point[i] = &change_point_list[i];

	/* 记录所有已知的变化点（起始和结束地址），省略那些用于空内存区域的变化点 */
	chgidx = 0;
	for (i=0; i < old_nr; i++)	{
		if (biosmap[i].size != 0) {
			change_point[chgidx]->addr = biosmap[i].addr;   // 起始地址
			change_point[chgidx++]->pbios = &biosmap[i];
			change_point[chgidx]->addr = biosmap[i].addr + biosmap[i].size;     // 结束地址
			change_point[chgidx++]->pbios = &biosmap[i];
		}
	}
	chg_nr = chgidx;    	/* 真实的change-points */

	/* sort change-point list by memory addresses (low -> high) */
	still_changing = 1;
	while (still_changing)	{
		still_changing = 0;
		for (i=1; i < chg_nr; i++)  {
			/* if <current_addr> > <last_addr>, swap 地址小的放前面 */
			/* or, if current = <start_addr> & last = <end_addr>, swap 地址相等时，作为起始地址放前面*/
			if ((change_point[i]->addr < change_point[i-1]->addr) ||
				((change_point[i]->addr == change_point[i-1]->addr) &&      // change_point[i]和change_point[i-1]相等
				 (change_point[i]->addr == change_point[i]->pbios->addr) &&     // change_point[i]为起始地址
				 (change_point[i-1]->addr != change_point[i-1]->pbios->addr))   // change_point[i-1]为结束地址
			   )
			{
				change_tmp = change_point[i];
				change_point[i] = change_point[i-1];
				change_point[i-1] = change_tmp;
				still_changing=1;
			}
		}
	}

	/* 创建一个新的 bios 内存映射，删除重叠 */
	overlap_entries=0;	 /* 重叠表中的记录数 */
	new_bios_entry=0;	 /* 用于创建新 bios 映射记录的索引 */
	last_type = 0;		 /* 以未定义的内存类型开始 */
	last_addr = 0;		 /* 从 0 开始作为最后的起始地址 */
	/* 循环变化点，确定对新 bios 地图的影响 */
	for (chgidx=0; chgidx < chg_nr; chgidx++)
	{
		/* 跟踪所有重叠的bios记录 */
		if (change_point[chgidx]->addr == change_point[chgidx]->pbios->addr)    // change_point为起始地址
		{
			/* 将map记录添加到重叠列表（>1个条目意味着重叠） */
			overlap_list[overlap_entries++]=change_point[chgidx]->pbios;
		}
		else        // change_point为结束地址
		{
			/* 从列表中删除记录（顺序无关，因此与最后一个交换） */
			for (i=0; i<overlap_entries; i++)
			{
				if (overlap_list[i] == change_point[chgidx]->pbios)
					overlap_list[i] = overlap_list[overlap_entries-1];
			}
			overlap_entries--;
		}
		/* 如果有重叠记录，请决定使用哪种“类型” */
		/* （较大的值优先——1=可用，2、3、4、4+=不可用）*/
		current_type = 0;
		for (i=0; i<overlap_entries; i++)
			if (overlap_list[i]->type > current_type)
				current_type = overlap_list[i]->type;
		/* 继续根据此信息构建新的bios map */
		if (current_type != last_type)	{
			if (last_type != 0)	 {
				new_bios[new_bios_entry].size =
					change_point[chgidx]->addr - last_addr;
				/* 仅当新大小不为零时才向前移动 */
				if (new_bios[new_bios_entry].size != 0)
					if (++new_bios_entry >= E820MAX)
						break; 	/* 没有更多空间可用于新的 bios 记录 */
			}
			if (current_type != 0)	{
				new_bios[new_bios_entry].addr = change_point[chgidx]->addr; // 保存起始地址
				new_bios[new_bios_entry].type = current_type;   // 保存类型
				last_addr=change_point[chgidx]->addr;
			}
			last_type = current_type;
		}
	}
	new_nr = new_bios_entry;   /* 保留新 bios 记录的计数 */

	/* 将新的 bios 映射复制到原始位置 */
	memcpy(biosmap, new_bios, new_nr*sizeof(struct e820entry));
	*pnr_map = new_nr;

	return 0;
}

/*
 * 将BIOS e820映射复制到安全的地方.
 *
 * 在我们处理时检查它的完整性。
 *
 * 如果我们幸运并且生活在现代系统上，设置代码将为我们提供一个内存映射，
 * 我们可以使用它来正确设置内存。如果不是，我们将伪造内存映射。
 *
 * 在使用之前，我们会检查内存映射是否至少包含2个元素，
 * 因为setup.S中的检测代码可能并不完美，而且大多数人类已知的PC都有两个内存区域：
 *      一个从0到640k，
 *      和一个从1mb起。
 * (例如，IBM thinkpad 560x 不配合内存检测代码。）
 */
static int __init copy_e820_map(struct e820entry * biosmap, int nr_map)
{
	/* Only one memory region (or negative)? Ignore it */
	if (nr_map < 2)     // 至少BIOS与RAM不是一个内存段的，所以nr_map < 2肯定是不对的
		return -1;

	do {
		unsigned long long start = biosmap->addr;
		unsigned long long size = biosmap->size;
		unsigned long long end = start + size;
		unsigned long type = biosmap->type;

		/* Overflow in 64 bits? Ignore the memory map. */
		if (start > end)
			return -1;

		/*
		 * Some BIOSes claim RAM in the 640k - 1M region.
		 * Not right. Fix it up.
		 * 如果类型为E820_RAM，即可用内存，判断这个范围是否覆盖 640KB～1MB，
		 * 这段需要为ISA图形卡等保留的，所以这段要保留，如果谁覆盖了这段需要把这段抠除。
		 * 物理地址从0x000a0000到 0x000fffff的范围通常留给BIOS例程，并且映射ISA图形卡上的内部内存。
		 * 这个区域就是所有的IBM兼容PC上从640KB～1MB之间著名的空洞：物理地址存在但被保留，对应的页框不能由操作系统使用。
		 */
		if (type == E820_RAM) {
		    /* 处理跨在洞上或者含在洞里的区域
		     * 1. 含在洞里的不能用
		     * 2. 跨在洞上的，把在洞内的部分去掉即可
		     * */
			if (start < 0x100000ULL/*1MB*/ && end > 0xA0000ULL/*640KB*/) {
				if (start < 0xA0000ULL)
					add_memory_region(start, 0xA0000ULL-start, type);       // 将E820图填充到struct e820map结构中
				if (end <= 0x100000ULL)
					continue;
				start = 0x100000ULL;
				size = end - start;
			}
		}
		add_memory_region(start, size, type);       // 将E820图填充到struct e820map结构中
	} while (biosmap++,--nr_map);
	return 0;
}

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
struct edd edd;
#ifdef CONFIG_EDD_MODULE
EXPORT_SYMBOL(edd);
#endif
/**
 * copy_edd() - Copy the BIOS EDD information
 *              from boot_params into a safe place.
 *
 */
static inline void copy_edd(void)
{
     memcpy(edd.mbr_signature, EDD_MBR_SIGNATURE, sizeof(edd.mbr_signature));
     memcpy(edd.edd_info, EDD_BUF, sizeof(edd.edd_info));
     edd.mbr_signature_nr = EDD_MBR_SIG_NR;
     edd.edd_info_nr = EDD_NR;
}
#else
static inline void copy_edd(void)
{
}
#endif

/*
 * Do NOT EVER look at the BIOS memory size location.
 * It does not work on many machines.
 */
#define LOWMEMSIZE()	(0x9f000)

static void __init parse_cmdline_early (char ** cmdline_p)
{
	char c = ' ', *to = command_line, *from = saved_command_line;
	int len = 0;
	int userdef = 0;

	/* 为 proc/cmdline 保存未解析的命令行副本 */
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';     // 256个参数

	for (;;) {
		if (c != ' ')       // 命令行参数有空格分隔
			goto next_char;
		/*
		 * "mem=nopentium" 禁用 4MB 页表。
		 * "mem=XXX[kKmM]" 定义从 HIGH_MEM 到 <mem> 的内存区域，覆盖 bios 大小。
		 * "memmap=XXX[KkmM]@XXX[KkmM]" 定义从 <start> 到 <start>+<mem> 的内存区域，覆盖 bios 大小。
		 *
		 * HPA tells me bootloaders need to parse mem=, so no new
		 * option should be mem=  [also see Documentation/i386/boot.txt]
		 */
		if (!memcmp(from, "mem=", 4)) {
			if (to != command_line)
				to--;
			if (!memcmp(from+4, "nopentium", 9)) {
				from += 9+4;
				clear_bit(X86_FEATURE_PSE, boot_cpu_data.x86_capability);
				disable_pse = 1;
			} else {
				/* If the user specifies memory size, we
				 * limit the BIOS-provided memory map to
				 * that size. exactmap can be used to specify
				 * the exact map. mem=number can be used to
				 * trim the existing memory map.
				 */
				unsigned long long mem_size;
 
				mem_size = memparse(from+4, &from);
				limit_regions(mem_size);
				userdef=1;
			}
		}

		else if (!memcmp(from, "memmap=", 7)) {
			if (to != command_line)
				to--;
			if (!memcmp(from+7, "exactmap", 8)) {
				from += 8+7;
				e820.nr_map = 0;
				userdef = 1;
			} else {
				/* If the user specifies memory size, we
				 * limit the BIOS-provided memory map to
				 * that size. exactmap can be used to specify
				 * the exact map. mem=number can be used to
				 * trim the existing memory map.
				 */
				unsigned long long start_at, mem_size;
 
				mem_size = memparse(from+7, &from);
				if (*from == '@') {
					start_at = memparse(from+1, &from);
					add_memory_region(start_at, mem_size, E820_RAM);
				} else if (*from == '#') {
					start_at = memparse(from+1, &from);
					add_memory_region(start_at, mem_size, E820_ACPI);
				} else if (*from == '$') {
					start_at = memparse(from+1, &from);
					add_memory_region(start_at, mem_size, E820_RESERVED);
				} else {
					limit_regions(mem_size);
					userdef=1;
				}
			}
		}

		else if (!memcmp(from, "noexec=", 7))
			noexec_setup(from + 7);


#ifdef  CONFIG_X86_SMP
		/*
		 * If the BIOS enumerates physical processors before logical,
		 * maxcpus=N at enumeration-time can be used to disable HT.
		 */
		else if (!memcmp(from, "maxcpus=", 8)) {
			extern unsigned int maxcpus;

			maxcpus = simple_strtoul(from + 8, NULL, 0);
		}
#endif

#ifdef CONFIG_ACPI_BOOT
		/* "acpi=off" disables both ACPI table parsing and interpreter */
		else if (!memcmp(from, "acpi=off", 8)) {
			disable_acpi();
		}

		/* acpi=force to over-ride black-list */
		else if (!memcmp(from, "acpi=force", 10)) {
			acpi_force = 1;
			acpi_ht = 1;
			acpi_disabled = 0;
		}

		/* acpi=strict disables out-of-spec workarounds */
		else if (!memcmp(from, "acpi=strict", 11)) {
			acpi_strict = 1;
		}

		/* Limit ACPI just to boot-time to enable HT */
		else if (!memcmp(from, "acpi=ht", 7)) {
			if (!acpi_force)
				disable_acpi();
			acpi_ht = 1;
		}
		
		/* "pci=noacpi" disable ACPI IRQ routing and PCI scan */
		else if (!memcmp(from, "pci=noacpi", 10)) {
			acpi_disable_pci();
		}
		/* "acpi=noirq" disables ACPI interrupt routing */
		else if (!memcmp(from, "acpi=noirq", 10)) {
			acpi_noirq_set();
		}

		else if (!memcmp(from, "acpi_sci=edge", 13))
			acpi_sci_flags.trigger =  1;

		else if (!memcmp(from, "acpi_sci=level", 14))
			acpi_sci_flags.trigger = 3;

		else if (!memcmp(from, "acpi_sci=high", 13))
			acpi_sci_flags.polarity = 1;

		else if (!memcmp(from, "acpi_sci=low", 12))
			acpi_sci_flags.polarity = 3;

#ifdef CONFIG_X86_IO_APIC
		else if (!memcmp(from, "acpi_skip_timer_override", 24))
			acpi_skip_timer_override = 1;
#endif

#ifdef CONFIG_X86_LOCAL_APIC
		/* disable IO-APIC */
		else if (!memcmp(from, "noapic", 6))
			disable_ioapic_setup();
#endif /* CONFIG_X86_LOCAL_APIC */
#endif /* CONFIG_ACPI_BOOT */

		/*
		 * highmem=size 强制 highmem 为“大小”字节。这甚至适用于没有 highmem 的盒子。这也适用于减少较大盒子上的 highmem 大小。
		 */
		else if (!memcmp(from, "highmem=", 8))
			highmem_pages = memparse(from+8, &from) >> PAGE_SHIFT;      // 高端内存的页框数，默认-1
	
		/*
		 * vmalloc=size 强制 vmalloc 区域正好是“大小”字节。这可用于增加（或减少）vmalloc 区域 - 默认为128MB。
		 */
		else if (!memcmp(from, "vmalloc=", 8))
			__VMALLOC_RESERVE = memparse(from+8, &from);

	next_char:
		c = *(from++);      // 指向下一个字符
		if (!c)     // 到头了
			break;
		if (COMMAND_LINE_SIZE <= ++len)     // 超过限制了
			break;
		*(to++) = c;        //
	}
	*to = '\0';
	*cmdline_p = command_line;
	if (userdef) {
		printk(KERN_INFO "user-defined physical RAM map:\n");
		print_memory_map("user");
	}
}

/*
 * Callback for efi_memory_walk.
 */
static int __init
efi_find_max_pfn(unsigned long start, unsigned long end, void *arg)
{
	unsigned long *max_pfn = arg, pfn;

	if (start < end) {
		pfn = PFN_UP(end -1);
		if (pfn > *max_pfn)
			*max_pfn = pfn;
	}
	return 0;
}


/*
 * 找到我们可用的最高页框编号
 */
void __init find_max_pfn(void)
{
	int i;

	max_pfn = 0;
	if (efi_enabled) {
		efi_memmap_walk(efi_find_max_pfn, &max_pfn);
		return;
	}

	// 遍历e820 map中的记录
	for (i = 0; i < e820.nr_map; i++) {
		unsigned long start, end;
		/* RAM? */
		if (e820.map[i].type != E820_RAM)
			continue;       // 只考虑RAM区域
		start = PFN_UP(e820.map[i].addr);
		end = PFN_DOWN(e820.map[i].addr + e820.map[i].size);
		if (start >= end)
			continue;
		if (end > max_pfn)
			max_pfn = end;      // 最大可映射页框号，物理内存为4GB时max_pfn为1M
	}
}

/*
 * 确定低和高内存范围:
 */
unsigned long __init find_max_low_pfn(void)
{
	unsigned long max_low_pfn;      // 低端内存的页框号

	max_low_pfn = max_pfn;      // max_pfn为最后一个可用的页框号，物理内存为4GB时为1M
	if (max_low_pfn > MAXMEM_PFN) {         // MAXMEM_PFN：0x38000，即896MB
		if (highmem_pages == -1)        // highmem_pages表示高端内存的页面数，用户可以指定，但实际为没有指定
			highmem_pages = max_pfn - MAXMEM_PFN;
		if (highmem_pages + MAXMEM_PFN < max_pfn)
			max_pfn = MAXMEM_PFN + highmem_pages;
		if (highmem_pages + MAXMEM_PFN > max_pfn) {		//如果指定的超出，则使用的计算出的
			printk("only %luMB highmem pages available, ignoring highmem size of %uMB.\n", pages_to_mb(max_pfn - MAXMEM_PFN), pages_to_mb(highmem_pages));
			highmem_pages = 0;
		}
		max_low_pfn = MAXMEM_PFN;
#ifndef CONFIG_HIGHMEM
		/* 最大可用内存是可直接寻址的 */
		// 没有配置CONFIG_HIGHMEM时，最多可用896MB内存
		printk(KERN_WARNING "Warning only %ldMB will be used.\n", MAXMEM>>20);
		if (max_pfn > MAX_NONPAE_PFN)				// MAX_NONPAE_PFN为1M
			printk(KERN_WARNING "Use a PAE enabled kernel.\n");
		else
			printk(KERN_WARNING "Use a HIGHMEM enabled kernel.\n");
		max_pfn = MAXMEM_PFN;			// 如果没配置highmem，则只能访问896MB物理内存
#else /* !CONFIG_HIGHMEM */
#ifndef CONFIG_X86_PAE
		if (max_pfn > MAX_NONPAE_PFN) {
			max_pfn = MAX_NONPAE_PFN;
			printk(KERN_WARNING "Warning only 4GB will be used.\n");
			printk(KERN_WARNING "Use a PAE enabled kernel.\n");
		}
#endif /* !CONFIG_X86_PAE */
#endif /* !CONFIG_HIGHMEM */
	} else {
		if (highmem_pages == -1)
			highmem_pages = 0;
#ifdef CONFIG_HIGHMEM
		if (highmem_pages >= max_pfn) {
			printk(KERN_ERR "highmem size specified (%uMB) is bigger than pages available (%luMB)!.\n", pages_to_mb(highmem_pages), pages_to_mb(max_pfn));
			highmem_pages = 0;
		}
		if (highmem_pages) {
			if (max_low_pfn-highmem_pages < 64*1024*1024/PAGE_SIZE){
				printk(KERN_ERR "highmem size %uMB results in smaller than 64MB lowmem, ignoring it.\n", pages_to_mb(highmem_pages));
				highmem_pages = 0;
			}
			max_low_pfn -= highmem_pages;
		}
#else
		if (highmem_pages)
			printk(KERN_ERR "ignoring highmem size on non-highmem kernel!\n");
#endif
	}
	return max_low_pfn;
}

#ifndef CONFIG_DISCONTIGMEM

/*
 * Free all available memory for boot time allocation.  Used
 * as a callback function by efi_memory_walk()
 */

static int __init
free_available_memory(unsigned long start, unsigned long end, void *arg)
{
	/* check max_low_pfn */
	if (start >= ((max_low_pfn + 1) << PAGE_SHIFT))
		return 0;
	if (end >= ((max_low_pfn + 1) << PAGE_SHIFT))
		end = (max_low_pfn + 1) << PAGE_SHIFT;
	if (start < end)
		free_bootmem(start, end - start);

	return 0;
}
/*
 * 使用 bootmem 分配器注册完全可用的低 RAM 页面。
 * 此函数的主要的功能是把内核可以使用的页框号（最大为max_low_pfn)在位图中对应的
 * 此函数把从1MB开始（HIGH_MEMORY定义为1024*1024)到位图结束所占用的页框号在位图中对应的位置1。
 */
static void __init register_bootmem_low_pages(unsigned long max_low_pfn)
{
	int i;

	if (efi_enabled) {
		efi_memmap_walk(free_available_memory, NULL);
		return;
	}
	for (i = 0; i < e820.nr_map; i++) {
		unsigned long curr_pfn, last_pfn, size;
		/*
		 * 保留可用的低内存
		 */
		if (e820.map[i].type != E820_RAM)
			continue;
		/*
		 * 我们对可用内存的起始地址进行四舍五入：
		 */
		curr_pfn = PFN_UP(e820.map[i].addr);
		if (curr_pfn >= max_low_pfn)
			continue;
		/*
		 * ...在可用范围的末尾向下：
		 */
		last_pfn = PFN_DOWN(e820.map[i].addr + e820.map[i].size);

		if (last_pfn > max_low_pfn)
			last_pfn = max_low_pfn;

		/*
		 * ..最后，是否所有的四舍五入和玩耍只是让该区域消失？
		 */
		if (last_pfn <= curr_pfn)
			continue;

		size = last_pfn - curr_pfn;
		free_bootmem(PFN_PHYS(curr_pfn), PFN_PHYS(size));
	}
}

/*
 * workaround for Dell systems that neglect to reserve EBDA
 */
static void __init reserve_ebda_region(void)
{
	unsigned int addr;
	addr = get_bios_ebda();
	if (addr)
		reserve_bootmem(addr, PAGE_SIZE);	
}

/* 配置连续内存时的定义
 * */
static unsigned long __init setup_memory(void)
{
	unsigned long bootmap_size, start_pfn, max_low_pfn;

	/*
	 * 部分使用的页面不可用 - 因此我们向上舍入：
	 */
	start_pfn = PFN_UP(init_pg_tables_end);     // init_pg_tables_end为_end+0x2000，即pg1后面

	find_max_pfn();     // 设置max_pfn，应当为0xfffff000~0xffffffff的页框号，即1M

	max_low_pfn = find_max_low_pfn();       // 设置max_low_pfn正常为896MB的页框号，修正max_pfn

#ifdef CONFIG_HIGHMEM
	highstart_pfn = highend_pfn = max_pfn;		// 1M页框号
	if (max_pfn > max_low_pfn) {
		highstart_pfn = max_low_pfn;
	}
	printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
		pages_to_mb(highend_pfn - highstart_pfn));
#endif
	printk(KERN_NOTICE "%ldMB LOWMEM available.\n",
			pages_to_mb(max_low_pfn));
	/*
	 * 初始化启动时间分配器（仅限低内存）:
	 * 页框号从0到0x38000的区域(896MB)的由bootmem管理,位图放在start_pfn的地址上
	 * bootmap_size表示位图的大小，单位字节
	 */
	bootmap_size = init_bootmem(start_pfn, max_low_pfn);

	/* register_bootmem_low_pages通过将位图中对应的比特位清零，
	 * 释放所有潜在可用的内存页。在IA-32系统上BIOS对该任务提供了支持，
	 * BIOS向内核提供了可用内存区的列表，即初始化过程中更早一点提供的e820映射.
	 * */
	register_bootmem_low_pages(max_low_pfn);

	/*
	 * 也保留bootmem位图本身。我们分两步执行此操作
	 * （第一步是 init_bootmem()），因为这会捕获（非常不可能的）
	 * 我们意外使用无效 RAM 区域初始化 bootmem 分配器的情况。
	 *
	 * 由于bootmem分配器需要一些内存页管理分配位图，必须首先调用reserve_bootmem分配这些内存页。
	 *
	 * 但还有一些其他的内存区已经在使用中，必须相应地标记出来。因此，还需要用reserve_bootmem注册相应的页。
	 *
	 * 需要注册的内存区的确切数目，高度依赖于内核配置。例如，需要保留0页，
	 * 因为在许多计算机上该页是一个特殊的BIOS页，有些特定于计算机的功能需要该页才能运作正常。
	 * 其他的reserve_bootmem调用则分配与内核配置相关的内存区，例如，用于ACPI数据或SMP启动时的配置。
	 */
	reserve_bootmem(HIGH_MEMORY, (PFN_PHYS(start_pfn) +
			 bootmap_size + PAGE_SIZE-1) - (HIGH_MEMORY));

	/*
	 * 保留物理页面0 - 它是许多机器上的特殊BIOS页面，可实现干净重启、SMP 操作、笔记本电脑功能。
	 */
	reserve_bootmem(0, PAGE_SIZE);

	/* 保留 EBDA 区域，这是一个 4K 区域*/
	reserve_ebda_region();

    /* could be an AMD 768MPX chipset. Reserve a page  before VGA to prevent
       PCI prefetch into it (errata #56). Usually the page is reserved anyways,
       unless you have no PS/2 mouse plugged in. */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD &&
	    boot_cpu_data.x86 == 6)
	     reserve_bootmem(0xa0000 - 4096, 4096);

#ifdef CONFIG_SMP
	/*
	 * But first pinch a few for the stack/trampoline stuff
	 * FIXME: Don't need the extra page at 4K, but need to fix
	 * trampoline before removing it. (see the GDT stuff)
	 */
	reserve_bootmem(PAGE_SIZE, PAGE_SIZE);
#endif
#ifdef CONFIG_ACPI_SLEEP
	/*
	 * Reserve low memory region for sleep support.
	 */
	acpi_reserve_bootmem();
#endif
#ifdef CONFIG_X86_FIND_SMP_CONFIG
	/*
	 * Find and reserve possible boot-time SMP configuration:
	 */
	find_smp_config();
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE <= (max_low_pfn << PAGE_SHIFT)) {
			reserve_bootmem(INITRD_START, INITRD_SIZE);
			initrd_start =
				INITRD_START ? INITRD_START + PAGE_OFFSET : 0;
			initrd_end = initrd_start+INITRD_SIZE;
		}
		else {
			printk(KERN_ERR "initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			    INITRD_START + INITRD_SIZE,
			    max_low_pfn << PAGE_SHIFT);
			initrd_start = 0;
		}
	}
#endif
	return max_low_pfn;
}
#else
extern unsigned long setup_memory(void);
#endif /* !CONFIG_DISCONTIGMEM */

/*
 * Request address space for all standard RAM and ROM resources
 * and also for regions reported as reserved by the e820.
 */
static void __init
legacy_init_iomem_resources(struct resource *code_resource, struct resource *data_resource)
{
	int i;

	probe_roms();
	for (i = 0; i < e820.nr_map; i++) {
		struct resource *res;
		if (e820.map[i].addr + e820.map[i].size > 0x100000000ULL)
			continue;
		res = alloc_bootmem_low(sizeof(struct resource));
		switch (e820.map[i].type) {
		case E820_RAM:	res->name = "System RAM"; break;
		case E820_ACPI:	res->name = "ACPI Tables"; break;
		case E820_NVS:	res->name = "ACPI Non-volatile Storage"; break;
		default:	res->name = "reserved";
		}
		res->start = e820.map[i].addr;
		res->end = res->start + e820.map[i].size - 1;
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		request_resource(&iomem_resource, res);
		if (e820.map[i].type == E820_RAM) {
			/*
			 *  We don't know which RAM region contains kernel data,
			 *  so we try it repeatedly and let the resource manager
			 *  test it.
			 */
			request_resource(res, code_resource);
			request_resource(res, data_resource);
		}
	}
}

/*
 * Request address space for all standard resources
 */
static void __init register_memory(void)
{
	unsigned long gapstart, gapsize;
	unsigned long long last;
	int	      i;

	if (efi_enabled)
		efi_initialize_iomem_resources(&code_resource, &data_resource);
	else
		legacy_init_iomem_resources(&code_resource, &data_resource);

	/* EFI systems may still have VGA */
	request_resource(&iomem_resource, &video_ram_resource);

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < STANDARD_IO_RESOURCES; i++)
		request_resource(&ioport_resource, &standard_io_resources[i]);

	/*
	 * Search for the bigest gap in the low 32 bits of the e820
	 * memory space.
	 */
	last = 0x100000000ull;
	gapstart = 0x10000000;
	gapsize = 0x400000;
	i = e820.nr_map;
	while (--i >= 0) {
		unsigned long long start = e820.map[i].addr;
		unsigned long long end = start + e820.map[i].size;

		/*
		 * Since "last" is at most 4GB, we know we'll
		 * fit in 32 bits if this condition is true
		 */
		if (last > end) {
			unsigned long gap = last - end;

			if (gap > gapsize) {
				gapsize = gap;
				gapstart = end;
			}
		}
		if (start < last)
			last = start;
	}

	/*
	 * Start allocating dynamic PCI memory a bit into the gap,
	 * aligned up to the nearest megabyte.
	 *
	 * Question: should we try to pad it up a bit (do something
	 * like " + (gapsize >> 3)" in there too?). We now have the
	 * technology.
	 */
	pci_mem_start = (gapstart + 0xfffff) & ~0xfffff;

	printk("Allocating PCI resources starting at %08lx (gap: %08lx:%08lx)\n",
		pci_mem_start, gapstart, gapsize);
}

/* Use inline assembly to define this because the nops are defined 
   as inline assembly strings in the include files and we cannot 
   get them easily into strings. */
asm("\t.data\nintelnops: " 
    GENERIC_NOP1 GENERIC_NOP2 GENERIC_NOP3 GENERIC_NOP4 GENERIC_NOP5 GENERIC_NOP6
    GENERIC_NOP7 GENERIC_NOP8); 
asm("\t.data\nk8nops: " 
    K8_NOP1 K8_NOP2 K8_NOP3 K8_NOP4 K8_NOP5 K8_NOP6
    K8_NOP7 K8_NOP8); 
asm("\t.data\nk7nops: " 
    K7_NOP1 K7_NOP2 K7_NOP3 K7_NOP4 K7_NOP5 K7_NOP6
    K7_NOP7 K7_NOP8); 
    
extern unsigned char intelnops[], k8nops[], k7nops[];
static unsigned char *intel_nops[ASM_NOP_MAX+1] = { 
     NULL,
     intelnops,
     intelnops + 1,
     intelnops + 1 + 2,
     intelnops + 1 + 2 + 3,
     intelnops + 1 + 2 + 3 + 4,
     intelnops + 1 + 2 + 3 + 4 + 5,
     intelnops + 1 + 2 + 3 + 4 + 5 + 6,
     intelnops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
}; 
static unsigned char *k8_nops[ASM_NOP_MAX+1] = { 
     NULL,
     k8nops,
     k8nops + 1,
     k8nops + 1 + 2,
     k8nops + 1 + 2 + 3,
     k8nops + 1 + 2 + 3 + 4,
     k8nops + 1 + 2 + 3 + 4 + 5,
     k8nops + 1 + 2 + 3 + 4 + 5 + 6,
     k8nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
}; 
static unsigned char *k7_nops[ASM_NOP_MAX+1] = { 
     NULL,
     k7nops,
     k7nops + 1,
     k7nops + 1 + 2,
     k7nops + 1 + 2 + 3,
     k7nops + 1 + 2 + 3 + 4,
     k7nops + 1 + 2 + 3 + 4 + 5,
     k7nops + 1 + 2 + 3 + 4 + 5 + 6,
     k7nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
}; 
static struct nop { 
     int cpuid; 
     unsigned char **noptable; 
} noptypes[] = { 
     { X86_FEATURE_K8, k8_nops }, 
     { X86_FEATURE_K7, k7_nops }, 
     { -1, NULL }
}; 

/* Replace instructions with better alternatives for this CPU type.

   This runs before SMP is initialized to avoid SMP problems with
   self modifying code. This implies that assymetric systems where
   APs have less capabilities than the boot processor are not handled. 
   In this case boot with "noreplacement". */ 
void apply_alternatives(void *start, void *end) 
{ 
	struct alt_instr *a; 
	int diff, i, k;
        unsigned char **noptable = intel_nops; 
	for (i = 0; noptypes[i].cpuid >= 0; i++) { 
		if (boot_cpu_has(noptypes[i].cpuid)) { 
			noptable = noptypes[i].noptable;
			break;
		}
	} 
	for (a = start; (void *)a < end; a++) { 
		if (!boot_cpu_has(a->cpuid))
			continue;
		BUG_ON(a->replacementlen > a->instrlen); 
		memcpy(a->instr, a->replacement, a->replacementlen); 
		diff = a->instrlen - a->replacementlen; 
		/* Pad the rest with nops */
		for (i = a->replacementlen; diff > 0; diff -= k, i += k) {
			k = diff;
			if (k > ASM_NOP_MAX)
				k = ASM_NOP_MAX;
			memcpy(a->instr + i, noptable[k], k); 
		} 
	}
} 

static int no_replacement __initdata = 0; 
 
void __init alternative_instructions(void)
{
	extern struct alt_instr __alt_instructions[], __alt_instructions_end[];
	if (no_replacement) 
		return;
	apply_alternatives(__alt_instructions, __alt_instructions_end);
}

static int __init noreplacement_setup(char *s)
{ 
     no_replacement = 1; 
     return 0; 
} 

__setup("noreplacement", noreplacement_setup); 

static char * __init machine_specific_memory_setup(void);

#ifdef CONFIG_MCA
static void set_mca_bus(int x)
{
	MCA_bus = x;
}
#else
static void set_mca_bus(int x) { }
#endif

/*
 * 确定我们是否由EFI加载程序加载。如果是这样，那么我们也已经
 * 传递了efi memmap，systab等，因此我们应该使用这些数据结构
 * 进行初始化。注意，efi初始化代码路径由全局efi_enabled确定。
 * 这允许在现有系统（使用传统BIOS）以及EFI系统上使用相同的内核映像。
 */
void __init setup_arch(char **cmdline_p)
{
	unsigned long max_low_pfn;

	memcpy(&boot_cpu_data, &new_cpu_data, sizeof(new_cpu_data));
	pre_setup_arch_hook();
	early_cpu_init();

	/*
	 * FIXME: This isn't an official loader_type right
	 * now but does currently work with elilo.
	 * If we were configured as an EFI kernel, check to make
	 * sure that we were loaded correctly from elilo and that
	 * the system table is valid.  If not, then initialize normally.
	 */
#ifdef CONFIG_EFI
	if ((LOADER_TYPE == 0x50) && EFI_SYSTAB)
		efi_enabled = 1;
#endif

 	ROOT_DEV = old_decode_dev(ORIG_ROOT_DEV);
 	drive_info = DRIVE_INFO;
 	screen_info = SCREEN_INFO;
	edid_info = EDID_INFO;
	apm_info.bios = APM_BIOS_INFO;
	ist_info = IST_INFO;
	saved_videomode = VIDEO_MODE;
	if( SYS_DESC_TABLE.length != 0 ) {
		set_mca_bus(SYS_DESC_TABLE.table[3] & 0x2);
		machine_id = SYS_DESC_TABLE.table[0];
		machine_submodel_id = SYS_DESC_TABLE.table[1];
		BIOS_revision = SYS_DESC_TABLE.table[2];
	}
	aux_device_present = AUX_DEVICE_INFO;
	bootloader_type = LOADER_TYPE;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif
	ARCH_SETUP
	if (efi_enabled)
		efi_init();
	else {
		printk(KERN_INFO "BIOS-provided physical RAM map:\n");
		/* machine_specific_memory_setup处理特定与机器的内存设置。
		 * 创建一个列表，包括系统占据的内存区和空闲内存区
		 */
		print_memory_map(machine_specific_memory_setup());      // 正常打印"BIOS-e820"
	}

	copy_edd();

	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = init_pg_tables_end + PAGE_OFFSET;

	code_resource.start = virt_to_phys(_text);
	code_resource.end = virt_to_phys(_etext)-1;
	data_resource.start = virt_to_phys(_etext);
	data_resource.end = virt_to_phys(_edata)-1;

	/* 分析命令行，主要关注类似mem=XXX[KkmM]、highmem=XXX[kKmM]或memmap=XXX[KkmM]" "@XXX[KkmM]之类的参数
	 * */
	parse_cmdline_early(cmdline_p);

	/* 计算物理内存数量小于896 MiB的系统上内存页的数目.
	 * 该函数有两个版本。一个用于连续内存系统（在arch/x86/kernel/setup.c），另一个用于不连续内存系统（在arch/x86/mm/discontig.c）。
	 * 尽管实现不同，但二者的效果相同。
	 * 1. 确定（每个结点）可用的物理内存页的数目。
	 * 2. 初始化bootmem分配器。
	 * 3. 接下来分配各种内存区，例如，运行第一个用户空间过程所需的最初的RAM磁盘。
	 * */
	max_low_pfn = setup_memory()

	/*
	 * NOTE: before this point _nobody_ is allowed to allocate
	 * any memory using the bootmem allocator.  Although the
	 * alloctor is now initialised only the first 8Mb of the kernel
	 * virtual address space has been mapped.  All allocations before
	 * paging_init() has completed must use the alloc_bootmem_low_pages()
	 * variant (which allocates DMA'able memory) and care must be taken
	 * not to exceed the 8Mb limit.
	 */

#ifdef CONFIG_SMP
	smp_alloc_memory(); /* AP processor realmode stacks in low memory*/
#endif
	paging_init();          // 初始化内核页表并启用内存分页，因为IA-32计算机上默认情况下分页是禁用的。

	/*
	 * NOTE: at this point the bootmem allocator is fully available.
	 */

#ifdef CONFIG_EARLY_PRINTK
	{
		char *s = strstr(*cmdline_p, "earlyprintk=");
		if (s) {
			extern void setup_early_printk(char *);

			setup_early_printk(s);
			printk("early console enabled\n");
		}
	}
#endif


	dmi_scan_machine();

#ifdef CONFIG_X86_GENERICARCH
	generic_apic_probe(*cmdline_p);
#endif	
	if (efi_enabled)
		efi_map_memmap();

	/*
	 * Parse the ACPI tables for possible boot-time SMP configuration.
	 */
	acpi_boot_table_init();
	acpi_boot_init();

#ifdef CONFIG_X86_LOCAL_APIC
	if (smp_found_config)
		get_smp_config();
#endif

	register_memory();

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	if (!efi_enabled || (efi_mem_type(0xa0000) != EFI_CONVENTIONAL_MEMORY))
		conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
}

#include "setup_arch_post.h"
/*
 * Local Variables:
 * mode:c
 * c-file-style:"k&r"
 * c-basic-offset:8
 * End:
 */
