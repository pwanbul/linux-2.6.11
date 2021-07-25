/*
 * A simple kernel FIFO implementation.
 *
 * Copyright (C) 2004 Stelian Pop <stelian@popies.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/kfifo.h>

/*
 * kfifo_init - allocates a new FIFO using a preallocated buffer
 * @buffer: the preallocated buffer to be used.
 * @size: the size of the internal buffer, this have to be a power of 2.
 * @gfp_mask: get_free_pages mask, passed to kmalloc()
 * @lock: the lock to be used to protect the fifo buffer
 *
 * Do NOT pass the kfifo to kfifo_free() after use ! Simply free the
 * struct kfifo with kfree().
 * 动态初始化
 */
struct kfifo *kfifo_init(unsigned char *buffer, unsigned int size,
			 int gfp_mask, spinlock_t *lock)
{
	struct kfifo *fifo;

	/* size must be a power of 2 */
	BUG_ON(size & (size - 1));  // 等于0时才是2的幂

	// 为kfifo分配内存
	fifo = kmalloc(sizeof(struct kfifo), gfp_mask);
	if (!fifo)
		return ERR_PTR(-ENOMEM);

	fifo->buffer = buffer;      // kfifo不维护buffer
	fifo->size = size;          // buffer的size必须是2的幂
	fifo->in = fifo->out = 0;
	fifo->lock = lock;

	return fifo;
}
EXPORT_SYMBOL(kfifo_init);

/*
 * kfifo_alloc - allocates a new FIFO and its internal buffer
 * @size: the size of the internal buffer to be allocated.
 * @gfp_mask: get_free_pages mask, passed to kmalloc()
 * @lock: the lock to be used to protect the fifo buffer
 *
 * The size will be rounded-up to a power of 2.
 */
struct kfifo *kfifo_alloc(unsigned int size, int gfp_mask, spinlock_t *lock)
{
	unsigned char *buffer;
	struct kfifo *ret;

	/*
	 * 向上取整到 2 的下一个幂，因为我们的“让索引环绕”技术仅适用于这种情况。
	 */
	if (size & (size - 1)) {
		BUG_ON(size > 0x80000000);
		size = roundup_pow_of_two(size);    // 7->8,15->16
	}

	// 分配buffer空间
	buffer = kmalloc(size, gfp_mask);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	// 初始化kfifo
	ret = kfifo_init(buffer, size, gfp_mask, lock);

	if (IS_ERR(ret))
		kfree(buffer);

	return ret;
}
EXPORT_SYMBOL(kfifo_alloc);

/*
 * kfifo_free - frees the FIFO
 * @fifo: the fifo to be freed.
 * 使用kfifo生成的kfifo，才能使用该接口释放
 */
void kfifo_free(struct kfifo *fifo)
{
	kfree(fifo->buffer);
	kfree(fifo);
}
EXPORT_SYMBOL(kfifo_free);

/*
 * __kfifo_put - puts some data into the FIFO, no locking version
 * @fifo: the fifo to be used.
 * @buffer: the data to be added.
 * @len: the length of the data to be added.
 *
 * 此函数根据可用空间最多将“缓冲区”中的“len”字节复制到 FIFO 中，并返回复制的字节数。
 *
 * 请注意，只有一个并发读取器和一个并发写入器，您不需要额外的锁定来使用这些函数。
 */
unsigned int __kfifo_put(struct kfifo *fifo,
			 unsigned char *buffer, unsigned int len)
{
	unsigned int l;
    // 要保证len的大小不会超过buffer的size，多出的部分不会放入buffer中
	len = min(len, fifo->size - fifo->in + fifo->out);

	/* first put the data starting from fifo->in to buffer end */
	l = min(len, fifo->size - (fifo->in & (fifo->size - 1)));
	memcpy(fifo->buffer + (fifo->in & (fifo->size - 1)), buffer, l);

	/* then put the rest (if any) at the beginning of the buffer */
	memcpy(fifo->buffer, buffer + l, len - l);

	fifo->in += len;

	return len;
}
EXPORT_SYMBOL(__kfifo_put);

/*
 * __kfifo_get - gets some data from the FIFO, no locking version
 * @fifo: the fifo to be used.
 * @buffer: where the data must be copied.
 * @len: the size of the destination buffer.
 *
 * 此函数最多将 'len' 个字节从 FIFO 复制到 'buffer' 并返回复制的字节数。
 *
 * 请注意，只有一个并发读取器和一个并发写入器，您不需要额外的锁定来使用这些函数。
 */
unsigned int __kfifo_get(struct kfifo *fifo,
			 unsigned char *buffer, unsigned int len)
{
	unsigned int l;

	len = min(len, fifo->in - fifo->out);

	/* first get the data from fifo->out until the end of the buffer */
	l = min(len, fifo->size - (fifo->out & (fifo->size - 1)));
	memcpy(buffer, fifo->buffer + (fifo->out & (fifo->size - 1)), l);

	/* then get the rest (if any) from the beginning of the buffer */
	memcpy(buffer + l, fifo->buffer, len - l);

	fifo->out += len;

	return len;
}
EXPORT_SYMBOL(__kfifo_get);
