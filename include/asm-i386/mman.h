#ifndef __I386_MMAN_H__
#define __I386_MMAN_H__

// mmap prot
#define PROT_READ	0x1		/* page can be read */
#define PROT_WRITE	0x2		/* page can be written */
#define PROT_EXEC	0x4		/* page can be executed */
#define PROT_SEM	0x8		/* page may be used for atomic ops */
#define PROT_NONE	0x0		/* page can not be accessed */
#define PROT_GROWSDOWN	0x01000000	/* mprotect flag: extend change to start of growsdown vma */
#define PROT_GROWSUP	0x02000000	/* mprotect flag: extend change to end of growsup vma */

// mmap flag
#define MAP_SHARED	0x01		/* 共享映射 Share changes */
#define MAP_PRIVATE	0x02		/* 私有映射 Changes are private */
#define MAP_TYPE	0x0f		/* Mask for type of mapping */
#define MAP_FIXED	0x10		/* 使用指定的地址 Interpret addr exactly */
#define MAP_ANONYMOUS	0x20		/* 匿名映射 don't use a file */

#define MAP_GROWSDOWN	0x0100		/* stack-like segment */
#define MAP_DENYWRITE	0x0800		/* ETXTBSY，该标志在用户空间中被忽略 */
#define MAP_EXECUTABLE	0x1000		/* mark it as an executable，该标志在用户空间中被忽略 */
#define MAP_LOCKED	0x2000		/* pages are locked */
#define MAP_NORESERVE	0x4000		/* don't check for reservations */
#define MAP_POPULATE	0x8000		/* populate (prefault) pagetables */
#define MAP_NONBLOCK	0x10000		/* do not block on IO */

#define MS_ASYNC	1		/* sync memory asynchronously */
#define MS_INVALIDATE	2		/* invalidate the caches */
#define MS_SYNC		4		/* synchronous memory sync */

#define MCL_CURRENT	1		/* lock all current mappings */
#define MCL_FUTURE	2		/* lock all future mappings */

#define MADV_NORMAL	0x0		/* default page-in behavior */
#define MADV_RANDOM	0x1		/* page-in minimum required */
#define MADV_SEQUENTIAL	0x2		/* read-ahead aggressively */
#define MADV_WILLNEED	0x3		/* pre-fault pages */
#define MADV_DONTNEED	0x4		/* discard these pages */

/* compatibility flags */
#define MAP_ANON	MAP_ANONYMOUS
#define MAP_FILE	0

#endif /* __I386_MMAN_H__ */
