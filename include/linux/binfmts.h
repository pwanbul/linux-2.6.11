#ifndef _LINUX_BINFMTS_H
#define _LINUX_BINFMTS_H

#include <linux/capability.h>

struct pt_regs;

/*
 * MAX_ARG_PAGES 定义为新程序的参数和信封分配的页数。
 * 32应该就足够了，这给出了 128kB w/4KB 页面的最大 env+arg！
 */
#define MAX_ARG_PAGES 32		// 命令行参数和环境变量所占用的页面数

/* sizeof(linux_binprm->buf) */
#define BINPRM_BUF_SIZE 128

#ifdef __KERNEL__

/*
 * 此结构用于保存加载二进制文件时使用的参数。
 * 打包二进制程序的参数
 */
struct linux_binprm{
	char buf[BINPRM_BUF_SIZE];      // 保存可执行文件的头128字节
	struct page *page[MAX_ARG_PAGES];       // 参数页
	struct mm_struct *mm;
	unsigned long p; /* current top of mem 当前内存页最高地址*/
	int sh_bang;
	struct file * file;
	int e_uid, e_gid;
	kernel_cap_t cap_inheritable, cap_permitted, cap_effective;		// 权能
	void *security;
	int argc, envc;
	char * filename;	/* Name of binary as seen by procps */
	char * interp;		/* Name of the binary really executed. Most
				   of the time same as filename, but could be
				   different for binfmt_{misc,script} */
	unsigned interp_flags;
	unsigned interp_data;
	unsigned long loader, exec;
};

#define BINPRM_FLAGS_ENFORCE_NONDUMP_BIT 0
#define BINPRM_FLAGS_ENFORCE_NONDUMP (1 << BINPRM_FLAGS_ENFORCE_NONDUMP_BIT)

/* fd of the binary should be passed to the interpreter */
#define BINPRM_FLAGS_EXECFD_BIT 1
#define BINPRM_FLAGS_EXECFD (1 << BINPRM_FLAGS_EXECFD_BIT)


/*
 * 此结构定义了用于加载 linux 接受的二进制格式的函数。
 * 二进制格式支持，如elf,a.out等
 */
struct linux_binfmt {
	struct linux_binfmt * next;
	struct module *module;
	int (*load_binary)(struct linux_binprm *, struct  pt_regs * regs);
	int (*load_shlib)(struct file *);
	int (*core_dump)(long signr, struct pt_regs * regs, struct file * file);
	unsigned long min_coredump;	/* minimal dump size */
};

extern int register_binfmt(struct linux_binfmt *);
extern int unregister_binfmt(struct linux_binfmt *);

extern int prepare_binprm(struct linux_binprm *);
extern void remove_arg_zero(struct linux_binprm *);
extern int search_binary_handler(struct linux_binprm *,struct pt_regs *);
extern int flush_old_exec(struct linux_binprm * bprm);

/* 堆栈区域保护 */
#define EXSTACK_DEFAULT   0	/* 任何arch默认设置为 */
#define EXSTACK_DISABLE_X 1	/* 禁用可执行堆栈 */
#define EXSTACK_ENABLE_X  2	/* 启用可执行堆栈 */

extern int setup_arg_pages(struct linux_binprm * bprm,
			   unsigned long stack_top,
			   int executable_stack);
extern int copy_strings(int argc,char __user * __user * argv,struct linux_binprm *bprm); 
extern int copy_strings_kernel(int argc,char ** argv,struct linux_binprm *bprm);
extern void compute_creds(struct linux_binprm *binprm);
extern int do_coredump(long signr, int exit_code, struct pt_regs * regs);
extern int set_binfmt(struct linux_binfmt *new);

#endif /* __KERNEL__ */
#endif /* _LINUX_BINFMTS_H */
