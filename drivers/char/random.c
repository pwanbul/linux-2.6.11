/*
 * random.c -- A strong random number generator
 *
 * Version 1.89, last modified 19-Sep-99
 *
 * Copyright Theodore Ts'o, 1994, 1995, 1996, 1997, 1998, 1999.  All
 * rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU General Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/*
 * (now, with legal B.S. out of the way.....)
 *
 * 该例程从设备驱动程序等收集环境噪声，
 * 并返回良好的随机数，适合加密使用。
 * 除了明显的密码用途外，这些数字还适用于TCP序列号的种子，
 * 以及其他需要具有不仅随机而且难以被攻击者预测的数字的地方。
 *
 * 操作原理
 * ===================
 *
 * 计算机是非常可预测的设备。 因此，在计算机上生成真正的随机数是极其困难的
 * ——与伪随机数相反，伪随机数可以通过使用算法轻松生成。
 * 不幸的是，攻击者很容易猜测伪随机数生成器的序列，对于某些应用程序来说这是不可接受的。
 * 因此，我们必须尝试从计算机环境中收集外部攻击者很难观察到的“环境噪声”，并使用它来生成随机数。
 * 在 Unix 环境中，这最好从内核内部完成。
 *
 * 来自环境的随机性来源包括键盘间计时、某些中断的中断间计时
 * 以及 (a) 非确定性和 (b) 外部观察者难以测量的其他事件。
 * 来自这些来源的随机性被添加到“熵池”中，它使用类似 CRC 的函数进行混合。
 * 这在密码学上不是很强大，但假设随机性不是恶意选择的，它就足够了，
 * 并且它足够快以至于在每个中断上执行它的开销非常合理。
 * 当随机字节混合到熵池中时，例程会估计有多少位随机性已存储到随机数生成器的内部状态中。
 *
 * 当需要随机字节时，它们是通过获取“熵池”内容的 SHA 散列来获得的。
 * SHA 哈希避免暴露熵池的内部状态。 据信，从其输出中导出有关 SHA 输入的任何有用信息在计算上是不可行的。
 * 即使可以通过某种巧妙的方式来分析 SHA，只要从生成器返回的数据量小于池中的固有熵，输出数据是完全不可预测的。
 * 出于这个原因，该例程在输出随机数时会降低其对熵池中包含多少位“真正随机性”的内部估计。
 *
 * 如果这个估计为零，程序仍然可以生成随机数；然而，攻击者可能（至少在理论上）能够从先前的输出推断生成器的未来输出。
 * 这需要成功地对 SHA 进行密码分析，这被认为是不可行的，但存在着遥远的可能性。
 * 尽管如此，这些数字对于绝大多数目的应该是有用的。
 *
 * Exported interfaces ---- output
 * ===============================
 * https://zhuanlan.zhihu.com/p/64680713
 *
 * 共有三个导出接口；第一个是设计为在内核中使用的:
 *
 * 	void get_random_bytes(void *buf, int nbytes);
 *
 * 此接口将返回请求的随机字节数，并将其放入请求的缓冲区中.
 *
 * 另外两个接口是两个字符设备 dev/random 和 dev/urandom。
 * dev/random 适用于需要非常高质量的随机性时（例如，用于密钥生成或一次性填充），
 * 因为它只会返回包含的最大随机数位数（由随机数生成器估计）在熵池中。
 *
 * /dev/urandom 设备没有这个限制，它会根据请求返回尽可能多的字节。
 * 随着越来越多的随机字节被请求而没有给熵池充电的时间，这将导致随机数仅具有加密强度。
 * 然而，对于许多应用，这是可以接受的。
 *
 * Exported interfaces ---- input
 * ==============================
 *
 * 当前用于从设备收集环境噪声的导出接口是：
 *
 * 	void add_input_randomness(unsigned int type, unsigned int code,
 *                                unsigned int value);
 * 	void add_interrupt_randomness(int irq);
 *
 * add_input_randomness() 使用输入层中断时序，以及来自硬件的事件类型信息。
 *
 * add_interrupt_randomness() 使用中断时间作为熵池的随机输入。
 * 请注意，并非所有中断都是随机性的良好来源！
 * 例如，定时器中断不是一个好的选择，因为中断的周期太规律，因此攻击者可以预测。
 * 磁盘中断是更好的度量，因为磁盘中断的时间更不可预测。
 *
 * 所有这些例程都试图估计特定随机源的随机数位。他们通过跟踪事件时序的一阶和二阶增量来做到这一点。
 *
 * 确保系统启动时的不可预测性
 * ============================================
 *
 * 当任何操作系统启动时，它都会经历一系列攻击者相当可预测的动作，
 * 特别是如果启动不涉及与人类操作员的交互。
 * 这将熵池中不可预测的实际位数减少到 entropy_count 中的值以下。
 * 为了抵消这种影响，它有助于在关闭和启动时在熵池中携带信息。
 * 为此，请将以下几行放入在引导序列期间运行的适当脚本：
 *
 *	echo "Initializing random number generator..."
 *	random_seed=/var/run/random-seed
 *	# Carry a random seed from start-up to start-up
 *	# Load and then save the whole entropy pool
 *	if [ -f $random_seed ]; then
 *		cat $random_seed >/dev/urandom
 *	else
 *		touch $random_seed
 *	fi
 *	chmod 600 $random_seed
 *	dd if=/dev/urandom of=$random_seed count=1 bs=512
 *
 * 以及在系统关闭时运行的适当脚本中的以下几行：
 *
 *	# Carry a random seed from shut-down to start-up
 *	# Save the whole entropy pool
 *	echo "Saving random seed..."
 *	random_seed=/var/run/random-seed
 *	touch $random_seed
 *	chmod 600 $random_seed
 *	dd if=/dev/urandom of=$random_seed count=1 bs=512
 *
 * 例如，在大多数使用 System V init 脚本的现代系统上，
 * 此类代码片段可以在 etc/rc.d/init.d/random 中找到。
 * 在较旧的 Linux 系统上，正确的脚本位置可能在 etc/rcb.d/rc.local 或 etc/rc.d/rc.0 中。
 *
 * 实际上，这些命令会导致熵池的内容在关闭时保存并在启动时重新加载到熵池中。
 * （启动脚本中添加的'dd'是为了确保每次启动时etc/random-seed都是不同的，
 * 即使系统没有执行rc.0就崩溃了。） 即使完全了解启动活动，预测熵池的状态也需要了解系统以前的历史。
 *
 * Configuring the /dev/random driver under Linux
 * ==============================================
 *
 * The /dev/random driver under Linux uses minor numbers 8 and 9 of
 * the /dev/mem major number (#1).  So if your system does not have
 * /dev/random and /dev/urandom created already, they can be created
 * by using the commands:
 *
 * 	mknod /dev/random c 1 8
 * 	mknod /dev/urandom c 1 9
 *
 * Acknowledgements:
 * =================
 *
 * 构建这个随机数生成器的想法来源于 Pretty Good Privacy 的随机数生成器，以及与 Phil Karn 的私人讨论。
 * Colin Plumb 提供了一个更快的随机数生成器，它加速了熵池的混合函数，取自 PGPfone。
 * Dale Worley 也贡献了许多有用的想法和建议来改进这个驱动程序。
 *
 * 设计中的任何缺陷完全由我负责，不应归咎于 Phil、Colin 或 PGP 的任何作者。
 *
 * SHA 变换的代码取自 Peter Gutmann 的实现，该实现已被置于公共领域。
 * MD5 转换的代码取自 Colin Plumb 的实现，该实现已被置于公共领域。
 * MD5 加密校验和由 Ronald Rivest 设计，并记录在 RFC 1321“MD5 消息摘要算法”中。
 *
 * 有关此主题的更多背景信息可从 Donald Eastlake、Steve Crocker
 * 和 Jeff Schiller 撰写的 RFC 1750“安全性随机性建议”中获得。
 */

#include <linux/utsname.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/genhd.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/percpu.h>

#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>

/*
 * Configuration information
 */
#define DEFAULT_POOL_SIZE 512
#define SECONDARY_POOL_SIZE 128
#define BATCH_ENTROPY_SIZE 256
#define USE_SHA

/*
 * The minimum number of bits of entropy before we wake up a read on
 * /dev/random.  Should be enough to do a significant reseed.
 */
static int random_read_wakeup_thresh = 64;

/*
 * If the entropy count falls under this number of bits, then we
 * should wake up processes which are selecting or polling on write
 * access to /dev/random.
 */
static int random_write_wakeup_thresh = 128;

/*
 * When the input pool goes over trickle_thresh, start dropping most
 * samples to avoid wasting CPU time and reduce lock contention.
 */

static int trickle_thresh = DEFAULT_POOL_SIZE * 7;

static DEFINE_PER_CPU(int, trickle_count) = 0;

/*
 * 大小为 .poolwords 的池与 GF(2) 上 .poolwords 次的原始多项式混合。
 * 各种尺寸的水龙头定义如下。
 * 它们被选择为均匀间隔（与均匀间隔的最小 RMS 距离；注释中的数字是按比例缩放的平方误差总和），
 * 除了最后一个抽头，它是 1，以使扭曲尽可能快地发生。
 */
static struct poolinfo {
	int poolwords;
	int tap1, tap2, tap3, tap4, tap5;
} poolinfo_table[] = {
	/* x^2048 + x^1638 + x^1231 + x^819 + x^411 + x + 1  -- 115 */
	{ 2048,	1638,	1231,	819,	411,	1 },

	/* x^1024 + x^817 + x^615 + x^412 + x^204 + x + 1 -- 290 */
	{ 1024,	817,	615,	412,	204,	1 },
#if 0				/* Alternate polynomial */
	/* x^1024 + x^819 + x^616 + x^410 + x^207 + x^2 + 1 -- 115 */
	{ 1024,	819,	616,	410,	207,	2 },
#endif

	/* x^512 + x^411 + x^308 + x^208 + x^104 + x + 1 -- 225 */
	{ 512,	411,	308,	208,	104,	1 },
#if 0				/* Alternates */
	/* x^512 + x^409 + x^307 + x^206 + x^102 + x^2 + 1 -- 95 */
	{ 512,	409,	307,	206,	102,	2 },
	/* x^512 + x^409 + x^309 + x^205 + x^103 + x^2 + 1 -- 95 */
	{ 512,	409,	309,	205,	103,	2 },
#endif

	/* x^256 + x^205 + x^155 + x^101 + x^52 + x + 1 -- 125 */
	{ 256,	205,	155,	101,	52,	1 },

	/* x^128 + x^103 + x^76 + x^51 +x^25 + x + 1 -- 105 */
	{ 128,	103,	76,	51,	25,	1 },
#if 0	/* Alternate polynomial */
	/* x^128 + x^103 + x^78 + x^51 + x^27 + x^2 + 1 -- 70 */
	{ 128,	103,	78,	51,	27,	2 },
#endif

	/* x^64 + x^52 + x^39 + x^26 + x^14 + x + 1 -- 15 */
	{ 64,	52,	39,	26,	14,	1 },

	/* x^32 + x^26 + x^20 + x^14 + x^7 + x + 1 -- 15 */
	{ 32,	26,	20,	14,	7,	1 },

	{ 0,	0,	0,	0,	0,	0 },
};

