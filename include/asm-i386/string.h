#ifndef _I386_STRING_H_
#define _I386_STRING_H_

#ifdef __KERNEL__
#include <linux/config.h>
/*
 * On a 486 or Pentium, we are better off not using the
 * byte string operations. But on a 386 or a PPro the
 * byte string ops are faster than doing it by hand
 * (MUCH faster on a Pentium).
 */

/*
 * 此字符串包含将所有字符串函数定义为内联函数。使用 gcc。
 * 它还假设 ds=es=data 空间，这应该是正常的。
 * 大多数字符串函数都经过大量手工优化，尤其是 strsep,strstr,str[c]spn。
 * 它们应该可以工作，但不是很容易理解。一切都完全在寄存器组内完成，
 * 使功能快速而干净。 字符串指令一直被使用，导致“稍微”不清楚的代码 :-)
 *
 *		NO Copyright (C) 1991, 1992 Linus Torvalds,
 *		consider these trivial functions to be PD.
 */

/* AK: in fact I bet it would be better to move this stuff all out of line.
 */
/* 内核中各处都会处理字符串，因而对字符串处理的时间要求很严格。
 * 由于很多体系结构都提供了专门的汇编指令来执行所需的任务，
 * 或者由于手工优化的汇编代码可能比编译器生成的代码更为快速，
 * 因此所有体系结构在<asm-arch/string.h>中都定义了自身的各种字符串操作。
 *
 * 所有这些操作，都是用来替换用户空间中所用的C标准库的同名函数，
 * 以便在内核中执行同样的任务。对于每个由体系结构自身以优化
 * 形式定义的字符串操作来说，都必须定义相应的__HAVE_ARCH_OPERATION宏。
 * 例如，对memcpy必须设置__HAVE_ARCH_MEMCPY。体系结构相关代码
 * 未能实现的所有函数，都替换为lib/string.c中实现的体系结构无关的标准操作。
 * */

/*
 * (1) lodsb、lodsw、lodsl：把DS:SI指向的存储单元中的数据装入AL或AX或EAX，然后根据DF标志增减SI
 * (2) stosb、stosw、stosl：把AL或AX或EAX中的数据装入ES:DI指向的存储单元，然后根据DF标志增减DI
 * (3) movsb、movsw、movsl：把DS:SI指向的存储单元中的数据装入ES:DI指向的存储单元中，然后根据DF标志分别增减SI和DI
 * (4) scasb、scasw、scasl：把AL或AX或EAX中的数据与ES:DI指向的存储单元中的数据相减，影响标志位，然后根据DF标志分别增减SI和DI
 * (5) cmpsb、cmpsw、cmpsl：把DS:SI指向的存储单元中的数据与ES:DI指向的存储单元中的数据相减，影响标志位，然后根据DF标志分别增减SI和DI
 * (6) rep：重复其后的串操作指令。重复前先判断CX是否为0，为0就结束重复，否则CX减1，重复其后的串操作指令。主要用在MOVS和STOS前。一般不用在LODS前。
 *
 * 上述指令涉及的寄存器：段寄存器DS和ES、变址寄存器SI和DI、累加器AX、计数器CX涉及
 * 的标志位：DF、AF、CF、OF、PF、SF、ZF
 * */

#define __HAVE_ARCH_STRCPY
static inline char * strcpy(char * dest,const char *src)
{
int d0, d1, d2;
__asm__ __volatile__(
	"1:\tlodsb\n\t"
	"stosb\n\t"
	"testb %%al,%%al\n\t"		// 检查是否遇到'\0'
	"jne 1b"
	: "=&S" (d0), "=&D" (d1), "=&a" (d2)
	:"0" (src),"1" (dest) : "memory");
return dest;
}

#define __HAVE_ARCH_STRNCPY
static inline char * strncpy(char * dest,const char *src,size_t count)
{
int d0, d1, d2, d3;
__asm__ __volatile__(
	"1:\tdecl %2\n\t"
	"js 2f\n\t"
	"lodsb\n\t"
	"stosb\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n\t"
	"rep\n\t"
	"stosb\n"
	"2:"
	: "=&S" (d0), "=&D" (d1), "=&c" (d2), "=&a" (d3)
	:"0" (src),"1" (dest),"2" (count) : "memory");
return dest;
}

