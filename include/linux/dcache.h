#ifndef __LINUX_DCACHE_H
#define __LINUX_DCACHE_H

#ifdef __KERNEL__

#include <asm/atomic.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/rcupdate.h>
#include <asm/bug.h>

struct nameidata;
struct vfsmount;

/*
 * linux/include/linux/dcache.h
 *
 * Dirent cache data structures
 *
 * (C) Copyright 1997 Thomas Schoebel-Theuer,
 * with heavy changes by Linus Torvalds
 */

#define IS_ROOT(x) ((x) == (x)->d_parent)

/*
 * “快速字符串”——简化参数传递，但更重要的是保存关于字符串的“元数据”（即长度和哈希）。
 *
 * hash 首先出现，因此它依偎在 dentry 中的 d_parent 上。
 */
struct qstr {
	unsigned int hash;
	unsigned int len;
	const unsigned char *name;
};

struct dentry_stat_t {
	int nr_dentry;
	int nr_unused;
	int age_limit;          /* age in seconds */
	int want_pages;         /* pages requested by system */
	int dummy[2];
};
extern struct dentry_stat_t dentry_stat;

/* Name hashing routines. Initial hash value */
/* Hash courtesy of the R5 hash in reiserfs modulo sign bits */
#define init_name_hash()		0

/* 部分哈希更新函数。假设每个字符大约 4 位 */
static inline unsigned long
partial_name_hash(unsigned long c, unsigned long prevhash)
{
	return (prevhash + (c << 4) + (c >> 4)) * 11;
}

/*
 * 最后：将位数减少到 int 值（并尽量避免丢失位数）
 */
static inline unsigned long end_name_hash(unsigned long hash)
{
	return (unsigned int) hash;
}

/* Compute the hash for a name string. */
static inline unsigned int
full_name_hash(const unsigned char *name, unsigned int len)
{
	unsigned long hash = init_name_hash();
	while (len--)
		hash = partial_name_hash(*name++, hash);
	return end_name_hash(hash);
}

struct dcookie_struct;

#define DNAME_INLINE_LEN_MIN 36

/* 和超级块和索引节点不同，目录项并不是实际存在于磁盘上的。
 *
 * 在使用的时候在内存中创建目录项对象，其实通过索引节点已经可以定位到指定的文件，
 * 但是索引节点对象的属性非常多，在查找，比较文件时，直接用索引节点效率不高，所以引入了目录项的概念。
 *
 * 路径中的每个部分都是一个目录项，比如路径： /mnt/cdrom/foo/bar 其中包含5个目录项，/ mnt cdrom foo bar
 *
 * 每个目录项对象都有3种状态：被使用，未使用和负状态
 * 		被使用：对应一个有效的索引节点，并且该对象由一个或多个使用者
 * 		未使用：对应一个有效的索引节点，但是VFS当前并没有使用这个目录项
 * 		负状态：没有对应的有效索引节点（可能索引节点被删除或者路径不存在了）
 *
 * 	目录项的目的就是提高文件查找，比较的效率，所以访问过的目录项都会缓存在slab中。
 * */
struct dentry {
	atomic_t d_count;		/* 使用计数 */
	unsigned int d_flags;		/* protected by d_lock 目录项标识 */
	spinlock_t d_lock;		/* per dentry lock 单目录项锁 */
	struct inode *d_inode;		/* Where the name belongs to - NULL is
					 * negative 相关联的索引节点 */
	/*
	 * The next three fields are touched by __d_lookup.  Place them here
	 * so they all fit in a 16-byte range, with 16-byte alignment.
	 */
	struct dentry *d_parent;	/* parent directory 父目录的目录项对象*/
	struct qstr d_name;		// 目录项名称

	struct list_head d_lru;		/* LRU list 未使用的链表 */
	struct list_head d_child;	/* child of parent list */
	struct list_head d_subdirs;	/* our children 子目录链表 */
	struct list_head d_alias;	/* inode alias list 索引节点别名链表 */
	unsigned long d_time;		/* used by d_revalidate */
	struct dentry_operations *d_op; // 目录项操作相关函数
	struct super_block *d_sb;	/* The root of the dentry tree 文件的超级块 */
	void *d_fsdata;			/* fs-specific data 文件系统特有数据 */
 	struct rcu_head d_rcu;
	struct dcookie_struct *d_cookie; /* cookie, if any */
	struct hlist_node d_hash;	/* lookup hash list 散列表 */
	int d_mounted;
	unsigned char d_iname[DNAME_INLINE_LEN_MIN];	/* small names  短文件名 */
};