#define POOLBITS	poolwords*32
#define POOLBYTES	poolwords*4

/*
 * For the purposes of better mixing, we use the CRC-32 polynomial as
 * well to make a twisted Generalized Feedback Shift Reigster
 *
 * (See M. Matsumoto & Y. Kurita, 1992.  Twisted GFSR generators.  ACM
 * Transactions on Modeling and Computer Simulation 2(3):179-194.
 * Also see M. Matsumoto & Y. Kurita, 1994.  Twisted GFSR generators
 * II.  ACM Transactions on Mdeling and Computer Simulation 4:254-266)
 *
 * Thanks to Colin Plumb for suggesting this.
 *
 * We have not analyzed the resultant polynomial to prove it primitive;
 * in fact it almost certainly isn't.  Nonetheless, the irreducible factors
 * of a random large-degree polynomial over GF(2) are more than large enough
 * that periodicity is not a concern.
 *
 * The input hash is much less sensitive than the output hash.  All
 * that we want of it is that it be a good non-cryptographic hash;
 * i.e. it not produce collisions when fed "random" data of the sort
 * we expect to see.  As long as the pool state differs for different
 * inputs, we have preserved the input entropy and done a good job.
 * The fact that an intelligent attacker can construct inputs that
 * will produce controlled alterations to the pool's state is not
 * important because we don't consider such inputs to contribute any
 * randomness.  The only property we need with respect to them is that
 * the attacker can't increase his/her knowledge of the pool's state.
 * Since all additions are reversible (knowing the final state and the
 * input, you can reconstruct the initial state), if an attacker has
 * any uncertainty about the initial state, he/she can only shuffle
 * that uncertainty about, but never cause any collisions (which would
 * decrease the uncertainty).
 *
 * The chosen system lets the state of the pool be (essentially) the input
 * modulo the generator polymnomial.  Now, for random primitive polynomials,
 * this is a universal class of hash functions, meaning that the chance
 * of a collision is limited by the attacker's knowledge of the generator
 * polynomail, so if it is chosen at random, an attacker can never force
 * a collision.  Here, we use a fixed polynomial, but we *can* assume that
 * ###--> it is unknown to the processes generating the input entropy. <-###
 * Because of this important property, this is a good, collision-resistant
 * hash; hash collisions will occur no more often than chance.
 */

/*
 * Linux 2.2 compatibility
 */
#ifndef DECLARE_WAITQUEUE
#define DECLARE_WAITQUEUE(WAIT, PTR) struct wait_queue WAIT = { PTR, NULL }
#endif
#ifndef DECLARE_WAIT_QUEUE_HEAD
#define DECLARE_WAIT_QUEUE_HEAD(WAIT) struct wait_queue *WAIT
#endif

/*
 * 静态全局变量
 */
static struct entropy_store *random_state; /* 默认的全局存储 */
static struct entropy_store *sec_random_state; /* 二级存储 */
static struct entropy_store *urandom_state; /* 对于 urandom */
static DECLARE_WAIT_QUEUE_HEAD(random_read_wait);
static DECLARE_WAIT_QUEUE_HEAD(random_write_wait);

/*
 * Forward procedure declarations
 */
#ifdef CONFIG_SYSCTL
static void sysctl_init_random(struct entropy_store *random_state);
#endif

/*****************************************************************
 *
 * Utility functions, with some ASM defined functions for speed
 * purposes
 *
 *****************************************************************/

/*
 * Unfortunately, while the GCC optimizer for the i386 understands how
 * to optimize a static rotate left of x bits, it doesn't know how to
 * deal with a variable rotate of x bits.  So we use a bit of asm magic.
 */
#if (!defined (__i386__))
static inline __u32 rotate_left(int i, __u32 word)
{
	return (word << i) | (word >> (32 - i));
}
#else
static inline __u32 rotate_left(int i, __u32 word)
{
	__asm__("roll %%cl,%0"
		:"=r" (word)
		:"0" (word),"c" (i));
	return word;
}
#endif

/*
 * More asm magic....
 *
 * For entropy estimation, we need to do an integral base 2
 * logarithm.
 *
 * Note the "12bits" suffix - this is used for numbers between
 * 0 and 4095 only.  This allows a few shortcuts.
 */
#if 0	/* Slow but clear version */
static inline __u32 int_ln_12bits(__u32 word)
{
	__u32 nbits = 0;

	while (word >>= 1)
		nbits++;
	return nbits;
}
#else	/* Faster (more clever) version, courtesy Colin Plumb */
static inline __u32 int_ln_12bits(__u32 word)
{
	/* Smear msbit right to make an n-bit mask */
	word |= word >> 8;
	word |= word >> 4;
	word |= word >> 2;
	word |= word >> 1;
	/* Remove one bit to make this a logarithm */
	word >>= 1;
	/* Count the bits set in the word */
	word -= (word >> 1) & 0x555;
	word = (word & 0x333) + ((word >> 2) & 0x333);
	word += (word >> 4);
	word += (word >> 8);
	return word & 15;
}
#endif