#define __HAVE_ARCH_STRCAT
static inline char * strcat(char * dest,const char * src)
{
int d0, d1, d2, d3;
__asm__ __volatile__(
	"repne\n\t"
	"scasb\n\t"
	"decl %1\n"
	"1:\tlodsb\n\t"
	"stosb\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b"
	: "=&S" (d0), "=&D" (d1), "=&a" (d2), "=&c" (d3)
	: "0" (src), "1" (dest), "2" (0), "3" (0xffffffffu):"memory");
return dest;
}

#define __HAVE_ARCH_STRNCAT
static inline char * strncat(char * dest,const char * src,size_t count)
{
int d0, d1, d2, d3;
__asm__ __volatile__(
	"repne\n\t"
	"scasb\n\t"
	"decl %1\n\t"
	"movl %8,%3\n"
	"1:\tdecl %3\n\t"
	"js 2f\n\t"
	"lodsb\n\t"
	"stosb\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n"
	"2:\txorl %2,%2\n\t"
	"stosb"
	: "=&S" (d0), "=&D" (d1), "=&a" (d2), "=&c" (d3)
	: "0" (src),"1" (dest),"2" (0),"3" (0xffffffffu), "g" (count)
	: "memory");
return dest;
}

#define __HAVE_ARCH_STRCMP
static inline int strcmp(const char * cs,const char * ct)
{
int d0, d1;
register int __res;
__asm__ __volatile__(
	"1:\tlodsb\n\t"
	"scasb\n\t"
	"jne 2f\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n\t"
	"xorl %%eax,%%eax\n\t"
	"jmp 3f\n"
	"2:\tsbbl %%eax,%%eax\n\t"
	"orb $1,%%al\n"
	"3:"
	:"=a" (__res), "=&S" (d0), "=&D" (d1)
		     :"1" (cs),"2" (ct));
return __res;
}

#define __HAVE_ARCH_STRNCMP
static inline int strncmp(const char * cs,const char * ct,size_t count)
{
register int __res;
int d0, d1, d2;
__asm__ __volatile__(
	"1:\tdecl %3\n\t"
	"js 2f\n\t"
	"lodsb\n\t"
	"scasb\n\t"
	"jne 3f\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n"
	"2:\txorl %%eax,%%eax\n\t"
	"jmp 4f\n"
	"3:\tsbbl %%eax,%%eax\n\t"
	"orb $1,%%al\n"
	"4:"
		     :"=a" (__res), "=&S" (d0), "=&D" (d1), "=&c" (d2)
		     :"1" (cs),"2" (ct),"3" (count));
return __res;
}

#define __HAVE_ARCH_STRCHR
static inline char * strchr(const char * s, int c)
{
int d0;
register char * __res;
__asm__ __volatile__(
	"movb %%al,%%ah\n"
	"1:\tlodsb\n\t"
	"cmpb %%ah,%%al\n\t"
	"je 2f\n\t"
	"testb %%al,%%al\n\t"
	"jne 1b\n\t"
	"movl $1,%1\n"
	"2:\tmovl %1,%0\n\t"
	"decl %0"
	:"=a" (__res), "=&S" (d0) : "1" (s),"0" (c));
return __res;
}

#define __HAVE_ARCH_STRRCHR
static inline char * strrchr(const char * s, int c)
{
int d0, d1;
register char * __res;
__asm__ __volatile__(
	"movb %%al,%%ah\n"
	"1:\tlodsb\n\t"
	"cmpb %%ah,%%al\n\t"
	"jne 2f\n\t"
	"leal -1(%%esi),%0\n"
	"2:\ttestb %%al,%%al\n\t"
	"jne 1b"
	:"=g" (__res), "=&S" (d0), "=&a" (d1) :"0" (0),"1" (s),"2" (c));
return __res;
}

#define __HAVE_ARCH_STRLEN
static inline size_t strlen(const char * s)
{
int d0;
register int __res;
__asm__ __volatile__(
	"repne\n\t"
	"scasb\n\t"
	"notl %0\n\t"
	"decl %0"
	:"=c" (__res), "=&D" (d0) :"1" (s),"a" (0), "0" (0xffffffffu));
return __res;
}

