/*
 * linux/kernel/irq/manage.c
 *
 * Copyright (C) 1992, 1998-2004 Linus Torvalds, Ingo Molnar
 *
 * This file contains driver APIs to the irq subsystem.
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/interrupt.h>

#include "internals.h"

#ifdef CONFIG_SMP

cpumask_t irq_affinity[NR_IRQS] = { [0 ... NR_IRQS-1] = CPU_MASK_ALL };

/**
 *	synchronize_irq - wait for pending IRQ handlers (on other CPUs)
 *
 *	This function waits for any pending IRQ handlers for this interrupt
 *	to complete before returning. If you use this function while
 *	holding a resource the IRQ handler may need you will deadlock.
 *
 *	This function may be called - with care - from IRQ context.
 */
void synchronize_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_desc + irq;

	while (desc->status & IRQ_INPROGRESS)
		cpu_relax();
}

EXPORT_SYMBOL(synchronize_irq);

#endif

/**
 *	disable_irq_nosync - disable an irq without waiting
 *	@irq: Interrupt to disable
 *
 *	Disable the selected interrupt line.  Disables and Enables are
 *	nested.
 *	Unlike disable_irq(), this function does not ensure existing
 *	instances of the IRQ handler have completed before returning.
 *
 *	This function may be called from IRQ context.
 */
