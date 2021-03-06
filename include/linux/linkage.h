#ifndef _LINUX_LINKAGE_H
#define _LINUX_LINKAGE_H

#include <linux/config.h>
#include <asm/linkage.h>

#ifdef __cplusplus
#define CPP_ASMLINKAGE extern "C"
#else
#define CPP_ASMLINKAGE
#endif

#ifndef asmlinkage
#define asmlinkage CPP_ASMLINKAGE
#endif

#ifndef prevent_tail_call
# define prevent_tail_call(ret) do { } while (0)
#endif

#ifndef __ALIGN
#define __ALIGN		.align 4,0x90		// 对齐的空间用0x90 nop填充
#define __ALIGN_STR	".align 4,0x90"
#endif

#ifdef __ASSEMBLY__

#define ALIGN __ALIGN
#define ALIGN_STR __ALIGN_STR

// 内核控制路径定义
#define ENTRY(name) \
  .globl name; \
  ALIGN; \
  name:

#endif

#define NORET_TYPE    /**/
#define ATTRIB_NORET  __attribute__((noreturn))
#define NORET_AND     noreturn,

#ifndef FASTCALL
#define FASTCALL(x)	x
#define fastcall
#endif

#endif