/*
 * 32位模式下：
 * 源地址是DS:ESI,目的地址是ES:EDI			应当把源数据和目标地址放在这两个寄存器中
 *
 * 注意：在传送完成之后，ESI和EDI会增加或者减小。
 * CLD 当DF=0 时，表示正向传送，传送之后ESI和EDI的值会增加；
 * STD 当DF=1 时，表示反向传送，传送之后ESI和EDI的值会减小；
 *
 * 他们的区别是：ESI和EDI的加减想象成有一个指针
 * MOVSB:传送一个字节，之后ESI和EDI加/减1，
 * MOVSW:传送一个字，之后ESI和EDI加/减2
 * MOVSL(INTEL使用MOVSD):传送一个双字，之后ESI和EDI加/减4
 *
 * 单纯的movsb/ movsw/ movsl只能执行一次，
 * 如果希望处理器自动地反复执行，可以加上指令前缀"rep;"
 * 在寄存器ECX中设置传送的次数。
 * 当ECX不等于0时，则执行movsb/ movsw/ movsl,
 * 执行后，ECX的值减一，直到减为0为止。
 *
 * 例如，ESI正向移动(加)时，"hello world"，执行 movsb/ movsw/ movsl
 * 输出："h","hel","hello w"
 *
 * 反向移动时，ESI要指向"hello world"的尾端，EDI要指向存储空间的尾端
 * 执行 movsb/ movsw/ movsl
 * 输出："\n","d\n","rld\n"
 *
 * 注意上面两种方式区别，默认正向
 * */

static inline void * __memcpy(void * to, const void * from, size_t n)
{
int d0, d1, d2;
__asm__ __volatile__(
	"rep ; movsl\n\t"
	"testb $2,%b4\n\t"		// %b4，4是第4个参数n，b是指取该寄存器的1字节大小
	"je 1f\n\t"		// 如果n的第1字节的第2为等于1，那至少还有2字节
	"movsw\n"
	"1:\ttestb $1,%b4\n\t"		// 还剩1字节
	"je 2f\n\t"
	"movsb\n"
	"2:"
	// "c"即ECX，rep的计数器，"D"即EDI，目标变址寄存器，"S"即ESI，源变址寄存器
	: "=&c" (d0), "=&D" (d1), "=&S" (d2)
	//q 表示为eax ebx ecx edx；"0"指代"=&c"， "1"指代"=&D"， "2"指代"=&S"
	:"0" (n/4), "q" (n),"1" ((long) to),"2" ((long) from)
	: "memory");
return (to);
}

/*
 * 这看起来非常难看，但编译器可以完全优化它，因为计数是恒定的。
 */
static inline void * __constant_memcpy(void * to, const void * from, size_t n)
{
	if (n <= 128)
		return __builtin_memcpy(to, from, n);

#define COMMON(x) \
__asm__ __volatile__( \
	"rep ; movsl" \
	x \
	: "=&c" (d0), "=&D" (d1), "=&S" (d2) \
	: "0" (n/4),"1" ((long) to),"2" ((long) from) \
	: "memory");
{
	int d0, d1, d2;
	switch (n % 4) {		// 取余
		case 0: COMMON(""); return to;
		case 1: COMMON("\n\tmovsb"); return to;
		case 2: COMMON("\n\tmovsw"); return to;
		default: COMMON("\n\tmovsw\n\tmovsb"); return to;
	}
}
  
#undef COMMON
}

#define __HAVE_ARCH_MEMCPY

#ifdef CONFIG_X86_USE_3DNOW		// 架构特定实现

#include <asm/mmx.h>

/*
 *	该 CPU 强烈支持 3DNow（例如 AMD Athlon）
 */

static inline void * __constant_memcpy3d(void * to, const void * from, size_t len)
{
	if (len < 512)
		return __constant_memcpy(to, from, len);
	return _mmx_memcpy(to, from, len);
}

static __inline__ void *__memcpy3d(void *to, const void *from, size_t len)
{
	if (len < 512)
		return __memcpy(to, from, len);
	return _mmx_memcpy(to, from, len);
}

#define memcpy(t, f, n) \
(__builtin_constant_p(n) ? \
 __constant_memcpy3d((t),(f),(n)) : \
 __memcpy3d((t),(f),(n)))

#else

/*
 *	No 3D Now!
 */
 
#define memcpy(t, f, n) \
(__builtin_constant_p(n) ? \		// n是否为编译期常量，是常量的话，函数返回1，否则函数返回0
 __constant_memcpy((t),(f),(n)) : \
 __memcpy((t),(f),(n)))

#endif

#define __HAVE_ARCH_MEMMOVE
void *memmove(void * dest,const void * src, size_t n);

#define memcmp __builtin_memcmp

#define __HAVE_ARCH_MEMCHR
static inline void * memchr(const void * cs,int c,size_t count)
{
int d0;
register void * __res;
if (!count)
	return NULL;
__asm__ __volatile__(
	"repne\n\t"
	"scasb\n\t"
	"je 1f\n\t"
	"movl $1,%0\n"
	"1:\tdecl %0"
	:"=D" (__res), "=&c" (d0) : "a" (c),"0" (cs),"1" (count));
return __res;
}

