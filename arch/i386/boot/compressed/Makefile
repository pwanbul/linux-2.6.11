#
# linux/arch/i386/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

targets		:= vmlinux vmlinux.bin vmlinux.bin.gz head.o misc.o piggy.o
EXTRA_AFLAGS	:= -traditional

LDFLAGS_vmlinux := -Ttext $(IMAGE_OFFSET) -e startup_32

$(obj)/vmlinux: $(obj)/head.o $(obj)/misc.o $(obj)/piggy.o FORCE
	$(call if_changed,ld)
	@:

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCEm
	$(call if_changed,gzip)

LDFLAGS_piggy.o := -r --format binary --oformat elf32-i386 -T

$(obj)/piggy.o: $(obj)/vmlinux.scr $(obj)/vmlinux.bin.gz FORCE
	$(call if_changed,ld)

# piggy.o 其实是直接把 arch/i386/boot/compressed/vmlinux.bin.gz
# 这个gzip压缩文件作为一个二进制格式链接起来，关键是boot/compressed/vmlinux.scr 这个文件，
# 出了指定二进制数据外还导出了input_len， input_data，两个可重定位的变量。
# 后面解压的代码根据这两个变量可以知道压缩的内核所在的地址和长度，然后能够正确解压内核。