/* ld script to make i386 Linux kernel
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>;
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/page.h>

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(startup_32)           // 入口为startup_32
jiffies = jiffies_64;
SECTIONS
{
FIXADDR_START       // 0xfffff000 -

. = ALIGN(16);		/* 异常表 */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  RODATA

  /* writeable */
  .data : {			/* Data */
	*(.data)
	CONSTRUCTORS
	}

  . = ALIGN(4096);
  __nosave_begin = .;
  .data_nosave : { *(.data.nosave) }
  . = ALIGN(4096);
  __nosave_end = .;

  . = ALIGN(4096);
  .data.page_aligned : { *(.data.idt) }

  . = ALIGN(32);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  _edata = .;			/* End of data section */

  . = ALIGN(THREAD_SIZE);	/* init_task */
  .data.init_task : { *(.data.init_task) }      // init_task的线程描述符数据

  /* will be freed after init */
  // 初始化结束后这些代码将被释放
  . = ALIGN(4096);		/* 初始化代码和数据 */
  __init_begin = .;			// 初始化代码和数据开始

  .init.text : {    // 初始化函数
	_sinittext = .;
	*(.init.text)
	_einittext = .;
  }
  .init.data : { *(.init.data) }    // 初始化数据

  . = ALIGN(16);
  __setup_start = .;
  .init.setup : { *(.init.setup) }
  __setup_end = .;

  __initcall_start = .;     // initcall 处理
  .initcall.init : {
	*(.initcall1.init) 
	*(.initcall2.init) 
	*(.initcall3.init) 
	*(.initcall4.init) 
	*(.initcall5.init) 
	*(.initcall6.init) 
	*(.initcall7.init)
  }
  __initcall_end = .;

  __con_initcall_start = .;			// 控制台初始化
  .con_initcall.init : { *(.con_initcall.init) }
  __con_initcall_end = .;

  SECURITY_INIT			// LSM初始化
  . = ALIGN(4);
  __alt_instructions = .;
  .altinstructions : { *(.altinstructions) } 
  __alt_instructions_end = .; 
 .altinstr_replacement : { *(.altinstr_replacement) } 
  /* .exit.text 在运行时被丢弃，而不是链接时，以处理来自 .altinstructions 和 .eh_frame 的引用 */
  .exit.text : { *(.exit.text) }
  .exit.data : { *(.exit.data) }
  . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;
  . = ALIGN(32);
  __per_cpu_start = .;              // per cpu 变量
  .data.percpu  : { *(.data.percpu) }
  __per_cpu_end = .;
  . = ALIGN(4096);
  __init_end = .;			// 初始化相关代码和结束
  /* freed after init ends here */
	
  __bss_start = .;		/* BSS */
  .bss : {
	*(.bss.page_aligned)		// swapper_pg_dir	内核页全局目录
	*(.bss)
  }
  . = ALIGN(4);
  __bss_stop = .; 

  _end = . ;

  /* 这是内核创建早期引导页表的地方 */
  . = ALIGN(4096);
  pg0 = .;      // 临时内核页表从pg0处开始存放

  /* Sections to be discarded */
  /DISCARD/ : {
	*(.exitcall.exit)
	}

  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
}
