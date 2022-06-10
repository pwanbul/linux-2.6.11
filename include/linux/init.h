#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

#include <linux/config.h>
#include <linux/compiler.h>

/* 这些宏用于将某些函数或已初始化的数据（不适用于未初始化的数据）
 * 标记为“已初始化”函数。内核可以以此暗示该函数仅在初始化阶段使用，
 * 并在之后释放已使用的内存资源。
 *
 * Usage:
 * 对于函数:
 * 
 * 您应该在函数名称之前立即添加 __init，例如:
 *
 * static void __init initme(int x, int y)
 * {
 *    extern int z; z = x * y;
 * }
 *
 * 如果函数在某处有原型，您还可以在原型的右括号和分号之间添加 __init:
 *
 * extern int initialize_foobar_device(int, int, int) __init;
 *
 * 对于初始化数据:
 * 您应该在变量名和等号之间插入 __initdata 后跟值，(数据应当是静态的(包括函数内的)、全局的)例如:
 *
 * static int init_variable __initdata = 0;
 * static char linux_logo[] __initdata = { 0x32, 0x36, ... };
 *
 * 不要忘记初始化不在文件范围内的数据，即在函数内，否则 gcc 会将数据放入 bss 部分而不是 init 部分.
 * 
 * 另请注意，此数据不能为“const”.
 */

/* These are for everybody (although not all archs will actually
   discard it in modules) */
/* 这个标志符和函数声明放在一起，表示gcc编译器在编译时，
 * 需要把这个函数放在.init.text Section 中，
 * 而这个Section 在内核完成初始化之后，就会被释放掉。
 * */
#define __init		__attribute__ ((__section__ (".init.text")))

//这个标志符和变量声明放在一起，表示gcc编译器在编译时，需要把这个变量放在.init.data Section中，而这个Section 在内核完成初始化之后，会释放掉。
#define __initdata	__attribute__ ((__section__ (".init.data")))
#define __exitdata	__attribute__ ((__section__(".exit.data")))
#define __exit_call	__attribute_used__ __attribute__ ((__section__ (".exitcall.exit")))

#ifdef MODULE
#define __exit		__attribute__ ((__section__(".exit.text")))
#else
#define __exit		__attribute_used__ __attribute__ ((__section__(".exit.text")))
#endif

/* For assembly routines */
#define __INIT		.section	".init.text","ax"
#define __FINIT		.previous
#define __INITDATA	.section	".init.data","aw"

#ifndef __ASSEMBLY__
/*
 * 用于初始化调用..
 */
typedef int (*initcall_t)(void);		// initcall函数指针
typedef void (*exitcall_t)(void);		// exitcall函数指针

extern initcall_t __con_initcall_start[], __con_initcall_end[];
extern initcall_t __security_initcall_start[], __security_initcall_end[];

/* Defined in init/main.c */
extern char saved_command_line[];
#endif
  
#ifndef MODULE          // 静态链接

#ifndef __ASSEMBLY__

/* initcalls 现在按功能分组到单独的小节中。小节内的排序由链接顺序决定。
 * 为了向后兼容，initcall() 将调用放在设备初始化小节中。
 *
 * 注意：需要先定义一个指向函数的指针，在将指针设置到initcall section中
 * initcall的函数必定是__init的，反之不成立
 */

#define __define_initcall(level,fn) \
	static initcall_t __initcall_##fn __attribute_used__ \
	__attribute__((__section__(".initcall" level ".init"))) = fn
// initcall有多种级别
#define core_initcall(fn)		__define_initcall("1",fn)
#define postcore_initcall(fn)		__define_initcall("2",fn)
#define arch_initcall(fn)		__define_initcall("3",fn)
#define subsys_initcall(fn)		__define_initcall("4",fn)
#define fs_initcall(fn)			__define_initcall("5",fn)
#define device_initcall(fn)		__define_initcall("6",fn)
#define late_initcall(fn)		__define_initcall("7",fn)

#define __initcall(fn) device_initcall(fn)

// exitcall就一种
#define __exitcall(fn) \
	static exitcall_t __exitcall_##fn __exit_call = fn

// 其他类型的initcall
// 控制台相关
#define console_initcall(fn) \
	static initcall_t __initcall_##fn \
	__attribute_used__ __attribute__((__section__(".con_initcall.init")))=fn

// LSM相关
#define security_initcall(fn) \
	static initcall_t __initcall_##fn \
	__attribute_used__ __attribute__((__section__(".security_initcall.init"))) = fn

struct obs_kernel_param {
	const char *str;
	int (*setup_func)(char *);
	int early;
};