struct dentry_operations {
	/* 该函数判断目录项对象是否有效。VFS准备从dcache中使用一个目录项时会调用这个函数 */
	int (*d_revalidate)(struct dentry *, struct nameidata *);
	/* 为目录项对象生成hash值 */
	int (*d_hash) (struct dentry *, struct qstr *);
	/* 比较 qstr 类型的2个文件名 */
	int (*d_compare) (struct dentry *, struct qstr *, struct qstr *);
	/* 当目录项对象的 d_count 为0时，VFS调用这个函数 */
	int (*d_delete)(struct dentry *);
	/* 当目录项对象将要被释放时，VFS调用该函数 */
	void (*d_release)(struct dentry *);
	/* 当目录项对象丢失其索引节点时（也就是磁盘索引节点被删除了），VFS会调用该函数 */
	void (*d_iput)(struct dentry *, struct inode *);
};

/* the dentry parameter passed to d_hash and d_compare is the parent
 * directory of the entries to be compared. It is used in case these
 * functions need any directory specific information for determining
 * equivalency classes.  Using the dentry itself might not work, as it
 * might be a negative dentry which has no information associated with
 * it */

/*
locking rules:
		big lock	dcache_lock	d_lock   may block
d_revalidate:	no		no		no       yes
d_hash		no		no		no       yes
d_compare:	no		yes		yes      no
d_delete:	no		yes		no       no
d_release:	no		no		no       yes
d_iput:		no		no		no       yes
 */

/* d_flags entries */
#define DCACHE_AUTOFS_PENDING 0x0001    /* autofs: "under construction" */
#define DCACHE_NFSFS_RENAMED  0x0002    /* this dentry has been "silly
					 * renamed" and has to be
					 * deleted on the last dput()
					 */
#define	DCACHE_DISCONNECTED 0x0004
     /* This dentry is possibly not currently connected to the dcache tree,
      * in which case its parent will either be itself, or will have this
      * flag as well.  nfsd will not use a dentry with this bit set, but will
      * first endeavour to clear the bit either by discovering that it is
      * connected, or by performing lookup operations.   Any filesystem which
      * supports nfsd_operations MUST have a lookup function which, if it finds
      * a directory inode with a DCACHE_DISCONNECTED dentry, will d_move
      * that dentry into place and return that dentry rather than the passed one,
      * typically using d_splice_alias.
      */

#define DCACHE_REFERENCED	0x0008  /* Recently used, don't discard. */
#define DCACHE_UNHASHED		0x0010	

extern spinlock_t dcache_lock;

/**
 * d_drop - drop a dentry
 * @dentry: dentry to drop
 *
 * d_drop() unhashes the entry from the parent
 * dentry hashes, so that it won't be found through
 * a VFS lookup any more. Note that this is different
 * from deleting the dentry - d_delete will try to
 * mark the dentry negative if possible, giving a
 * successful _negative_ lookup, while d_drop will
 * just make the cache lookup fail.
 *
 * d_drop() is used mainly for stuff that wants
 * to invalidate a dentry for some reason (NFS
 * timeouts or autofs deletes).
 */

static inline void __d_drop(struct dentry *dentry)
{
	if (!(dentry->d_flags & DCACHE_UNHASHED)) {
		dentry->d_flags |= DCACHE_UNHASHED;
		hlist_del_rcu(&dentry->d_hash);
	}
}

static inline void d_drop(struct dentry *dentry)
{
	spin_lock(&dcache_lock);
 	__d_drop(dentry);
	spin_unlock(&dcache_lock);
}

static inline int dname_external(struct dentry *dentry)
{
	return dentry->d_name.name != dentry->d_iname;
}

/*
 * These are the low-level FS interfaces to the dcache..
 */
extern void d_instantiate(struct dentry *, struct inode *);
extern struct dentry * d_instantiate_unique(struct dentry *, struct inode *);
extern void d_delete(struct dentry *);

