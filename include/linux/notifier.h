/*
 *	管理通知链的例程，以将状态更改传递给任何感兴趣的例程。
 *	我们需要这个而不是硬编码的调用列表，以便模块可以深入了解内部。
 *	网络设备需要它们，所以在这里它们是为你们其他人准备的。
 *
 *				Alan Cox <Alan.Cox@linux.org>
 */

// 通知链机制 https://www.cnblogs.com/pengdonglin137/articles/4075148.html

#ifndef _LINUX_NOTIFIER_H
#define _LINUX_NOTIFIER_H
#include <linux/errno.h>

struct notifier_block
{
	int (*notifier_call)(struct notifier_block *self, unsigned long, void *);  // callback，由被通知方提供
	struct notifier_block *next;        // 链表指针
	int priority;       // 优先级，值越大优先级越高，排在左边
};


#ifdef __KERNEL__

extern int notifier_chain_register(struct notifier_block **list, struct notifier_block *n);
extern int notifier_chain_unregister(struct notifier_block **nl, struct notifier_block *n);
extern int notifier_call_chain(struct notifier_block **n, unsigned long val, void *v);

// 回调函数可以返回以下值
#define NOTIFY_DONE		0x0000		/* 对事件视而不见 Don't care */
#define NOTIFY_OK		0x0001		/* 事件正确处理 Suits me */
#define NOTIFY_STOP_MASK	0x8000		/* 由notifier_call_chain检查，看继续调用回调函数，
 * 还是停止，_BAD和_STOP中包含该标志 Don't call further */
#define NOTIFY_BAD		(NOTIFY_STOP_MASK|0x0002)	/* 事件处理出错，不再继续调用回调函数 Bad/Veto action	*/
/*
 * 从通知程序返回并停止进一步调用的清洁方式。
 */
#define NOTIFY_STOP		(NOTIFY_OK|NOTIFY_STOP_MASK)    // 回调出错，不再继续调用该事件回调函数

/*
 *	Declared notifiers so far. I can imagine quite a few more chains
 *	over time (eg laptop power reset chains, reboot chain (to clean 
 *	device units up), device [un]mount chain, module load/unload chain,
 *	low memory chain, screenblank chain (for plug in modular screenblankers) 
 *	VC switch chains (for loadable kernel svgalib VC switch helpers) etc...
 */
 
/* netdevice notifier chain */
#define NETDEV_UP	0x0001	/* For now you can't veto a device up/down */
#define NETDEV_DOWN	0x0002
#define NETDEV_REBOOT	0x0003	/* Tell a protocol stack a network interface
				   detected a hardware crash and restarted
				   - we can use this eg to kick tcp sessions
				   once done */
#define NETDEV_CHANGE	0x0004	/* Notify device state change */
#define NETDEV_REGISTER 0x0005
#define NETDEV_UNREGISTER	0x0006
#define NETDEV_CHANGEMTU	0x0007
#define NETDEV_CHANGEADDR	0x0008
#define NETDEV_GOING_DOWN	0x0009
#define NETDEV_CHANGENAME	0x000A

#define SYS_DOWN	0x0001	/* Notify of system down */
#define SYS_RESTART	SYS_DOWN
#define SYS_HALT	0x0002	/* Notify of system halt */
#define SYS_POWER_OFF	0x0003	/* Notify of system power off */

#define NETLINK_URELEASE	0x0001	/* Unicast netlink socket released */

#define CPU_ONLINE		0x0002 /* CPU (unsigned)v is up */
#define CPU_UP_PREPARE		0x0003 /* CPU (unsigned)v coming up */
#define CPU_UP_CANCELED		0x0004 /* CPU (unsigned)v NOT coming up */
#define CPU_DOWN_PREPARE	0x0005 /* CPU (unsigned)v going down */
#define CPU_DOWN_FAILED		0x0006 /* CPU (unsigned)v NOT going down */
#define CPU_DEAD		0x0007 /* CPU (unsigned)v dead */

#endif /* __KERNEL__ */
#endif /* _LINUX_NOTIFIER_H */
