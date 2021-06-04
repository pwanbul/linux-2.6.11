/*
 *	bootsect.S		Copyright (C) 1991, 1992 Linus Torvalds
 *
 *	modified by Drew Eckhardt
 *	modified by Bruce Evans (bde)
 *	modified by Chris Noe (May 1999) (as86 -> gas)
 *	gutted by H. Peter Anvin (Jan 2003)
 *
 * BIG FAT NOTE: We're in real mode using 64k segments.  Therefore segment
 * addresses must be multiplied by 16 to obtain their respective linear
 * addresses. To avoid confusion, linear addresses are written using leading
 * hex while segment addresses are written as segment:offset.
 *
 */

// 0磁头0磁道1扇区中代码
#include <asm/boot.h>

SETUPSECTS	= 4			/* default nr of setup-sectors */
BOOTSEG		= 0x07C0		/* original address of boot-sector 段地址cs*/
INITSEG		= DEF_INITSEG		/* we move boot here - out of the way 0x9000*/
SETUPSEG	= DEF_SETUPSEG		/* setup starts here 0x9020*/
SYSSEG		= DEF_SYSSEG		/* system loaded at 0x10000 (65536) 0x1000*/
SYSSIZE		= DEF_SYSSIZE		/* system size: # of 16-byte clicks 0x7F00*/
					/* to be loaded */
ROOT_DEV	= 0 			/* ROOT_DEV is now written by "build" */
SWAP_DEV	= 0			/* SWAP_DEV is now written by "build" */

#ifndef SVGA_MODE
#define SVGA_MODE ASK_VGA
#endif

#ifndef RAMDISK
#define RAMDISK 0
#endif

#ifndef ROOT_RDONLY
#define ROOT_RDONLY 1
#endif

.code16     // 这个阶段处于实模式，怎么能用C呢？答案是，这里用的是16位的C。gcc早已 经支持.code16 gcc模式了
.text

.global _start
_start:      // 安装在0x90000处

	# Normalize the start address
	jmpl	$BOOTSEG, $start2       # 跳转到$BOOTSEG段中偏移量为$start2处

start2:
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	$0x7c00, %sp
	sti               // 设置中断
	cld               // 清除方向标志，cld即告诉程序si，di向前移动(内存地址增大的方向)，std指令为设置方向，告诉程序si，di向后移动(内存地址减小的方向)

	movw	$bugger_off_msg, %si         // si指向要显示的字符

msg_loop:
	lodsb       // LODSB的功能是将DS:[si]中的内容取到AL中，然后SI<=SI+/-1
	andb	%al, %al
	jz	die            // je是jump if zero的意思,jz是jump if equal的意思,两者都是结果为0(相等),即ZF=1时转移
	movb	$0xe, %ah
	movw	$7, %bx
	int	$0x10
	jmp	msg_loop

die:
	# Allow the user to press a key, then reboot
	xorw	%ax, %ax
	int	$0x16        // 键盘中断 在ah中接收基本键盘输入功能信号
	int	$0x19           // int 19一般是不会执行的，因为在int 16之后系统将会跳到用户选择的系统运行，也就//是说如果用户不选择的话，系统将会重启，然后执行下面的跳转代码

	# int 0x19 should never return.  In case it does anyway,
	# invoke the BIOS reset code...
	ljmp	$0xf000,$0xfff0


bugger_off_msg:
	.ascii	"Direct booting from floppy is no longer supported.\r\n"
	.ascii	"Please use a boot loader program instead.\r\n"
	.ascii	"\n"
	.ascii	"Remove disk and press any key to reboot . . .\r\n"
	.byte	0
	

	# Kernel attributes; used by setup

	.org 497
setup_sects:	.byte SETUPSECTS
root_flags:	.word ROOT_RDONLY
syssize:	.word SYSSIZE
swap_dev:	.word SWAP_DEV
ram_size:	.word RAMDISK
vid_mode:	.word SVGA_MODE
root_dev:	.word ROOT_DEV
boot_flag:	.word 0xAA55        // 结束标记