/* allocate/de-allocate */
extern struct dentry * d_alloc(struct dentry *, const struct qstr *);
extern struct dentry * d_alloc_anon(struct inode *);
extern struct dentry * d_splice_alias(struct inode *, struct dentry *);
extern void shrink_dcache_sb(struct super_block *);
extern void shrink_dcache_parent(struct dentry *);
extern void shrink_dcache_anon(struct hlist_head *);
extern int d_invalidate(struct dentry *);

/* only used at mount-time */
extern struct dentry * d_alloc_root(struct inode *);

/* <clickety>-<click> the ramfs-type tree */
extern void d_genocide(struct dentry *);

extern struct dentry *d_find_alias(struct inode *);
extern void d_prune_aliases(struct inode *);

/* test whether we have any submounts in a subdir tree */
extern int have_submounts(struct dentry *);

/*
 * This adds the entry to the hash queues.
 */
extern void d_rehash(struct dentry *);

/**
 * d_add - add dentry to hash queues
 * @entry: dentry to add
 * @inode: The inode to attach to this dentry
 *
 * This adds the entry to the hash queues and initializes @inode.
 * The entry was actually filled in earlier during d_alloc().
 */
 
static inline void d_add(struct dentry *entry, struct inode *inode)
{
	d_instantiate(entry, inode);
	d_rehash(entry);
}

/**
 * d_add_unique - add dentry to hash queues without aliasing
 * @entry: dentry to add
 * @inode: The inode to attach to this dentry
 *
 * This adds the entry to the hash queues and initializes @inode.
 * The entry was actually filled in earlier during d_alloc().
 */
static inline struct dentry *d_add_unique(struct dentry *entry, struct inode *inode)
{
	struct dentry *res;

	res = d_instantiate_unique(entry, inode);
	d_rehash(res != NULL ? res : entry);
	return res;
}

/* used for rename() and baskets */
extern void d_move(struct dentry *, struct dentry *);

/* appendix may either be NULL or be used for transname suffixes */
extern struct dentry * d_lookup(struct dentry *, struct qstr *);
extern struct dentry * __d_lookup(struct dentry *, struct qstr *);

/* validate "insecure" dentry pointer */
extern int d_validate(struct dentry *, struct dentry *);

extern char * d_path(struct dentry *, struct vfsmount *, char *, int);
  
/* Allocation counts.. */

/**
 *	dget, dget_locked	-	get a reference to a dentry
 *	@dentry: dentry to get a reference to
 *
 *	Given a dentry or %NULL pointer increment the reference count
 *	if appropriate and return the dentry. A dentry will not be 
 *	destroyed when it has references. dget() should never be
 *	called for dentries with zero reference counter. For these cases
 *	(preferably none, functions in dcache.c are sufficient for normal
 *	needs and they take necessary precautions) you should hold dcache_lock
 *	and call dget_locked() instead of dget().
 */
 
static inline struct dentry *dget(struct dentry *dentry)
{
	if (dentry) {
		BUG_ON(!atomic_read(&dentry->d_count));
		atomic_inc(&dentry->d_count);
	}
	return dentry;
}

extern struct dentry * dget_locked(struct dentry *);

/**
 *	d_unhashed -	is dentry hashed
 *	@dentry: entry to check
 *
 *	Returns true if the dentry passed is not currently hashed.
 */
 
static inline int d_unhashed(struct dentry *dentry)
{
	return (dentry->d_flags & DCACHE_UNHASHED);
}

static inline struct dentry *dget_parent(struct dentry *dentry)
{
	struct dentry *ret;

	spin_lock(&dentry->d_lock);
	ret = dget(dentry->d_parent);
	spin_unlock(&dentry->d_lock);
	return ret;
}

extern void dput(struct dentry *);

static inline int d_mountpoint(struct dentry *dentry)
{
	return dentry->d_mounted;
}

extern struct vfsmount *lookup_mnt(struct vfsmount *, struct dentry *);
extern struct dentry *lookup_create(struct nameidata *nd, int is_dir);

extern int sysctl_vfs_cache_pressure;

#endif /* __KERNEL__ */

#endif	/* __LINUX_DCACHE_H */
