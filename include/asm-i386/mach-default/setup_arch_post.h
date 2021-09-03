/**
 * machine_specific_memory_setup - 挂钩机器特定的内存设置。
 *
 * Description:
 *	This is included late in kernel/setup.c so that it can make
 *	use of all of the static functions.
 **/

static char * __init machine_specific_memory_setup(void)
{
	char *who;


	who = "BIOS-e820";

	/*
	 * 尝试复制 BIOS 提供的 E820-map。
	 *
	 * 否则伪造内存映射；从 0k->640k 的一段，
	 * 下一部分从 1mb->property_mem_k
	 *
	 * 首先调用sanitize_e820_map函数将重叠的去除,
	 * E820_MAP为((struct e820entry *) (PARAM+E820MAP))
	 * E820_MAP_NR为(*(char*) (PARAM+E820NR))
	 *
	 *
	 *  BIOS-e820: 0000000000000000 - 000000000009fc00 (usable)     0KB-639KB
     *  BIOS-e820: 000000000009fc00 - 00000000000a0000 (reserved)   639KB-640KB
     *  BIOS-e820: 00000000000f0000 - 0000000000100000 (reserved)   960KB-1MB
     *  BIOS-e820: 0000000000100000 - 000000007ffdc000 (usable)     1MB-2047MB
     *  BIOS-e820: 000000007ffdc000 - 0000000080000000 (reserved)   2047MB-2048MB
     *  BIOS-e820: 00000000feffc000 - 00000000ff000000 (reserved)   4079MB-4080MB
     *  BIOS-e820: 00000000fffc0000 - 0000000100000000 (reserved)   4080MB-4096MB
     *  e820 update range: 0000000000000000 - 0000000000001000 (usable) ==> (reserved)
     *  e820 remove range: 00000000000a0000 - 0000000000100000 (usable)
	 *
	 */
	/* 对e820map排序和处理重叠区域，E820_MAP
	 * E820_MAP指向存放e820 map的地址，处理后的结果也会放到这个地址上
	 * E820_MAP_NR执行记录的数量，处理后修改这个值
	 * */
	sanitize_e820_map(E820_MAP, &E820_MAP_NR);
	if (copy_e820_map(E820_MAP, E820_MAP_NR) < 0) {     // 将E820图copy到struct e820map结构中，正常返回0
		unsigned long mem_size;

		/* 比较其他方法的结果并取更大的 */
		if (ALT_MEM_K < EXT_MEM_K) {
			mem_size = EXT_MEM_K;
			who = "BIOS-88";
		} else {
			mem_size = ALT_MEM_K;
			who = "BIOS-e801";
		}

		e820.nr_map = 0;
		add_memory_region(0, LOWMEMSIZE(), E820_RAM);
		add_memory_region(HIGH_MEMORY, mem_size << 10, E820_RAM);
  	}
	return who;
}
