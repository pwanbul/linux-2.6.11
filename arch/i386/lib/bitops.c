#include <linux/bitops.h>
#include <linux/module.h>

/**
 * find_next_bit - find the first set bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The maximum size to search
 */
int find_next_bit(const unsigned long *addr, int size, int offset)
{
	const unsigned long *p = addr + (offset >> 5);
	int set = 0, bit = offset & 31, res;

	if (bit) {
		/*
		 * Look for nonzero in the first 32 bits:
		 */
		__asm__("bsfl %1,%0\n\t"
			"jne 1f\n\t"
			"movl $32, %0\n"
			"1:"
			: "=r" (set)
			: "r" (*p >> bit));
		if (set < (32 - bit))
			return set + offset;
		set = 32 - bit;
		p++;
	}
	/*
	 * No set bit yet, search remaining full words for a bit
	 */
	res = find_first_bit (p, size - 32 * (p - addr));
	return (offset + set + res);
}
EXPORT_SYMBOL(find_next_bit);

/**
 * find_next_zero_bit - 找到内存区域的第一个零位
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The maximum size to search
 *
 * addr为文件描述符位图的地址，size是位图的大小，offset是上一次分配的fd+1
 */
int find_next_zero_bit(const unsigned long *addr, int size, int offset)
{
	unsigned long * p = ((unsigned long *) addr) + (offset >> 5);		// 从上一次分配的地方找起
	int set = 0, bit = offset & 31, res;		// 32的二进制1 1111

	// p为数组元素的指针，bit是元素中偏移量
	if (bit) {
		/*
		 * 在前 32 位中查找零。
		 */
		__asm__("bsfl %1,%0\n\t"
			"jne 1f\n\t"
			"movl $32, %0\n"
			"1:"
			: "=r" (set)
			: "r" (~(*p >> bit)));
		if (set < (32 - bit))
			return set + offset;
		set = 32 - bit;
		p++;
	}
	/*
	 * 还没有零，搜索剩余的完整字节以获取零
	 */
	res = find_first_zero_bit (p, size - 32 * (p - (unsigned long *) addr));
	return (offset + set + res);
}
EXPORT_SYMBOL(find_next_zero_bit);