void disable_irq_nosync(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	if (!desc->depth++) {
		desc->status |= IRQ_DISABLED;
		desc->handler->disable(irq);
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(disable_irq_nosync);

/**
 *	disable_irq - disable an irq and wait for completion
 *	@irq: Interrupt to disable
 *
 *	Disable the selected interrupt line.  Enables and Disables are
 *	nested.
 *	This function waits for any pending IRQ handlers for this interrupt
 *	to complete before returning. If you use this function while
 *	holding a resource the IRQ handler may need you will deadlock.
 *
 *	This function may be called - with care - from IRQ context.
 */
void disable_irq(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;

	disable_irq_nosync(irq);
	if (desc->action)
		synchronize_irq(irq);
}

EXPORT_SYMBOL(disable_irq);

/**
 *	enable_irq - enable handling of an irq
 *	@irq: Interrupt to enable
 *
 *	Undoes the effect of one call to disable_irq().  If this
 *	matches the last disable, processing of interrupts on this
 *	IRQ line is re-enabled.
 *
 *	This function may be called from IRQ context.
 */
void enable_irq(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	switch (desc->depth) {
	case 0:
		WARN_ON(1);
		break;
	case 1: {
		unsigned int status = desc->status & ~IRQ_DISABLED;

		desc->status = status;
		if ((status & (IRQ_PENDING | IRQ_REPLAY)) == IRQ_PENDING) {
			desc->status = status | IRQ_REPLAY;
			hw_resend_irq(desc->handler,irq);
		}
		desc->handler->enable(irq);
		/* fall-through */
	}
	default:
		desc->depth--;
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(enable_irq);

/*
 * Internal function that tells the architecture code whether a
 * particular irq has been exclusively allocated or is available
 * for driver use.
 */
int can_request_irq(unsigned int irq, unsigned long irqflags)
{
	struct irqaction *action;

	if (irq >= NR_IRQS)
		return 0;

	action = irq_desc[irq].action;
	if (action)
		if (irqflags & action->flags & SA_SHIRQ)
			action = NULL;

	return !action;
}

/*
 * 注册 irq action 的内部函数 - 通常用于分配作为架构一部分的特殊中断。
 * irq: irq线
 **/
int setup_irq(unsigned int irq, struct irqaction * new)
{
	// irq_desc为irq描述符数组
	struct irq_desc *desc = irq_desc + irq;
	struct irqaction *old, **p;
	unsigned long flags;
	int shared = 0;

	if (desc->handler == &no_irq_type)
		return -ENOSYS;
	/*
	 * Some drivers like serial.c use request_irq() heavily,
	 * so we have to be careful not to interfere with a
	 * running system.
	 */
	if (new->flags & SA_SAMPLE_RANDOM) {
		/*
		 * This function might sleep, we want to call it first,
		 * outside of the atomic block.
		 * Yes, this might clear the entropy pool if the wrong
		 * driver is attempted to be loaded, without actually
		 * installing a new handler, but is this really a problem,
		 * only the sysadmin is able to do this.
		 */
		rand_initialize_irq(irq);
	}

	/*
	 * The following block of code has to be executed atomically
	 */
	spin_lock_irqsave(&desc->lock,flags);
	p = &desc->action;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			spin_unlock_irqrestore(&desc->lock,flags);
			return -EBUSY;
		}

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	*p = new;

	if (!shared) {
		desc->depth = 0;
		desc->status &= ~(IRQ_DISABLED | IRQ_AUTODETECT |
				  IRQ_WAITING | IRQ_INPROGRESS);
		if (desc->handler->startup)
			desc->handler->startup(irq);
		else
			desc->handler->enable(irq);
	}
	spin_unlock_irqrestore(&desc->lock,flags);

	new->irq = irq;
	register_irq_proc(irq);
	new->dir = NULL;
	register_handler_proc(irq, new);

	return 0;
}

/**
 *  删除一个中断处理函数
 *
 *	free_irq - free an interrupt
 *	@irq: Interrupt line to free
 *	@dev_id: Device identity to free
 *
 *	Remove an interrupt handler. The handler is removed and if the
 *	interrupt line is no longer in use by any driver it is disabled.
 *	On a shared IRQ the caller must ensure the interrupt is disabled
 *	on the card it drives before calling this function. The function
 *	does not return until any executing interrupts for this IRQ
 *	have completed.
 *
 *	This function must not be called from interrupt context.
 */
void free_irq(unsigned int irq, void *dev_id)
{
	struct irq_desc *desc;
	struct irqaction **p;
	unsigned long flags;

	if (irq >= NR_IRQS)
		return;

	desc = irq_desc + irq;
	spin_lock_irqsave(&desc->lock,flags);
	p = &desc->action;
	for (;;) {
		struct irqaction * action = *p;

		if (action) {
			struct irqaction **pp = p;

			p = &action->next;
			if (action->dev_id != dev_id)
				continue;

			/* Found it - now remove it from the list of entries */
			*pp = action->next;
			if (!desc->action) {
				desc->status |= IRQ_DISABLED;
				if (desc->handler->shutdown)
					desc->handler->shutdown(irq);
				else
					desc->handler->disable(irq);
			}
			spin_unlock_irqrestore(&desc->lock,flags);
			unregister_handler_proc(irq, action);

			/* Make sure it's not being used on another CPU */
			synchronize_irq(irq);
			kfree(action);
			return;
		}
		printk(KERN_ERR "Trying to free free IRQ%d\n",irq);
		spin_unlock_irqrestore(&desc->lock,flags);
		return;
	}
}

EXPORT_SYMBOL(free_irq);

/*
 *  由驱动程序调用，注册一个中断处理函数
 *
 *	request_irq - allocate an interrupt line
 *	@irq: Interrupt line to allocate
 *	@handler: Function to be called when the IRQ occurs
 *	@irqflags: Interrupt type flags
 *	@devname: An ascii name for the claiming device
 *	@dev_id: A cookie passed back to the handler function
 *
 *	此调用分配中断资源并启用中断线和 IRQ 处理。从这个调用开始，
 *	您的处理程序函数可能会被调用。由于您的处理程序函数必须清除电路板
 *	引发的任何中断，您必须注意初始化您的硬件并以正确的顺序设置中断处理程序。
 *
 *	dev_id 必须是全局唯一的。通常，设备数据结构的地址用作 cookie。
 *	由于处理程序接收此值，因此使用它是有意义的。
 *
 *	如果您的中断是共享的，您必须传递一个非空的 dev_id，因为这是释放中断时所必需的。
 *
 *	irqflags:
 *
 *	SA_SHIRQ		中断共享，在同一个IRQ线上注册多个中断处理程序
 *	SA_INTERRUPT		处理时禁用本地中断
 *	SA_SAMPLE_RANDOM	中断可用于熵池
 *
 */
int request_irq(unsigned int irq,
		irqreturn_t (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction * action;
	int retval;

	/*
	 * 完整性检查：共享中断必须传入真实的开发 ID，
	 * 否则我们稍后会在试图找出哪个中断是哪个（弄乱中断释放逻辑等）时遇到麻烦。
	 */
	if ((irqflags & SA_SHIRQ) && !dev_id)	// 如果设置了SA_SHIRQ就必须指定dev_id
		return -EINVAL;
	if (irq >= NR_IRQS)		// 超限
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	// 分配内存，非阻塞
	action = kmalloc(sizeof(struct irqaction), GFP_ATOMIC);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	cpus_clear(action->mask);
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_irq(irq, action);
	if (retval)
		kfree(action);

	return retval;
}

EXPORT_SYMBOL(request_irq);