/*
 * 仅用于真正的核心代码。有关正常方式，请参阅 moduleparam.h。
 *
 * 强制对齐，以便编译器不会在 .init.setup 中将 obs_kernel_param “数组”的元素间隔得太远。
 */
#define __setup_param(str, unique_id, fn, early)			\
	static char __setup_str_##unique_id[] __initdata = str;	\
	static struct obs_kernel_param __setup_##unique_id	\
		__attribute_used__				\
		__attribute__((__section__(".init.setup")))	\
		__attribute__((aligned((sizeof(long)))))	\
		= { __setup_str_##unique_id, fn, early }

#define __setup_null_param(str, unique_id)			\
	__setup_param(str, unique_id, NULL, 0)

#define __setup(str, fn)					\
	__setup_param(str, fn, fn, 0)

#define __obsolete_setup(str)					\
	__setup_null_param(str, __LINE__)

/* NOTE: fn is as per module_param, not __setup!  Emits warning if fn
 * returns non-zero. */
#define early_param(str, fn)					\
	__setup_param(str, fn, fn, 1)

/* Relies on saved_command_line being set */
void __init parse_early_param(void);
#endif /* __ASSEMBLY__ */

/*
 * module_init() - 驱动程序初始化入口点
 * @x: 在内核启动时或模块插入时运行的函数
 * 
 * module_init() 将在 do_initcalls 期间（如果是内置的）或在模块插入时（如果是模块）被调用。每个模块只能有一个。
 */
#define module_init(x)	__initcall(x);

/**
 * module_exit() - 驱动出口入口点
 * @x: 删除驱动程序时要运行的功能
 * 
 * 当驱动程序是一个模块时，当与 rmmod 一起使用时，module_exit()
 * 将使用 cleanup_module() 包装驱动程序清理代码。
 * 如果驱动程序被静态编译到内核中，module_exit() 没有任何作用。每个模块只能有一个。
 */
#define module_exit(x)	__exitcall(x);

#else /* MODULE */

/* 不要在模块中使用这些，但有些人会...... */
#define core_initcall(fn)		module_init(fn)
#define postcore_initcall(fn)	module_init(fn)
#define arch_initcall(fn)		module_init(fn)
#define subsys_initcall(fn)		module_init(fn)
#define fs_initcall(fn)			module_init(fn)
#define device_initcall(fn)		module_init(fn)
#define late_initcall(fn)		module_init(fn)

#define security_initcall(fn)		module_init(fn)

/* 这些宏创建了一个虚拟内联：gcc 2.9x 不将别名计为使用，因此当 __init 函数声明为静态时会出现“未使用函数”警告。
 * 我们使用虚拟 ___module_inline 函数来终止警告并检查 init/cleanup 函数的类型。 */

/* 每个模块必须使用一个 module_init() 或一个 no_module_init */
#define module_init(initfn)					\
	static inline initcall_t __inittest(void)		\
	{ return initfn; }					\
	int init_module(void) __attribute__((alias(#initfn)));

/* 仅当您想要可卸载时才需要这样做。 */
#define module_exit(exitfn)					\
	static inline exitcall_t __exittest(void)		\
	{ return exitfn; }					\
	void cleanup_module(void) __attribute__((alias(#exitfn)));


/************************************************************/
#define __setup_param(str, unique_id, fn)	/* nothing */
#define __setup_null_param(str, unique_id) 	/* nothing */
#define __setup(str, func) 			/* nothing */
#define __obsolete_setup(str) 			/* nothing */
#endif

/* Data marked not to be saved by software_suspend() */
#define __nosavedata __attribute__ ((__section__ (".data.nosave")))

/* This means "can be init if no module support, otherwise module load
   may call it." */
#ifdef CONFIG_MODULES
#define __init_or_module
#define __initdata_or_module
#else
#define __init_or_module __init
#define __initdata_or_module __initdata
#endif /*CONFIG_MODULES*/

#ifdef CONFIG_HOTPLUG
#define __devinit
#define __devinitdata
#define __devexit
#define __devexitdata
#else
#define __devinit __init
#define __devinitdata __initdata
#define __devexit __exit
#define __devexitdata __exitdata
#endif

/* Functions marked as __devexit may be discarded at kernel link time, depending
   on config options.  Newer versions of binutils detect references from
   retained sections to discarded sections and flag an error.  Pointers to
   __devexit functions must use __devexit_p(function_name), the wrapper will
   insert either the function_name or NULL, depending on the config options.
 */
#if defined(MODULE) || defined(CONFIG_HOTPLUG)
#define __devexit_p(x) x
#else
#define __devexit_p(x) NULL
#endif

#ifdef MODULE
#define __exit_p(x) x
#else
#define __exit_p(x) NULL
#endif

#endif /* _LINUX_INIT_H */
