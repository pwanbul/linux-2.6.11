#
# Makefile for the kernel security code
#

obj-$(CONFIG_KEYS)			+= keys/
subdir-$(CONFIG_SECURITY_SELINUX)	+= selinux

# 如果我们不选择安全模型，请使用默认功能
ifneq ($(CONFIG_SECURITY),y)
obj-y		+= commoncap.o		// 权能
endif

# Object file lists
obj-$(CONFIG_SECURITY)			+= security.o dummy.o
# 如果下面的被配置，那么CONFIG_SECURITY必定被配置
# 必须在capability.o 之前才能正确堆叠
# 必须按下面的顺序写，引用设计到initcall的顺序
obj-$(CONFIG_SECURITY_SELINUX)		+= selinux/built-in.o		// SELinux
obj-$(CONFIG_SECURITY_CAPABILITIES)	+= commoncap.o capability.o		// capabilities
obj-$(CONFIG_SECURITY_ROOTPLUG)		+= commoncap.o root_plug.o
obj-$(CONFIG_SECURITY_SECLVL)		+= seclvl.o
