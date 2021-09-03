/*
 * int 15, ax=e820 内存映射方案的结构和定义。
 *
 * 简而言之，arch/i386/bootsetup.S 在empty_zero_block中填充了一个临时表，
 * 其中包含一个可用地址大小双元组的列表。在 arch/i386/kernel/setup.c 中，
 * 此信息被传输到 e820map 中，而在 arch/i386/mm/init.c 中，
 * 该新信息用于标记页面是否保留。
 *
 */
#ifndef __E820_HEADER
#define __E820_HEADER

#define E820MAP	0x2d0		/* our map BIOS的0x15中断的返回的结果从该地址开始存储*/
#define E820MAX	32		/* number of entries in E820MAP 最多32条记录*/
#define E820NR	0x1e8		/* # entries in E820MAP 当前的记录索引保存在该地址上*/

// 4种内存区域
#define E820_RAM	1
#define E820_RESERVED	2
#define E820_ACPI	3 /* usable as RAM once ACPI tables have been read */
#define E820_NVS	4

#define HIGH_MEMORY	(1024*1024)

#ifndef __ASSEMBLY__
// e820图，BIOS返回的数据将会填入该结构中
struct e820map {
    int nr_map;     // nr_map是内存段的实际数量
    struct e820entry {
    // 下面3个字段共20字节，对应每条记录20字节
	unsigned long long addr;	/* start of memory segment addr字段表示内存段的起始地址*/
	unsigned long long size;	/* size of memory segment size字段表示内存段的大小*/
	unsigned long type;		/* type of memory segment type表示内存段的类型，比如E820_RAM表示可用内存*/
    } map[E820MAX];     // 每个内存段由struct e820entry表示,E820MAX是一个宏，为32，说明最多可以有32个内存段
};

extern struct e820map e820;
#endif/*!__ASSEMBLY__*/

#endif/*__E820_HEADER*/