static inline void * __memset_generic(void * s, char c,size_t count)
{
int d0, d1;
__asm__ __volatile__(
	"rep\n\t"
	"stosb"
	: "=&c" (d0), "=&D" (d1)
	:"a" (c),"1" (s),"0" (count)
	:"memory");
return s;
}

/* we might want to write optimized versions of these later */
#define __constant_count_memset(s,c,count) __memset_generic((s),(c),(count))

/*
 * memset(x,0,y) is a reasonably common thing to do, so we want to fill
 * things 32 bits at a time even when we don't know the size of the
 * area at compile-time..
 */
static inline void * __constant_c_memset(void * s, unsigned long c, size_t count)
{
int d0, d1;
__asm__ __volatile__(
	"rep ; stosl\n\t"
	"testb $2,%b3\n\t"
	"je 1f\n\t"
	"stosw\n"
	"1:\ttestb $1,%b3\n\t"
	"je 2f\n\t"
	"stosb\n"
	"2:"
	: "=&c" (d0), "=&D" (d1)
	:"a" (c), "q" (count), "0" (count/4), "1" ((long) s)
	:"memory");
return (s);	
}

/* Added by Gertjan van Wingerde to make minix and sysv module work */
#define __HAVE_ARCH_STRNLEN
static inline size_t strnlen(const char * s, size_t count)
{
int d0;
register int __res;
__asm__ __volatile__(
	"movl %2,%0\n\t"
	"jmp 2f\n"
	"1:\tcmpb $0,(%0)\n\t"
	"je 3f\n\t"
	"incl %0\n"
	"2:\tdecl %1\n\t"
	"cmpl $-1,%1\n\t"
	"jne 1b\n"
	"3:\tsubl %2,%0"
	:"=a" (__res), "=&d" (d0)
	:"c" (s),"1" (count));
return __res;
}
/* end of additional stuff */

#define __HAVE_ARCH_STRSTR

extern char *strstr(const char *cs, const char *ct);

/*
 * This looks horribly ugly, but the compiler can optimize it totally,
 * as we by now know that both pattern and count is constant..
 */
static inline void * __constant_c_and_count_memset(void * s, unsigned long pattern, size_t count)
{
	switch (count) {
		case 0:
			return s;
		case 1:
			*(unsigned char *)s = pattern;
			return s;
		case 2:
			*(unsigned short *)s = pattern;
			return s;
		case 3:
			*(unsigned short *)s = pattern;
			*(2+(unsigned char *)s) = pattern;
			return s;
		case 4:
			*(unsigned long *)s = pattern;
			return s;
	}
#define COMMON(x) \
__asm__  __volatile__( \
	"rep ; stosl" \
	x \
	: "=&c" (d0), "=&D" (d1) \
	: "a" (pattern),"0" (count/4),"1" ((long) s) \
	: "memory")
{
	int d0, d1;
	switch (count % 4) {
		case 0: COMMON(""); return s;
		case 1: COMMON("\n\tstosb"); return s;
		case 2: COMMON("\n\tstosw"); return s;
		default: COMMON("\n\tstosw\n\tstosb"); return s;
	}
}
  
#undef COMMON
}

#define __constant_c_x_memset(s, c, count) \
(__builtin_constant_p(count) ? \
 __constant_c_and_count_memset((s),(c),(count)) : \
 __constant_c_memset((s),(c),(count)))

#define __memset(s, c, count) \
(__builtin_constant_p(count) ? \
 __constant_count_memset((s),(c),(count)) : \
 __memset_generic((s),(c),(count)))

#define __HAVE_ARCH_MEMSET
#define memset(s, c, count) \
(__builtin_constant_p(c) ? \
 __constant_c_x_memset((s),(0x01010101UL*(unsigned char)(c)),(count)) : \
 __memset((s),(c),(count)))

/*
 * find the first occurrence of byte 'c', or 1 past the area if none
 */
#define __HAVE_ARCH_MEMSCAN
static inline void * memscan(void * addr, int c, size_t size)
{
	if (!size)
		return addr;
	__asm__("repnz; scasb\n\t"
		"jnz 1f\n\t"
		"dec %%edi\n"
		"1:"
		: "=D" (addr), "=c" (size)
		: "0" (addr), "1" (size), "a" (c));
	return addr;
}

#endif /* __KERNEL__ */

#endif