#if 0
static int debug = 0;
module_param(debug, bool, 0644);
#define DEBUG_ENT(fmt, arg...) do { if (debug) \
	printk(KERN_DEBUG "random %04d %04d %04d: " \
	fmt,\
	random_state->entropy_count,\
	sec_random_state->entropy_count,\
	urandom_state->entropy_count,\
	## arg); } while (0)
#else
#define DEBUG_ENT(fmt, arg...) do {} while (0)
#endif

/**********************************************************************
 *
 * 操作系统独立的熵存储。以下是处理在熵池中存储熵的函数。
 *
 **********************************************************************/

struct entropy_store {
	/*主要读取数据： */
	struct poolinfo poolinfo;		// 多项式
	__u32 *pool;		// 池空间
	const char *name;

	/* 读写数据： */
	spinlock_t lock ____cacheline_aligned_in_smp;
	unsigned add_ptr;
	int entropy_count;
	int input_rotate;
};

/*
 * 初始化熵存储。输入参数是随机池的大小。
 *
 * 如果有问题，返回一个负错误。
 */
static int create_entropy_store(int size, const char *name,
				struct entropy_store **ret_bucket)
{
	struct entropy_store *r;
	struct poolinfo *p;
	int poolwords;

	poolwords = (size + 3) / 4; /* 转换字节->字 */
	/* 池大小必须是 16 个 32 位字的倍数 */
	poolwords = ((poolwords + 15) / 16) * 16;

	for (p = poolinfo_table; p->poolwords; p++) {
		if (poolwords == p->poolwords)
			break;
	}
	if (p->poolwords == 0)
		return -EINVAL;

	r = kmalloc(sizeof(struct entropy_store), GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	memset (r, 0, sizeof(struct entropy_store));
	r->poolinfo = *p;	// 复制

	r->pool = kmalloc(POOLBYTES, GFP_KERNEL);		// POOLBYTES为poolwords*4
	if (!r->pool) {
		kfree(r);
		return -ENOMEM;
	}
	memset(r->pool, 0, POOLBYTES);
	spin_lock_init(&r->lock);
	r->name = name;
	*ret_bucket = r;
	return 0;
}

/* 清除熵池和相关计数器. */
static void clear_entropy_store(struct entropy_store *r)
{
	r->add_ptr = 0;
	r->entropy_count = 0;
	r->input_rotate = 0;
	memset(r->pool, 0, r->poolinfo.POOLBYTES);
}

/*
 * 此函数将一个字节添加到熵“池”中。它不更新熵估计。
 * 如果合适，调用者应该调用 credit_entropy_store。
 *
 *池用适当次数的原始多项式搅拌，然后扭曲。
 * 我们一次扭曲三位，因为这样做很便宜，并且在熵集中在低阶位的预期情况下略有帮助。
 */
static void __add_entropy_words(struct entropy_store *r, const __u32 *in,
				int nwords, __u32 out[16])
{
	static __u32 const twist_table[8] = {
		0x00000000, 0x3b6e20c8, 0x76dc4190, 0x4db26158,
		0xedb88320, 0xd6d6a3e8, 0x9b64c2b0, 0xa00ae278 };
	unsigned long i, add_ptr, tap1, tap2, tap3, tap4, tap5;
	int new_rotate, input_rotate;
	int wordmask = r->poolinfo.poolwords - 1;
	__u32 w, next_w;
	unsigned long flags;

	/* Taps 是恒定的，所以我们可以在不按住 r->lock 的情况下加载它们.  */
	tap1 = r->poolinfo.tap1;
	tap2 = r->poolinfo.tap2;
	tap3 = r->poolinfo.tap3;
	tap4 = r->poolinfo.tap4;
	tap5 = r->poolinfo.tap5;
	next_w = *in++;

	spin_lock_irqsave(&r->lock, flags);
	prefetch_range(r->pool, wordmask);
	input_rotate = r->input_rotate;
	add_ptr = r->add_ptr;

	while (nwords--) {
		w = rotate_left(input_rotate, next_w);
		if (nwords > 0)
			next_w = *in++;
		i = add_ptr = (add_ptr - 1) & wordmask;
		/*
		 * 通常，我们向池中添加 7 位旋转。
		 *在池的开头，添加额外的 7 位轮换，以便连续传递将输入位均匀地分布在池中。
		 */
		new_rotate = input_rotate + 14;
		if (i)
			new_rotate = input_rotate + 7;
		input_rotate = new_rotate & 31;

		/* XOR in the various taps */
		w ^= r->pool[(i + tap1) & wordmask];
		w ^= r->pool[(i + tap2) & wordmask];
		w ^= r->pool[(i + tap3) & wordmask];
		w ^= r->pool[(i + tap4) & wordmask];
		w ^= r->pool[(i + tap5) & wordmask];
		w ^= r->pool[i];
		r->pool[i] = (w >> 3) ^ twist_table[w & 7];
	}

	r->input_rotate = input_rotate;
	r->add_ptr = add_ptr;

	if (out) {
		for (i = 0; i < 16; i++) {
			out[i] = r->pool[add_ptr];
			add_ptr = (add_ptr - 1) & wordmask;
		}
	}

	spin_unlock_irqrestore(&r->lock, flags);
}

static inline void add_entropy_words(struct entropy_store *r, const __u32 *in,
				     int nwords)
{
	__add_entropy_words(r, in, nwords, NULL);
}

/*
 * Credit (or debit) the entropy store with n bits of entropy
 */
static void credit_entropy_store(struct entropy_store *r, int nbits)
{
	unsigned long flags;

	spin_lock_irqsave(&r->lock, flags);

	if (r->entropy_count + nbits < 0) {
		DEBUG_ENT("negative entropy/overflow (%d+%d)\n",
			  r->entropy_count, nbits);
		r->entropy_count = 0;
	} else if (r->entropy_count + nbits > r->poolinfo.POOLBITS) {
		r->entropy_count = r->poolinfo.POOLBITS;
	} else {
		r->entropy_count += nbits;
		if (nbits)
			DEBUG_ENT("added %d entropy credits to %s\n",
				  nbits, r->name);
	}

	spin_unlock_irqrestore(&r->lock, flags);
}

/**********************************************************************
 *
 * 熵批量输入管理
 *
 * 我们批量添加熵以避免增加中断延迟
 *
 **********************************************************************/

struct sample {
	__u32 data[2];
	int credit;
};

static struct sample *batch_entropy_pool, *batch_entropy_copy;
static int batch_head, batch_tail;
static DEFINE_SPINLOCK(batch_lock);

static int batch_max;
static void batch_entropy_process(void *private_);
static DECLARE_WORK(batch_work, batch_entropy_process, NULL);

/* note: the size must be a power of 2 */
static int __init batch_entropy_init(int size, struct entropy_store *r)
{
	batch_entropy_pool = kmalloc(size*sizeof(struct sample), GFP_KERNEL);
	if (!batch_entropy_pool)
		return -1;
	batch_entropy_copy = kmalloc(size*sizeof(struct sample), GFP_KERNEL);
	if (!batch_entropy_copy) {
		kfree(batch_entropy_pool);
		return -1;
	}
	batch_head = batch_tail = 0;
	batch_work.data = r;
	batch_max = size;
	return 0;
}

/*
 * Changes to the entropy data is put into a queue rather than being added to
 * the entropy counts directly.  This is presumably to avoid doing heavy
 * hashing calculations during an interrupt in add_timer_randomness().
 * Instead, the entropy is only added to the pool by keventd.
 */
static void batch_entropy_store(u32 a, u32 b, int num)
{
	int new;
	unsigned long flags;

	if (!batch_max)
		return;

	spin_lock_irqsave(&batch_lock, flags);

	batch_entropy_pool[batch_head].data[0] = a;
	batch_entropy_pool[batch_head].data[1] = b;
	batch_entropy_pool[batch_head].credit = num;

	if (((batch_head - batch_tail) & (batch_max - 1)) >= (batch_max / 2))
		schedule_delayed_work(&batch_work, 1);

	new = (batch_head + 1) & (batch_max - 1);
	if (new == batch_tail)
		DEBUG_ENT("batch entropy buffer full\n");
	else
		batch_head = new;

	spin_unlock_irqrestore(&batch_lock, flags);
}

/*
 * Flush out the accumulated entropy operations, adding entropy to the passed
 * store (normally random_state).  If that store has enough entropy, alternate
 * between randomizing the data of the primary and secondary stores.
 */
static void batch_entropy_process(void *private_)
{
	struct entropy_store *r	= (struct entropy_store *) private_, *p;
	int max_entropy = r->poolinfo.POOLBITS;
	unsigned head, tail;

	/* Mixing into the pool is expensive, so copy over the batch
	 * data and release the batch lock. The pool is at least half
	 * full, so don't worry too much about copying only the used
	 * part.
	 */
	spin_lock_irq(&batch_lock);

	memcpy(batch_entropy_copy, batch_entropy_pool,
	       batch_max * sizeof(struct sample));

	head = batch_head;
	tail = batch_tail;
	batch_tail = batch_head;

	spin_unlock_irq(&batch_lock);

	p = r;
	while (head != tail) {
		if (r->entropy_count >= max_entropy) {
			r = (r == sec_random_state) ? random_state :
				sec_random_state;
			max_entropy = r->poolinfo.POOLBITS;
		}
		add_entropy_words(r, batch_entropy_copy[tail].data, 2);
		credit_entropy_store(r, batch_entropy_copy[tail].credit);
		tail = (tail + 1) & (batch_max - 1);
	}
	if (p->entropy_count >= random_read_wakeup_thresh)
		wake_up_interruptible(&random_read_wait);
}

/*********************************************************************
 *
 * Entropy input management
 *
 *********************************************************************/

/* There is one of these per entropy source */
struct timer_rand_state {
	cycles_t last_time;
	long last_delta,last_delta2;
	unsigned dont_count_entropy:1;
};

static struct timer_rand_state input_timer_state;
static struct timer_rand_state extract_timer_state;
static struct timer_rand_state *irq_timer_state[NR_IRQS];

/*
 * This function adds entropy to the entropy "pool" by using timing
 * delays.  It uses the timer_rand_state structure to make an estimate
 * of how many bits of entropy this call has added to the pool.
 *
 * The number "num" is also added to the pool - it should somehow describe
 * the type of event which just happened.  This is currently 0-255 for
 * keyboard scan codes, and 256 upwards for interrupts.
 *
 */
static void add_timer_randomness(struct timer_rand_state *state, unsigned num)
{
	cycles_t data;
	long delta, delta2, delta3, time;
	int entropy = 0;

	preempt_disable();
	/* if over the trickle threshold, use only 1 in 4096 samples */
	if (random_state->entropy_count > trickle_thresh &&
	    (__get_cpu_var(trickle_count)++ & 0xfff))
		goto out;

	/*
	 * Calculate number of bits of randomness we probably added.
	 * We take into account the first, second and third-order deltas
	 * in order to make our estimate.
	 */
	time = jiffies;

	if (!state->dont_count_entropy) {
		delta = time - state->last_time;
		state->last_time = time;

		delta2 = delta - state->last_delta;
		state->last_delta = delta;

		delta3 = delta2 - state->last_delta2;
		state->last_delta2 = delta2;

		if (delta < 0)
			delta = -delta;
		if (delta2 < 0)
			delta2 = -delta2;
		if (delta3 < 0)
			delta3 = -delta3;
		if (delta > delta2)
			delta = delta2;
		if (delta > delta3)
			delta = delta3;

		/*
		 * delta is now minimum absolute delta.
		 * Round down by 1 bit on general principles,
		 * and limit entropy entimate to 12 bits.
		 */
		delta >>= 1;
		delta &= (1 << 12) - 1;

		entropy = int_ln_12bits(delta);
	}

	/*
	 * Use get_cycles() if implemented, otherwise fall back to
	 * jiffies.
	 */
	data = get_cycles();
	if (data)
		num ^= (u32)((data >> 31) >> 1);
	else
		data = time;

	batch_entropy_store(num, data, entropy);
out:
	preempt_enable();
}

extern void add_input_randomness(unsigned int type, unsigned int code,
				 unsigned int value)
{
	static unsigned char last_value;

	/* ignore autorepeat and the like */
	if (value == last_value)
		return;

	DEBUG_ENT("input event\n");
	last_value = value;
	add_timer_randomness(&input_timer_state,
			     (type << 4) ^ code ^ (code >> 4) ^ value);
}

void add_interrupt_randomness(int irq)
{
	if (irq >= NR_IRQS || irq_timer_state[irq] == 0)
		return;

	DEBUG_ENT("irq event %d\n", irq);
	add_timer_randomness(irq_timer_state[irq], 0x100 + irq);
}

void add_disk_randomness(struct gendisk *disk)
{
	if (!disk || !disk->random)
		return;
	/* first major is 1, so we get >= 0x200 here */
	DEBUG_ENT("disk event %d:%d\n", disk->major, disk->first_minor);

	add_timer_randomness(disk->random,
			     0x100 + MKDEV(disk->major, disk->first_minor));
}

EXPORT_SYMBOL(add_disk_randomness);

/******************************************************************
 *
 * Hash function definition
 *
 *******************************************************************/

/*
 * This chunk of code defines a function
 * void HASH_TRANSFORM(__u32 digest[HASH_BUFFER_SIZE + HASH_EXTRA_SIZE],
 * 		__u32 const data[16])
 *
 * The function hashes the input data to produce a digest in the first
 * HASH_BUFFER_SIZE words of the digest[] array, and uses HASH_EXTRA_SIZE
 * more words for internal purposes.  (This buffer is exported so the
 * caller can wipe it once rather than this code doing it each call,
 * and tacking it onto the end of the digest[] array is the quick and
 * dirty way of doing it.)
 *
 * It so happens that MD5 and SHA share most of the initial vector
 * used to initialize the digest[] array before the first call:
 * 1) 0x67452301
 * 2) 0xefcdab89
 * 3) 0x98badcfe
 * 4) 0x10325476
 * 5) 0xc3d2e1f0 (SHA only)
 *
 * For /dev/random purposes, the length of the data being hashed is
 * fixed in length, so appending a bit count in the usual way is not
 * cryptographically necessary.
 */

#ifdef USE_SHA

#define HASH_BUFFER_SIZE 5
#define HASH_EXTRA_SIZE 80
#define HASH_TRANSFORM SHATransform

/* Various size/speed tradeoffs are available.  Choose 0..3. */
#define SHA_CODE_SIZE 0

/*
 * SHA transform algorithm, taken from code written by Peter Gutmann,
 * and placed in the public domain.
 */

/* The SHA f()-functions.  */

#define f1(x,y,z)   (z ^ (x & (y ^ z)))		/* Rounds  0-19: x ? y : z */
#define f2(x,y,z)   (x ^ y ^ z)			/* Rounds 20-39: XOR */
#define f3(x,y,z)   ((x & y) + (z & (x ^ y)))	/* Rounds 40-59: majority */
#define f4(x,y,z)   (x ^ y ^ z)			/* Rounds 60-79: XOR */

/* The SHA Mysterious Constants */

#define K1  0x5A827999L			/* Rounds  0-19: sqrt(2) * 2^30 */
#define K2  0x6ED9EBA1L			/* Rounds 20-39: sqrt(3) * 2^30 */
#define K3  0x8F1BBCDCL			/* Rounds 40-59: sqrt(5) * 2^30 */
#define K4  0xCA62C1D6L			/* Rounds 60-79: sqrt(10) * 2^30 */

#define ROTL(n,X)  (((X) << n ) | ((X) >> (32 - n)))

#define subRound(a, b, c, d, e, f, k, data) \
    (e += ROTL(5, a) + f(b, c, d) + k + data, b = ROTL(30, b))

static void SHATransform(__u32 digest[85], __u32 const data[16])
{
	__u32 A, B, C, D, E;     /* Local vars */
	__u32 TEMP;
	int i;
#define W (digest + HASH_BUFFER_SIZE)	/* Expanded data array */

	/*
	 * Do the preliminary expansion of 16 to 80 words.  Doing it
	 * out-of-line line this is faster than doing it in-line on
	 * register-starved machines like the x86, and not really any
	 * slower on real processors.
	 */
	memcpy(W, data, 16*sizeof(__u32));
	for (i = 0; i < 64; i++) {
		TEMP = W[i] ^ W[i+2] ^ W[i+8] ^ W[i+13];
		W[i+16] = ROTL(1, TEMP);
	}

	/* Set up first buffer and local data buffer */
	A = digest[ 0 ];
	B = digest[ 1 ];
	C = digest[ 2 ];
	D = digest[ 3 ];
	E = digest[ 4 ];

	/* Heavy mangling, in 4 sub-rounds of 20 iterations each. */
#if SHA_CODE_SIZE == 0
	/*
	 * Approximately 50% of the speed of the largest version, but
	 * takes up 1/16 the space.  Saves about 6k on an i386 kernel.
	 */
	for (i = 0; i < 80; i++) {
		if (i < 40) {
			if (i < 20)
				TEMP = f1(B, C, D) + K1;
			else
				TEMP = f2(B, C, D) + K2;
		} else {
			if (i < 60)
				TEMP = f3(B, C, D) + K3;
			else
				TEMP = f4(B, C, D) + K4;
		}
		TEMP += ROTL(5, A) + E + W[i];
		E = D; D = C; C = ROTL(30, B); B = A; A = TEMP;
	}
#elif SHA_CODE_SIZE == 1
	for (i = 0; i < 20; i++) {
		TEMP = f1(B, C, D) + K1 + ROTL(5, A) + E + W[i];
		E = D; D = C; C = ROTL(30, B); B = A; A = TEMP;
	}
	for (; i < 40; i++) {
		TEMP = f2(B, C, D) + K2 + ROTL(5, A) + E + W[i];
		E = D; D = C; C = ROTL(30, B); B = A; A = TEMP;
	}
	for (; i < 60; i++) {
		TEMP = f3(B, C, D) + K3 + ROTL(5, A) + E + W[i];
		E = D; D = C; C = ROTL(30, B); B = A; A = TEMP;
	}
	for (; i < 80; i++) {
		TEMP = f4(B, C, D) + K4 + ROTL(5, A) + E + W[i];
		E = D; D = C; C = ROTL(30, B); B = A; A = TEMP;
	}
#elif SHA_CODE_SIZE == 2
	for (i = 0; i < 20; i += 5) {
		subRound(A, B, C, D, E, f1, K1, W[i  ]);
		subRound(E, A, B, C, D, f1, K1, W[i+1]);
		subRound(D, E, A, B, C, f1, K1, W[i+2]);
		subRound(C, D, E, A, B, f1, K1, W[i+3]);
		subRound(B, C, D, E, A, f1, K1, W[i+4]);
	}
	for (; i < 40; i += 5) {
		subRound(A, B, C, D, E, f2, K2, W[i  ]);
		subRound(E, A, B, C, D, f2, K2, W[i+1]);
		subRound(D, E, A, B, C, f2, K2, W[i+2]);
		subRound(C, D, E, A, B, f2, K2, W[i+3]);
		subRound(B, C, D, E, A, f2, K2, W[i+4]);
	}
	for (; i < 60; i += 5) {
		subRound(A, B, C, D, E, f3, K3, W[i  ]);
		subRound(E, A, B, C, D, f3, K3, W[i+1]);
		subRound(D, E, A, B, C, f3, K3, W[i+2]);
		subRound(C, D, E, A, B, f3, K3, W[i+3]);
		subRound(B, C, D, E, A, f3, K3, W[i+4]);
	}
	for (; i < 80; i += 5) {
		subRound(A, B, C, D, E, f4, K4, W[i  ]);
		subRound(E, A, B, C, D, f4, K4, W[i+1]);
		subRound(D, E, A, B, C, f4, K4, W[i+2]);
		subRound(C, D, E, A, B, f4, K4, W[i+3]);
		subRound(B, C, D, E, A, f4, K4, W[i+4]);
	}
#elif SHA_CODE_SIZE == 3 /* Really large version */
	subRound(A, B, C, D, E, f1, K1, W[ 0]);
	subRound(E, A, B, C, D, f1, K1, W[ 1]);
	subRound(D, E, A, B, C, f1, K1, W[ 2]);
	subRound(C, D, E, A, B, f1, K1, W[ 3]);
	subRound(B, C, D, E, A, f1, K1, W[ 4]);
	subRound(A, B, C, D, E, f1, K1, W[ 5]);
	subRound(E, A, B, C, D, f1, K1, W[ 6]);
	subRound(D, E, A, B, C, f1, K1, W[ 7]);
	subRound(C, D, E, A, B, f1, K1, W[ 8]);
	subRound(B, C, D, E, A, f1, K1, W[ 9]);
	subRound(A, B, C, D, E, f1, K1, W[10]);
	subRound(E, A, B, C, D, f1, K1, W[11]);
	subRound(D, E, A, B, C, f1, K1, W[12]);
	subRound(C, D, E, A, B, f1, K1, W[13]);
	subRound(B, C, D, E, A, f1, K1, W[14]);
	subRound(A, B, C, D, E, f1, K1, W[15]);
	subRound(E, A, B, C, D, f1, K1, W[16]);
	subRound(D, E, A, B, C, f1, K1, W[17]);
	subRound(C, D, E, A, B, f1, K1, W[18]);
	subRound(B, C, D, E, A, f1, K1, W[19]);

	subRound(A, B, C, D, E, f2, K2, W[20]);
	subRound(E, A, B, C, D, f2, K2, W[21]);
	subRound(D, E, A, B, C, f2, K2, W[22]);
	subRound(C, D, E, A, B, f2, K2, W[23]);
	subRound(B, C, D, E, A, f2, K2, W[24]);
	subRound(A, B, C, D, E, f2, K2, W[25]);
	subRound(E, A, B, C, D, f2, K2, W[26]);
	subRound(D, E, A, B, C, f2, K2, W[27]);
	subRound(C, D, E, A, B, f2, K2, W[28]);
	subRound(B, C, D, E, A, f2, K2, W[29]);
	subRound(A, B, C, D, E, f2, K2, W[30]);
	subRound(E, A, B, C, D, f2, K2, W[31]);
	subRound(D, E, A, B, C, f2, K2, W[32]);
	subRound(C, D, E, A, B, f2, K2, W[33]);
	subRound(B, C, D, E, A, f2, K2, W[34]);
	subRound(A, B, C, D, E, f2, K2, W[35]);
	subRound(E, A, B, C, D, f2, K2, W[36]);
	subRound(D, E, A, B, C, f2, K2, W[37]);
	subRound(C, D, E, A, B, f2, K2, W[38]);
	subRound(B, C, D, E, A, f2, K2, W[39]);

	subRound(A, B, C, D, E, f3, K3, W[40]);
	subRound(E, A, B, C, D, f3, K3, W[41]);
	subRound(D, E, A, B, C, f3, K3, W[42]);
	subRound(C, D, E, A, B, f3, K3, W[43]);
	subRound(B, C, D, E, A, f3, K3, W[44]);
	subRound(A, B, C, D, E, f3, K3, W[45]);
	subRound(E, A, B, C, D, f3, K3, W[46]);
	subRound(D, E, A, B, C, f3, K3, W[47]);
	subRound(C, D, E, A, B, f3, K3, W[48]);
	subRound(B, C, D, E, A, f3, K3, W[49]);
	subRound(A, B, C, D, E, f3, K3, W[50]);
	subRound(E, A, B, C, D, f3, K3, W[51]);
	subRound(D, E, A, B, C, f3, K3, W[52]);
	subRound(C, D, E, A, B, f3, K3, W[53]);
	subRound(B, C, D, E, A, f3, K3, W[54]);
	subRound(A, B, C, D, E, f3, K3, W[55]);
	subRound(E, A, B, C, D, f3, K3, W[56]);
	subRound(D, E, A, B, C, f3, K3, W[57]);
	subRound(C, D, E, A, B, f3, K3, W[58]);
	subRound(B, C, D, E, A, f3, K3, W[59]);

	subRound(A, B, C, D, E, f4, K4, W[60]);
	subRound(E, A, B, C, D, f4, K4, W[61]);
	subRound(D, E, A, B, C, f4, K4, W[62]);
	subRound(C, D, E, A, B, f4, K4, W[63]);
	subRound(B, C, D, E, A, f4, K4, W[64]);
	subRound(A, B, C, D, E, f4, K4, W[65]);
	subRound(E, A, B, C, D, f4, K4, W[66]);
	subRound(D, E, A, B, C, f4, K4, W[67]);
	subRound(C, D, E, A, B, f4, K4, W[68]);
	subRound(B, C, D, E, A, f4, K4, W[69]);
	subRound(A, B, C, D, E, f4, K4, W[70]);
	subRound(E, A, B, C, D, f4, K4, W[71]);
	subRound(D, E, A, B, C, f4, K4, W[72]);
	subRound(C, D, E, A, B, f4, K4, W[73]);
	subRound(B, C, D, E, A, f4, K4, W[74]);
	subRound(A, B, C, D, E, f4, K4, W[75]);
	subRound(E, A, B, C, D, f4, K4, W[76]);
	subRound(D, E, A, B, C, f4, K4, W[77]);
	subRound(C, D, E, A, B, f4, K4, W[78]);
	subRound(B, C, D, E, A, f4, K4, W[79]);
#else
#error Illegal SHA_CODE_SIZE
#endif

	/* Build message digest */
	digest[0] += A;
	digest[1] += B;
	digest[2] += C;
	digest[3] += D;
	digest[4] += E;

	/* W is wiped by the caller */
#undef W
}

#undef ROTL
#undef f1
#undef f2
#undef f3
#undef f4
#undef K1
#undef K2
#undef K3
#undef K4
#undef subRound

#else /* !USE_SHA - Use MD5 */

#define HASH_BUFFER_SIZE 4
#define HASH_EXTRA_SIZE 0
#define HASH_TRANSFORM MD5Transform

/*
 * MD5 transform algorithm, taken from code written by Colin Plumb,
 * and put into the public domain
 */

/* The four core functions - F1 is optimized somewhat */

/* #define F1(x, y, z) (x & y | ~x & z) */
#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

/* This is the central step in the MD5 algorithm. */
#define MD5STEP(f, w, x, y, z, data, s) \
	(w += f(x, y, z) + data,  w = w << s | w >> (32 - s),  w += x )

/*
 * The core of the MD5 algorithm, this alters an existing MD5 hash to
 * reflect the addition of 16 longwords of new data.  MD5Update blocks
 * the data and converts bytes into longwords for this routine.
 */
static void MD5Transform(__u32 buf[HASH_BUFFER_SIZE], __u32 const in[16])
{
	__u32 a, b, c, d;

	a = buf[0];
	b = buf[1];
	c = buf[2];
	d = buf[3];

	MD5STEP(F1, a, b, c, d, in[ 0]+0xd76aa478,  7);
	MD5STEP(F1, d, a, b, c, in[ 1]+0xe8c7b756, 12);
	MD5STEP(F1, c, d, a, b, in[ 2]+0x242070db, 17);
	MD5STEP(F1, b, c, d, a, in[ 3]+0xc1bdceee, 22);
	MD5STEP(F1, a, b, c, d, in[ 4]+0xf57c0faf,  7);
	MD5STEP(F1, d, a, b, c, in[ 5]+0x4787c62a, 12);
	MD5STEP(F1, c, d, a, b, in[ 6]+0xa8304613, 17);
	MD5STEP(F1, b, c, d, a, in[ 7]+0xfd469501, 22);
	MD5STEP(F1, a, b, c, d, in[ 8]+0x698098d8,  7);
	MD5STEP(F1, d, a, b, c, in[ 9]+0x8b44f7af, 12);
	MD5STEP(F1, c, d, a, b, in[10]+0xffff5bb1, 17);
	MD5STEP(F1, b, c, d, a, in[11]+0x895cd7be, 22);
	MD5STEP(F1, a, b, c, d, in[12]+0x6b901122,  7);
	MD5STEP(F1, d, a, b, c, in[13]+0xfd987193, 12);
	MD5STEP(F1, c, d, a, b, in[14]+0xa679438e, 17);
	MD5STEP(F1, b, c, d, a, in[15]+0x49b40821, 22);

	MD5STEP(F2, a, b, c, d, in[ 1]+0xf61e2562,  5);
	MD5STEP(F2, d, a, b, c, in[ 6]+0xc040b340,  9);
	MD5STEP(F2, c, d, a, b, in[11]+0x265e5a51, 14);
	MD5STEP(F2, b, c, d, a, in[ 0]+0xe9b6c7aa, 20);
	MD5STEP(F2, a, b, c, d, in[ 5]+0xd62f105d,  5);
	MD5STEP(F2, d, a, b, c, in[10]+0x02441453,  9);
	MD5STEP(F2, c, d, a, b, in[15]+0xd8a1e681, 14);
	MD5STEP(F2, b, c, d, a, in[ 4]+0xe7d3fbc8, 20);
	MD5STEP(F2, a, b, c, d, in[ 9]+0x21e1cde6,  5);
	MD5STEP(F2, d, a, b, c, in[14]+0xc33707d6,  9);
	MD5STEP(F2, c, d, a, b, in[ 3]+0xf4d50d87, 14);
	MD5STEP(F2, b, c, d, a, in[ 8]+0x455a14ed, 20);
	MD5STEP(F2, a, b, c, d, in[13]+0xa9e3e905,  5);
	MD5STEP(F2, d, a, b, c, in[ 2]+0xfcefa3f8,  9);
	MD5STEP(F2, c, d, a, b, in[ 7]+0x676f02d9, 14);
	MD5STEP(F2, b, c, d, a, in[12]+0x8d2a4c8a, 20);

	MD5STEP(F3, a, b, c, d, in[ 5]+0xfffa3942,  4);
	MD5STEP(F3, d, a, b, c, in[ 8]+0x8771f681, 11);
	MD5STEP(F3, c, d, a, b, in[11]+0x6d9d6122, 16);
	MD5STEP(F3, b, c, d, a, in[14]+0xfde5380c, 23);
	MD5STEP(F3, a, b, c, d, in[ 1]+0xa4beea44,  4);
	MD5STEP(F3, d, a, b, c, in[ 4]+0x4bdecfa9, 11);
	MD5STEP(F3, c, d, a, b, in[ 7]+0xf6bb4b60, 16);
	MD5STEP(F3, b, c, d, a, in[10]+0xbebfbc70, 23);
	MD5STEP(F3, a, b, c, d, in[13]+0x289b7ec6,  4);
	MD5STEP(F3, d, a, b, c, in[ 0]+0xeaa127fa, 11);
	MD5STEP(F3, c, d, a, b, in[ 3]+0xd4ef3085, 16);
	MD5STEP(F3, b, c, d, a, in[ 6]+0x04881d05, 23);
	MD5STEP(F3, a, b, c, d, in[ 9]+0xd9d4d039,  4);
	MD5STEP(F3, d, a, b, c, in[12]+0xe6db99e5, 11);
	MD5STEP(F3, c, d, a, b, in[15]+0x1fa27cf8, 16);
	MD5STEP(F3, b, c, d, a, in[ 2]+0xc4ac5665, 23);

	MD5STEP(F4, a, b, c, d, in[ 0]+0xf4292244,  6);
	MD5STEP(F4, d, a, b, c, in[ 7]+0x432aff97, 10);
	MD5STEP(F4, c, d, a, b, in[14]+0xab9423a7, 15);
	MD5STEP(F4, b, c, d, a, in[ 5]+0xfc93a039, 21);
	MD5STEP(F4, a, b, c, d, in[12]+0x655b59c3,  6);
	MD5STEP(F4, d, a, b, c, in[ 3]+0x8f0ccc92, 10);
	MD5STEP(F4, c, d, a, b, in[10]+0xffeff47d, 15);
	MD5STEP(F4, b, c, d, a, in[ 1]+0x85845dd1, 21);
	MD5STEP(F4, a, b, c, d, in[ 8]+0x6fa87e4f,  6);
	MD5STEP(F4, d, a, b, c, in[15]+0xfe2ce6e0, 10);
	MD5STEP(F4, c, d, a, b, in[ 6]+0xa3014314, 15);
	MD5STEP(F4, b, c, d, a, in[13]+0x4e0811a1, 21);
	MD5STEP(F4, a, b, c, d, in[ 4]+0xf7537e82,  6);
	MD5STEP(F4, d, a, b, c, in[11]+0xbd3af235, 10);
	MD5STEP(F4, c, d, a, b, in[ 2]+0x2ad7d2bb, 15);
	MD5STEP(F4, b, c, d, a, in[ 9]+0xeb86d391, 21);

	buf[0] += a;
	buf[1] += b;
	buf[2] += c;
	buf[3] += d;
}

#undef F1
#undef F2
#undef F3
#undef F4
#undef MD5STEP

#endif /* !USE_SHA */

/*********************************************************************
 *
 * Entropy extraction routines
 *
 *********************************************************************/

#define EXTRACT_ENTROPY_USER		1		// 假设buff位于用户空间
#define EXTRACT_ENTROPY_SECONDARY	2		// 辅助熵池
#define EXTRACT_ENTROPY_LIMIT		4
#define TMP_BUF_SIZE			(HASH_BUFFER_SIZE + HASH_EXTRA_SIZE)		// 5+80
#define SEC_XFER_SIZE			(TMP_BUF_SIZE*4)

static ssize_t extract_entropy(struct entropy_store *r, void * buf,
			       size_t nbytes, int flags);

/*
 * This utility inline function is responsible for transfering entropy
 * from the primary pool to the secondary extraction pool. We make
 * sure we pull enough for a 'catastrophic reseed'.
 */
static inline void xfer_secondary_pool(struct entropy_store *r,
				       size_t nbytes, __u32 *tmp)
{
	if (r->entropy_count < nbytes * 8 &&
	    r->entropy_count < r->poolinfo.POOLBITS) {
		int bytes = max_t(int, random_read_wakeup_thresh / 8,
				min_t(int, nbytes, TMP_BUF_SIZE));

		DEBUG_ENT("going to reseed %s with %d bits "
			  "(%d of %d requested)\n",
			  r->name, bytes * 8, nbytes * 8, r->entropy_count);

		bytes=extract_entropy(random_state, tmp, bytes,
				      EXTRACT_ENTROPY_LIMIT);
		add_entropy_words(r, tmp, (bytes + 3) / 4);
		credit_entropy_store(r, bytes*8);
	}
}

/*
 * 此函数从“熵池”中提取随机性，并将其返回到缓冲区中。
 * 该函数计算池中剩余多少位熵，但它不限制实际获得的字节数。
 * 如果给出了 EXTRACT_ENTROPY_USER 标志，则假定 buf 指针位于用户空间中。
 *
 * 如果给出了 EXTRACT_ENTROPY_SECONDARY 标志，那么我们实际上是从辅助池中提取熵，
 * 如果需要，可以从主池中重新填充。
 *
 * 注意：extract_entropy() 假设 .poolwords 是 16 个字的倍数。
 */
static ssize_t extract_entropy(struct entropy_store *r, void * buf,
			       size_t nbytes, int flags)
{
	ssize_t ret, i;
	__u32 tmp[TMP_BUF_SIZE], data[16];		// TMP_BUF_SIZE:85
	__u32 x;
	unsigned long cpuflags;

	/*冗余，但以防万一... */
	if (r->entropy_count > r->poolinfo.POOLBITS)
		r->entropy_count = r->poolinfo.POOLBITS;

	if (flags & EXTRACT_ENTROPY_SECONDARY)
		xfer_secondary_pool(r, nbytes, tmp);

	/* Hold lock while accounting */
	spin_lock_irqsave(&r->lock, cpuflags);

	DEBUG_ENT("trying to extract %d bits from %s\n", nbytes * 8, r->name);

	if (flags & EXTRACT_ENTROPY_LIMIT && nbytes >= r->entropy_count / 8)
		nbytes = r->entropy_count / 8;

	if (r->entropy_count / 8 >= nbytes)
		r->entropy_count -= nbytes*8;
	else
		r->entropy_count = 0;

	if (r->entropy_count < random_write_wakeup_thresh)
		wake_up_interruptible(&random_write_wait);

	DEBUG_ENT("debiting %d entropy credits from %s%s\n",
		  nbytes * 8, r->name,
		  flags & EXTRACT_ENTROPY_LIMIT ? "" : " (unlimited)");

	spin_unlock_irqrestore(&r->lock, cpuflags);

	ret = 0;
	while (nbytes) {
		/*
		 * Check if we need to break out or reschedule....
		 */
		if ((flags & EXTRACT_ENTROPY_USER) && need_resched()) {
			if (signal_pending(current)) {
				if (ret == 0)
					ret = -ERESTARTSYS;
				break;
			}

			schedule();
		}

		/* Hash the pool to get the output */
		tmp[0] = 0x67452301;
		tmp[1] = 0xefcdab89;
		tmp[2] = 0x98badcfe;
		tmp[3] = 0x10325476;
#ifdef USE_SHA
		tmp[4] = 0xc3d2e1f0;
#endif
		/*
		 * As we hash the pool, we mix intermediate values of
		 * the hash back into the pool.  This eliminates
		 * backtracking attacks (where the attacker knows
		 * the state of the pool plus the current outputs, and
		 * attempts to find previous ouputs), unless the hash
		 * function can be inverted.
		 */
		for (i = 0, x = 0; i < r->poolinfo.poolwords; i += 16, x+=2) {
			HASH_TRANSFORM(tmp, r->pool+i);
			add_entropy_words(r, &tmp[x%HASH_BUFFER_SIZE], 1);
		}

		/*
		 * To avoid duplicates, we atomically extract a
		 * portion of the pool while mixing, and hash one
		 * final time.
		 */
		__add_entropy_words(r, &tmp[x%HASH_BUFFER_SIZE], 1, data);
		HASH_TRANSFORM(tmp, data);

		/*
		 * In case the hash function has some recognizable
		 * output pattern, we fold it in half.
		 */
		for (i = 0; i <  HASH_BUFFER_SIZE/2; i++)
			tmp[i] ^= tmp[i + (HASH_BUFFER_SIZE+1)/2];
#if HASH_BUFFER_SIZE & 1	/* There's a middle word to deal with */
		x = tmp[HASH_BUFFER_SIZE/2];
		x ^= (x >> 16);		/* Fold it in half */
		((__u16 *)tmp)[HASH_BUFFER_SIZE-1] = (__u16)x;
#endif

		/* Copy data to destination buffer */
		i = min(nbytes, HASH_BUFFER_SIZE*sizeof(__u32)/2);
		if (flags & EXTRACT_ENTROPY_USER) {
			i -= copy_to_user(buf, (__u8 const *)tmp, i);
			if (!i) {
				ret = -EFAULT;
				break;
			}
		} else
			memcpy(buf, (__u8 const *)tmp, i);

		nbytes -= i;
		buf += i;
		ret += i;
	}

	/* Wipe data just returned from memory */
	memset(tmp, 0, sizeof(tmp));

	return ret;
}

/*
 * 该函数是导出的内核接口。它返回一些好的随机数，适用于播种 TCP 序列号等。
 *
 * 获取随机数
 */
void get_random_bytes(void *buf, int nbytes)
{
	struct entropy_store *r = urandom_state;
	int flags = EXTRACT_ENTROPY_SECONDARY;

	if (!r)
		r = sec_random_state;
	if (!r) {
		r = random_state;
		flags = 0;
	}
	if (!r) {
		printk(KERN_NOTICE "get_random_bytes called before "
				   "random driver initialization\n");
		return;
	}
	extract_entropy(r, (char *) buf, nbytes, flags);
}

EXPORT_SYMBOL(get_random_bytes);

/*********************************************************************
 *
 * Functions to interface with Linux
 *
 *********************************************************************/

/*
 * 用标准的东西初始化随机池。
 *
 *注意：这是一个依赖于操作系统的功能。
 */
static void init_std_data(struct entropy_store *r)
{
	struct timeval tv;
	__u32 words[2];
	char *p;
	int i;

	do_gettimeofday(&tv);
	words[0] = tv.tv_sec;
	words[1] = tv.tv_usec;
	add_entropy_words(r, words, 2);

	/*
	 *	这不会锁定 system.utsname。但是，我们正在生成熵，因此此处设置名称的比赛很好。
	 */
	p = (char *) &system_utsname;
	for (i = sizeof(system_utsname) / sizeof(words); i; i--) {
		memcpy(words, p, sizeof(words));
		add_entropy_words(r, words, sizeof(words)/4);
		p += sizeof(words);
	}
}

/* 熵池初始化
 * */
static int __init rand_initialize(void)
{
	int i;

	if (create_entropy_store(DEFAULT_POOL_SIZE, "primary", &random_state))
		goto err;
	if (batch_entropy_init(BATCH_ENTROPY_SIZE, random_state))
		goto err;
	if (create_entropy_store(SECONDARY_POOL_SIZE, "secondary",
				 &sec_random_state))
		goto err;
	if (create_entropy_store(SECONDARY_POOL_SIZE, "urandom",
				 &urandom_state))
		goto err;
	clear_entropy_store(random_state);
	clear_entropy_store(sec_random_state);
	clear_entropy_store(urandom_state);

	init_std_data(random_state);
	init_std_data(sec_random_state);
	init_std_data(urandom_state);
#ifdef CONFIG_SYSCTL
	sysctl_init_random(random_state);
#endif
	for (i = 0; i < NR_IRQS; i++)
		irq_timer_state[i] = NULL;
	memset(&input_timer_state, 0, sizeof(struct timer_rand_state));
	memset(&extract_timer_state, 0, sizeof(struct timer_rand_state));
	extract_timer_state.dont_count_entropy = 1;
	return 0;
err:
	return -1;
}
module_init(rand_initialize);

void rand_initialize_irq(int irq)
{
	struct timer_rand_state *state;

	if (irq >= NR_IRQS || irq_timer_state[irq])
		return;

	/*
	 * If kmalloc returns null, we just won't use that entropy
	 * source.
	 */
	state = kmalloc(sizeof(struct timer_rand_state), GFP_KERNEL);
	if (state) {
		memset(state, 0, sizeof(struct timer_rand_state));
		irq_timer_state[irq] = state;
	}
}

void rand_initialize_disk(struct gendisk *disk)
{
	struct timer_rand_state *state;

	/*
	 * If kmalloc returns null, we just won't use that entropy
	 * source.
	 */
	state = kmalloc(sizeof(struct timer_rand_state), GFP_KERNEL);
	if (state) {
		memset(state, 0, sizeof(struct timer_rand_state));
		disk->random = state;
	}
}

static ssize_t
random_read(struct file * file, char __user * buf, size_t nbytes, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	ssize_t n, retval = 0, count = 0;

	if (nbytes == 0)
		return 0;

	while (nbytes > 0) {
		n = nbytes;
		if (n > SEC_XFER_SIZE)
			n = SEC_XFER_SIZE;

		DEBUG_ENT("reading %d bits\n", n*8);

		n = extract_entropy(sec_random_state, buf, n,
				    EXTRACT_ENTROPY_USER |
				    EXTRACT_ENTROPY_LIMIT |
				    EXTRACT_ENTROPY_SECONDARY);

		DEBUG_ENT("read got %d bits (%d still needed)\n",
			  n*8, (nbytes-n)*8);

		if (n == 0) {
			if (file->f_flags & O_NONBLOCK) {
				retval = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				retval = -ERESTARTSYS;
				break;
			}

			set_current_state(TASK_INTERRUPTIBLE);
			add_wait_queue(&random_read_wait, &wait);

			if (sec_random_state->entropy_count / 8 == 0)
				schedule();

			set_current_state(TASK_RUNNING);
			remove_wait_queue(&random_read_wait, &wait);

			continue;
		}

		if (n < 0) {
			retval = n;
			break;
		}
		count += n;
		buf += n;
		nbytes -= n;
		break;		/* This break makes the device work */
				/* like a named pipe */
	}

	/*
	 * If we gave the user some bytes, update the access time.
	 */
	if (count)
		file_accessed(file);

	return (count ? count : retval);
}

static ssize_t
urandom_read(struct file * file, char __user * buf,
		      size_t nbytes, loff_t *ppos)
{
	int flags = EXTRACT_ENTROPY_USER;
	unsigned long cpuflags;

	spin_lock_irqsave(&random_state->lock, cpuflags);
	if (random_state->entropy_count > random_state->poolinfo.POOLBITS)
		flags |= EXTRACT_ENTROPY_SECONDARY;
	spin_unlock_irqrestore(&random_state->lock, cpuflags);

	return extract_entropy(urandom_state, buf, nbytes, flags);
}

static unsigned int
random_poll(struct file *file, poll_table * wait)
{
	unsigned int mask;

	poll_wait(file, &random_read_wait, wait);
	poll_wait(file, &random_write_wait, wait);
	mask = 0;
	if (random_state->entropy_count >= random_read_wakeup_thresh)
		mask |= POLLIN | POLLRDNORM;
	if (random_state->entropy_count < random_write_wakeup_thresh)
		mask |= POLLOUT | POLLWRNORM;
	return mask;
}

static ssize_t
random_write(struct file * file, const char __user * buffer,
	     size_t count, loff_t *ppos)
{
	int ret = 0;
	size_t bytes;
	__u32 buf[16];
	const char __user *p = buffer;
	size_t c = count;

	while (c > 0) {
		bytes = min(c, sizeof(buf));

		bytes -= copy_from_user(&buf, p, bytes);
		if (!bytes) {
			ret = -EFAULT;
			break;
		}
		c -= bytes;
		p += bytes;

		add_entropy_words(random_state, buf, (bytes + 3) / 4);
	}
	if (p == buffer) {
		return (ssize_t)ret;
	} else {
		struct inode *inode = file->f_dentry->d_inode;
	        inode->i_mtime = current_fs_time(inode->i_sb);
		mark_inode_dirty(inode);
		return (ssize_t)(p - buffer);
	}
}

static int
random_ioctl(struct inode * inode, struct file * file,
	     unsigned int cmd, unsigned long arg)
{
	int size, ent_count;
	int __user *p = (int __user *)arg;
	int retval;

	switch (cmd) {
	case RNDGETENTCNT:
		ent_count = random_state->entropy_count;
		if (put_user(ent_count, p))
			return -EFAULT;
		return 0;
	case RNDADDTOENTCNT:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (get_user(ent_count, p))
			return -EFAULT;
		credit_entropy_store(random_state, ent_count);
		/*
		 * Wake up waiting processes if we have enough
		 * entropy.
		 */
		if (random_state->entropy_count >= random_read_wakeup_thresh)
			wake_up_interruptible(&random_read_wait);
		return 0;
	case RNDADDENTROPY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (get_user(ent_count, p++))
			return -EFAULT;
		if (ent_count < 0)
			return -EINVAL;
		if (get_user(size, p++))
			return -EFAULT;
		retval = random_write(file, (const char __user *) p,
				      size, &file->f_pos);
		if (retval < 0)
			return retval;
		credit_entropy_store(random_state, ent_count);
		/*
		 * Wake up waiting processes if we have enough
		 * entropy.
		 */
		if (random_state->entropy_count >= random_read_wakeup_thresh)
			wake_up_interruptible(&random_read_wait);
		return 0;
	case RNDZAPENTCNT:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		random_state->entropy_count = 0;
		return 0;
	case RNDCLEARPOOL:
		/* Clear the entropy pool and associated counters. */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		clear_entropy_store(random_state);
		init_std_data(random_state);
		return 0;
	default:
		return -EINVAL;
	}
}

struct file_operations random_fops = {
	.read  = random_read,
	.write = random_write,
	.poll  = random_poll,
	.ioctl = random_ioctl,
};

struct file_operations urandom_fops = {
	.read  = urandom_read,
	.write = random_write,
	.ioctl = random_ioctl,
};

/***************************************************************
 * Random UUID interface
 *
 * Used here for a Boot ID, but can be useful for other kernel
 * drivers.
 ***************************************************************/

/*
 * Generate random UUID
 */
void generate_random_uuid(unsigned char uuid_out[16])
{
	get_random_bytes(uuid_out, 16);
	/* Set UUID version to 4 --- truely random generation */
	uuid_out[6] = (uuid_out[6] & 0x0F) | 0x40;
	/* Set the UUID variant to DCE */
	uuid_out[8] = (uuid_out[8] & 0x3F) | 0x80;
}

EXPORT_SYMBOL(generate_random_uuid);

/********************************************************************
 *
 * Sysctl interface
 *
 ********************************************************************/

#ifdef CONFIG_SYSCTL

#include <linux/sysctl.h>

static int min_read_thresh, max_read_thresh;
static int min_write_thresh, max_write_thresh;
static char sysctl_bootid[16];

/*
 * These functions is used to return both the bootid UUID, and random
 * UUID.  The difference is in whether table->data is NULL; if it is,
 * then a new UUID is generated and returned to the user.
 *
 * If the user accesses this via the proc interface, it will be returned
 * as an ASCII string in the standard UUID format.  If accesses via the
 * sysctl system call, it is returned as 16 bytes of binary data.
 */
static int proc_do_uuid(ctl_table *table, int write, struct file *filp,
			void __user *buffer, size_t *lenp, loff_t *ppos)
{
	ctl_table fake_table;
	unsigned char buf[64], tmp_uuid[16], *uuid;

	uuid = table->data;
	if (!uuid) {
		uuid = tmp_uuid;
		uuid[8] = 0;
	}
	if (uuid[8] == 0)
		generate_random_uuid(uuid);

	sprintf(buf, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
		"%02x%02x%02x%02x%02x%02x",
		uuid[0],  uuid[1],  uuid[2],  uuid[3],
		uuid[4],  uuid[5],  uuid[6],  uuid[7],
		uuid[8],  uuid[9],  uuid[10], uuid[11],
		uuid[12], uuid[13], uuid[14], uuid[15]);
	fake_table.data = buf;
	fake_table.maxlen = sizeof(buf);

	return proc_dostring(&fake_table, write, filp, buffer, lenp, ppos);
}

static int uuid_strategy(ctl_table *table, int __user *name, int nlen,
			 void __user *oldval, size_t __user *oldlenp,
			 void __user *newval, size_t newlen, void **context)
{
	unsigned char tmp_uuid[16], *uuid;
	unsigned int len;

	if (!oldval || !oldlenp)
		return 1;

	uuid = table->data;
	if (!uuid) {
		uuid = tmp_uuid;
		uuid[8] = 0;
	}
	if (uuid[8] == 0)
		generate_random_uuid(uuid);

	if (get_user(len, oldlenp))
		return -EFAULT;
	if (len) {
		if (len > 16)
			len = 16;
		if (copy_to_user(oldval, uuid, len) ||
		    put_user(len, oldlenp))
			return -EFAULT;
	}
	return 1;
}

static int sysctl_poolsize = DEFAULT_POOL_SIZE;
ctl_table random_table[] = {
	{
		.ctl_name 	= RANDOM_POOLSIZE,
		.procname	= "poolsize",
		.data		= &sysctl_poolsize,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= RANDOM_ENTROPY_COUNT,
		.procname	= "entropy_avail",
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= RANDOM_READ_THRESH,
		.procname	= "read_wakeup_threshold",
		.data		= &random_read_wakeup_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_read_thresh,
		.extra2		= &max_read_thresh,
	},
	{
		.ctl_name	= RANDOM_WRITE_THRESH,
		.procname	= "write_wakeup_threshold",
		.data		= &random_write_wakeup_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_write_thresh,
		.extra2		= &max_write_thresh,
	},
	{
		.ctl_name	= RANDOM_BOOT_ID,
		.procname	= "boot_id",
		.data		= &sysctl_bootid,
		.maxlen		= 16,
		.mode		= 0444,
		.proc_handler	= &proc_do_uuid,
		.strategy	= &uuid_strategy,
	},
	{
		.ctl_name	= RANDOM_UUID,
		.procname	= "uuid",
		.maxlen		= 16,
		.mode		= 0444,
		.proc_handler	= &proc_do_uuid,
		.strategy	= &uuid_strategy,
	},
	{ .ctl_name = 0 }
};

static void sysctl_init_random(struct entropy_store *random_state)
{
	min_read_thresh = 8;
	min_write_thresh = 0;
	max_read_thresh = max_write_thresh = random_state->poolinfo.POOLBITS;
	random_table[1].data = &random_state->entropy_count;
}
#endif 	/* CONFIG_SYSCTL */

/********************************************************************
 *
 * Random funtions for networking
 *
 ********************************************************************/

#ifdef CONFIG_INET
/*
 * TCP initial sequence number picking.  This uses the random number
 * generator to pick an initial secret value.  This value is hashed
 * along with the TCP endpoint information to provide a unique
 * starting point for each pair of TCP endpoints.  This defeats
 * attacks which rely on guessing the initial TCP sequence number.
 * This algorithm was suggested by Steve Bellovin.
 *
 * Using a very strong hash was taking an appreciable amount of the total
 * TCP connection establishment time, so this is a weaker hash,
 * compensated for by changing the secret periodically.
 */

/* F, G and H are basic MD4 functions: selection, majority, parity */
#define F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define G(x, y, z) (((x) & (y)) + (((x) ^ (y)) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))

/*
 * The generic round function.  The application is so specific that
 * we don't bother protecting all the arguments with parens, as is generally
 * good macro practice, in favor of extra legibility.
 * Rotation is separate from addition to prevent recomputation
 */
#define ROUND(f, a, b, c, d, x, s)	\
	(a += f(b, c, d) + x, a = (a << s) | (a >> (32 - s)))
#define K1 0
#define K2 013240474631UL
#define K3 015666365641UL

/*
 * Basic cut-down MD4 transform.  Returns only 32 bits of result.
 */
static __u32 halfMD4Transform (__u32 const buf[4], __u32 const in[8])
{
	__u32 a = buf[0], b = buf[1], c = buf[2], d = buf[3];

	/* Round 1 */
	ROUND(F, a, b, c, d, in[0] + K1,  3);
	ROUND(F, d, a, b, c, in[1] + K1,  7);
	ROUND(F, c, d, a, b, in[2] + K1, 11);
	ROUND(F, b, c, d, a, in[3] + K1, 19);
	ROUND(F, a, b, c, d, in[4] + K1,  3);
	ROUND(F, d, a, b, c, in[5] + K1,  7);
	ROUND(F, c, d, a, b, in[6] + K1, 11);
	ROUND(F, b, c, d, a, in[7] + K1, 19);

	/* Round 2 */
	ROUND(G, a, b, c, d, in[1] + K2,  3);
	ROUND(G, d, a, b, c, in[3] + K2,  5);
	ROUND(G, c, d, a, b, in[5] + K2,  9);
	ROUND(G, b, c, d, a, in[7] + K2, 13);
	ROUND(G, a, b, c, d, in[0] + K2,  3);
	ROUND(G, d, a, b, c, in[2] + K2,  5);
	ROUND(G, c, d, a, b, in[4] + K2,  9);
	ROUND(G, b, c, d, a, in[6] + K2, 13);

	/* Round 3 */
	ROUND(H, a, b, c, d, in[3] + K3,  3);
	ROUND(H, d, a, b, c, in[7] + K3,  9);
	ROUND(H, c, d, a, b, in[2] + K3, 11);
	ROUND(H, b, c, d, a, in[6] + K3, 15);
	ROUND(H, a, b, c, d, in[1] + K3,  3);
	ROUND(H, d, a, b, c, in[5] + K3,  9);
	ROUND(H, c, d, a, b, in[0] + K3, 11);
	ROUND(H, b, c, d, a, in[4] + K3, 15);

	return buf[1] + b;	/* "most hashed" word */
	/* Alternative: return sum of all words? */
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

static __u32 twothirdsMD4Transform (__u32 const buf[4], __u32 const in[12])
{
	__u32 a = buf[0], b = buf[1], c = buf[2], d = buf[3];

	/* Round 1 */
	ROUND(F, a, b, c, d, in[ 0] + K1,  3);
	ROUND(F, d, a, b, c, in[ 1] + K1,  7);
	ROUND(F, c, d, a, b, in[ 2] + K1, 11);
	ROUND(F, b, c, d, a, in[ 3] + K1, 19);
	ROUND(F, a, b, c, d, in[ 4] + K1,  3);
	ROUND(F, d, a, b, c, in[ 5] + K1,  7);
	ROUND(F, c, d, a, b, in[ 6] + K1, 11);
	ROUND(F, b, c, d, a, in[ 7] + K1, 19);
	ROUND(F, a, b, c, d, in[ 8] + K1,  3);
	ROUND(F, d, a, b, c, in[ 9] + K1,  7);
	ROUND(F, c, d, a, b, in[10] + K1, 11);
	ROUND(F, b, c, d, a, in[11] + K1, 19);

	/* Round 2 */
	ROUND(G, a, b, c, d, in[ 1] + K2,  3);
	ROUND(G, d, a, b, c, in[ 3] + K2,  5);
	ROUND(G, c, d, a, b, in[ 5] + K2,  9);
	ROUND(G, b, c, d, a, in[ 7] + K2, 13);
	ROUND(G, a, b, c, d, in[ 9] + K2,  3);
	ROUND(G, d, a, b, c, in[11] + K2,  5);
	ROUND(G, c, d, a, b, in[ 0] + K2,  9);
	ROUND(G, b, c, d, a, in[ 2] + K2, 13);
	ROUND(G, a, b, c, d, in[ 4] + K2,  3);
	ROUND(G, d, a, b, c, in[ 6] + K2,  5);
	ROUND(G, c, d, a, b, in[ 8] + K2,  9);
	ROUND(G, b, c, d, a, in[10] + K2, 13);

	/* Round 3 */
	ROUND(H, a, b, c, d, in[ 3] + K3,  3);
	ROUND(H, d, a, b, c, in[ 7] + K3,  9);
	ROUND(H, c, d, a, b, in[11] + K3, 11);
	ROUND(H, b, c, d, a, in[ 2] + K3, 15);
	ROUND(H, a, b, c, d, in[ 6] + K3,  3);
	ROUND(H, d, a, b, c, in[10] + K3,  9);
	ROUND(H, c, d, a, b, in[ 1] + K3, 11);
	ROUND(H, b, c, d, a, in[ 5] + K3, 15);
	ROUND(H, a, b, c, d, in[ 9] + K3,  3);
	ROUND(H, d, a, b, c, in[ 0] + K3,  9);
	ROUND(H, c, d, a, b, in[ 4] + K3, 11);
	ROUND(H, b, c, d, a, in[ 8] + K3, 15);

	return buf[1] + b; /* "most hashed" word */
	/* Alternative: return sum of all words? */
}
#endif

#undef ROUND
#undef F
#undef G
#undef H
#undef K1
#undef K2
#undef K3

/* This should not be decreased so low that ISNs wrap too fast. */
#define REKEY_INTERVAL (300 * HZ)
/*
 * Bit layout of the tcp sequence numbers (before adding current time):
 * bit 24-31: increased after every key exchange
 * bit 0-23: hash(source,dest)
 *
 * The implementation is similar to the algorithm described
 * in the Appendix of RFC 1185, except that
 * - it uses a 1 MHz clock instead of a 250 kHz clock
 * - it performs a rekey every 5 minutes, which is equivalent
 * 	to a (source,dest) tulple dependent forward jump of the
 * 	clock by 0..2^(HASH_BITS+1)
 *
 * Thus the average ISN wraparound time is 68 minutes instead of
 * 4.55 hours.
 *
 * SMP cleanup and lock avoidance with poor man's RCU.
 * 			Manfred Spraul <manfred@colorfullife.com>
 *
 */
#define COUNT_BITS 8
#define COUNT_MASK ((1 << COUNT_BITS) - 1)
#define HASH_BITS 24
#define HASH_MASK ((1 << HASH_BITS) - 1)

static struct keydata {
	__u32 count; /* already shifted to the final position */
	__u32 secret[12];
} ____cacheline_aligned ip_keydata[2];

static unsigned int ip_cnt;

static void rekey_seq_generator(void *private_);

static DECLARE_WORK(rekey_work, rekey_seq_generator, NULL);

/*
 * Lock avoidance:
 * The ISN generation runs lockless - it's just a hash over random data.
 * State changes happen every 5 minutes when the random key is replaced.
 * Synchronization is performed by having two copies of the hash function
 * state and rekey_seq_generator always updates the inactive copy.
 * The copy is then activated by updating ip_cnt.
 * The implementation breaks down if someone blocks the thread
 * that processes SYN requests for more than 5 minutes. Should never
 * happen, and even if that happens only a not perfectly compliant
 * ISN is generated, nothing fatal.
 */
static void rekey_seq_generator(void *private_)
{
	struct keydata *keyptr = &ip_keydata[1 ^ (ip_cnt & 1)];

	get_random_bytes(keyptr->secret, sizeof(keyptr->secret));
	keyptr->count = (ip_cnt & COUNT_MASK) << HASH_BITS;
	smp_wmb();
	ip_cnt++;
	schedule_delayed_work(&rekey_work, REKEY_INTERVAL);
}

static inline struct keydata *get_keyptr(void)
{
	struct keydata *keyptr = &ip_keydata[ip_cnt & 1];

	smp_rmb();

	return keyptr;
}

static __init int seqgen_init(void)
{
	rekey_seq_generator(NULL);
	return 0;
}
late_initcall(seqgen_init);

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
__u32 secure_tcpv6_sequence_number(__u32 *saddr, __u32 *daddr,
				   __u16 sport, __u16 dport)
{
	struct timeval tv;
	__u32 seq;
	__u32 hash[12];
	struct keydata *keyptr = get_keyptr();

	/* The procedure is the same as for IPv4, but addresses are longer.
	 * Thus we must use twothirdsMD4Transform.
	 */

	memcpy(hash, saddr, 16);
	hash[4]=(sport << 16) + dport;
	memcpy(&hash[5],keyptr->secret,sizeof(__u32) * 7);

	seq = twothirdsMD4Transform(daddr, hash) & HASH_MASK;
	seq += keyptr->count;

	do_gettimeofday(&tv);
	seq += tv.tv_usec + tv.tv_sec * 1000000;

	return seq;
}
EXPORT_SYMBOL(secure_tcpv6_sequence_number);
#endif

__u32 secure_tcp_sequence_number(__u32 saddr, __u32 daddr,
				 __u16 sport, __u16 dport)
{
	struct timeval tv;
	__u32 seq;
	__u32 hash[4];
	struct keydata *keyptr = get_keyptr();

	/*
	 *  Pick a unique starting offset for each TCP connection endpoints
	 *  (saddr, daddr, sport, dport).
	 *  Note that the words are placed into the starting vector, which is
	 *  then mixed with a partial MD4 over random data.
	 */
	hash[0]=saddr;
	hash[1]=daddr;
	hash[2]=(sport << 16) + dport;
	hash[3]=keyptr->secret[11];

	seq = halfMD4Transform(hash, keyptr->secret) & HASH_MASK;
	seq += keyptr->count;
	/*
	 *	As close as possible to RFC 793, which
	 *	suggests using a 250 kHz clock.
	 *	Further reading shows this assumes 2 Mb/s networks.
	 *	For 10 Mb/s Ethernet, a 1 MHz clock is appropriate.
	 *	That's funny, Linux has one built in!  Use it!
	 *	(Networks are faster now - should this be increased?)
	 */
	do_gettimeofday(&tv);
	seq += tv.tv_usec + tv.tv_sec * 1000000;
#if 0
	printk("init_seq(%lx, %lx, %d, %d) = %d\n",
	       saddr, daddr, sport, dport, seq);
#endif
	return seq;
}

EXPORT_SYMBOL(secure_tcp_sequence_number);

/*  The code below is shamelessly stolen from secure_tcp_sequence_number().
 *  All blames to Andrey V. Savochkin <saw@msu.ru>.
 */
__u32 secure_ip_id(__u32 daddr)
{
	struct keydata *keyptr;
	__u32 hash[4];

	keyptr = get_keyptr();

	/*
	 *  Pick a unique starting offset for each IP destination.
	 *  The dest ip address is placed in the starting vector,
	 *  which is then hashed with random data.
	 */
	hash[0] = daddr;
	hash[1] = keyptr->secret[9];
	hash[2] = keyptr->secret[10];
	hash[3] = keyptr->secret[11];

	return halfMD4Transform(hash, keyptr->secret);
}

/* Generate secure starting point for ephemeral TCP port search */
u32 secure_tcp_port_ephemeral(__u32 saddr, __u32 daddr, __u16 dport)
{
	struct keydata *keyptr = get_keyptr();
	u32 hash[4];

	/*
	 *  Pick a unique starting offset for each ephemeral port search
	 *  (saddr, daddr, dport) and 48bits of random data.
	 */
	hash[0] = saddr;
	hash[1] = daddr;
	hash[2] = dport ^ keyptr->secret[10];
	hash[3] = keyptr->secret[11];

	return halfMD4Transform(hash, keyptr->secret);
}

#ifdef CONFIG_SYN_COOKIES
/*
 * Secure SYN cookie computation. This is the algorithm worked out by
 * Dan Bernstein and Eric Schenk.
 *
 * For linux I implement the 1 minute counter by looking at the jiffies clock.
 * The count is passed in as a parameter, so this code doesn't much care.
 */

#define COOKIEBITS 24	/* Upper bits store count */
#define COOKIEMASK (((__u32)1 << COOKIEBITS) - 1)

static int syncookie_init;
static __u32 syncookie_secret[2][16-3+HASH_BUFFER_SIZE];

__u32 secure_tcp_syn_cookie(__u32 saddr, __u32 daddr, __u16 sport,
		__u16 dport, __u32 sseq, __u32 count, __u32 data)
{
	__u32 tmp[16 + HASH_BUFFER_SIZE + HASH_EXTRA_SIZE];
	__u32 seq;

	/*
	 * Pick two random secrets the first time we need a cookie.
	 */
	if (syncookie_init == 0) {
		get_random_bytes(syncookie_secret, sizeof(syncookie_secret));
		syncookie_init = 1;
	}

	/*
	 * Compute the secure sequence number.
	 * The output should be:
   	 *   HASH(sec1,saddr,sport,daddr,dport,sec1) + sseq + (count * 2^24)
	 *      + (HASH(sec2,saddr,sport,daddr,dport,count,sec2) % 2^24).
	 * Where sseq is their sequence number and count increases every
	 * minute by 1.
	 * As an extra hack, we add a small "data" value that encodes the
	 * MSS into the second hash value.
	 */

	memcpy(tmp + 3, syncookie_secret[0], sizeof(syncookie_secret[0]));
	tmp[0]=saddr;
	tmp[1]=daddr;
	tmp[2]=(sport << 16) + dport;
	HASH_TRANSFORM(tmp+16, tmp);
	seq = tmp[17] + sseq + (count << COOKIEBITS);

	memcpy(tmp + 3, syncookie_secret[1], sizeof(syncookie_secret[1]));
	tmp[0]=saddr;
	tmp[1]=daddr;
	tmp[2]=(sport << 16) + dport;
	tmp[3] = count;	/* minute counter */
	HASH_TRANSFORM(tmp + 16, tmp);

	/* Add in the second hash and the data */
	return seq + ((tmp[17] + data) & COOKIEMASK);
}

/*
 * This retrieves the small "data" value from the syncookie.
 * If the syncookie is bad, the data returned will be out of
 * range.  This must be checked by the caller.
 *
 * The count value used to generate the cookie must be within
 * "maxdiff" if the current (passed-in) "count".  The return value
 * is (__u32)-1 if this test fails.
 */
__u32 check_tcp_syn_cookie(__u32 cookie, __u32 saddr, __u32 daddr, __u16 sport,
		__u16 dport, __u32 sseq, __u32 count, __u32 maxdiff)
{
	__u32 tmp[16 + HASH_BUFFER_SIZE + HASH_EXTRA_SIZE];
	__u32 diff;

	if (syncookie_init == 0)
		return (__u32)-1; /* Well, duh! */

	/* Strip away the layers from the cookie */
	memcpy(tmp + 3, syncookie_secret[0], sizeof(syncookie_secret[0]));
	tmp[0]=saddr;
	tmp[1]=daddr;
	tmp[2]=(sport << 16) + dport;
	HASH_TRANSFORM(tmp + 16, tmp);
	cookie -= tmp[17] + sseq;
	/* Cookie is now reduced to (count * 2^24) ^ (hash % 2^24) */

	diff = (count - (cookie >> COOKIEBITS)) & ((__u32)-1 >> COOKIEBITS);
	if (diff >= maxdiff)
		return (__u32)-1;

	memcpy(tmp+3, syncookie_secret[1], sizeof(syncookie_secret[1]));
	tmp[0] = saddr;
	tmp[1] = daddr;
	tmp[2] = (sport << 16) + dport;
	tmp[3] = count - diff;	/* minute counter */
	HASH_TRANSFORM(tmp + 16, tmp);

	return (cookie - tmp[17]) & COOKIEMASK;	/* Leaving the data behind */
}
#endif
#endif /* CONFIG_INET */
