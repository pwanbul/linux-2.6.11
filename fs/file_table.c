/*
 *  linux/fs/file_table.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/eventpoll.h>
#include <linux/mount.h>
#include <linux/cdev.h>

/* sysctl 可调参数... */
struct files_stat_struct files_stat = {
	.max_files = NR_FILE
};

EXPORT_SYMBOL(files_stat); /* Needed by unix.o */

/* public. Not pretty! */
 __cacheline_aligned_in_smp DEFINE_SPINLOCK(files_lock);

static DEFINE_SPINLOCK(filp_count_lock);

/* slab constructors and destructors are called from arbitrary
 * context and must be fully threaded - use a local spinlock
 * to protect files_stat.nr_files
 */
void filp_ctor(void * objp, struct kmem_cache_s *cachep, unsigned long cflags)
{
	if ((cflags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		unsigned long flags;
		spin_lock_irqsave(&filp_count_lock, flags);
		files_stat.nr_files++;
		spin_unlock_irqrestore(&filp_count_lock, flags);
	}
}

void filp_dtor(void * objp, struct kmem_cache_s *cachep, unsigned long dflags)
{
	unsigned long flags;
	spin_lock_irqsave(&filp_count_lock, flags);
	files_stat.nr_files--;
	spin_unlock_irqrestore(&filp_count_lock, flags);
}

static inline void file_free(struct file *f)
{
	kmem_cache_free(filp_cachep, f);
}

/* 找到一个未使用的文件结构并返回一个指向它的指针。
 * 如果没有更多可用文件结构或内存不足，则返回 NULL。
 *
 * 通用接口，获取一个空的struct file结构
 * 注意，使用epoll需要config EPOLL
 */
struct file *get_empty_filp(void)
{
	static int old_max;
	struct file * f;

	/*
	 * 特权用户可以超过 max_files
	 */
	if (files_stat.nr_files < files_stat.max_files || capable(CAP_SYS_ADMIN)) {
		f = kmem_cache_alloc(filp_cachep, GFP_KERNEL);		// 专用接口
		if (f) {
			memset(f, 0, sizeof(*f));
			if (security_file_alloc(f)) {
				file_free(f);
				goto fail;
			}
			eventpoll_init_file(f);
			atomic_set(&f->f_count, 1);
			f->f_uid = current->fsuid;
			f->f_gid = current->fsgid;
			rwlock_init(&f->f_owner.lock);
			/* f->f_version: 0 */
			INIT_LIST_HEAD(&f->f_list);
			f->f_maxcount = INT_MAX;
			return f;
		}
	}

	/* 用完了filps - 报告 */
	if (files_stat.max_files >= old_max) {
		printk(KERN_INFO "VFS: file-max limit %d reached\n", files_stat.max_files);
		old_max = files_stat.max_files;
	} else {
		/* 大问题... */
		printk(KERN_WARNING "VFS: filp allocation failed\n");
	}
fail:
	return NULL;
}

EXPORT_SYMBOL(get_empty_filp);

void fastcall fput(struct file *file)
{
	if (atomic_dec_and_test(&file->f_count))
		__fput(file);
}

EXPORT_SYMBOL(fput);

/* __fput is called from task context when aio completion releases the last
 * last use of a struct file *.  Do not use otherwise.
 */
void fastcall __fput(struct file *file)
{
	struct dentry *dentry = file->f_dentry;
	struct vfsmount *mnt = file->f_vfsmnt;
	struct inode *inode = dentry->d_inode;

	might_sleep();
	/*
	 * The function eventpoll_release() should be the first called
	 * in the file cleanup chain.
	 */
	eventpoll_release(file);
	locks_remove_flock(file);

	if (file->f_op && file->f_op->release)
		file->f_op->release(inode, file);
	security_file_free(file);
	if (unlikely(inode->i_cdev != NULL))
		cdev_put(inode->i_cdev);
	fops_put(file->f_op);
	if (file->f_mode & FMODE_WRITE)
		put_write_access(inode);
	file_kill(file);
	file->f_dentry = NULL;
	file->f_vfsmnt = NULL;
	file_free(file);
	dput(dentry);
	mntput(mnt);
}

// 打开文件描述符fd指定的文件，返回struct file*
struct file fastcall *fget(unsigned int fd)
{
	struct file *file;
	struct files_struct *files = current->files;

	spin_lock(&files->file_lock);
	file = fcheck_files(files, fd);
	if (file)
		get_file(file);
	spin_unlock(&files->file_lock);
	return file;
}

EXPORT_SYMBOL(fget);

/*
 * 轻量级文件查找
 * - 如果 fd 表未共享，则没有 refcnt 增量。
 *
 * 只有在确保当前任务已经拥有对该文件的引用时，您才能使用它。
 * 该检查必须仅在 fget() 处完成，并返回一个标志以传递给相应的 fput_light()。
 * fget_light/fput_light 对之间不能有克隆。
 */
struct file fastcall *fget_light(unsigned int fd, int *fput_needed)
{
	struct file *file;
	struct files_struct *files = current->files;

	*fput_needed = 0;
	if (likely((atomic_read(&files->count) == 1))) {
		file = fcheck_files(files, fd);
	} else {
		spin_lock(&files->file_lock);
		file = fcheck_files(files, fd);
		if (file) {
			get_file(file);
			*fput_needed = 1;
		}
		spin_unlock(&files->file_lock);
	}
	return file;
}


void put_filp(struct file *file)
{
	if (atomic_dec_and_test(&file->f_count)) {
		security_file_free(file);
		file_kill(file);
		file_free(file);
	}
}

void file_move(struct file *file, struct list_head *list)
{
	if (!list)
		return;
	file_list_lock();
	list_move(&file->f_list, list);
	file_list_unlock();
}

void file_kill(struct file *file)
{
	if (!list_empty(&file->f_list)) {
		file_list_lock();
		list_del_init(&file->f_list);
		file_list_unlock();
	}
}

int fs_may_remount_ro(struct super_block *sb)
{
	struct list_head *p;

	/* Check that no files are currently opened for writing. */
	file_list_lock();
	list_for_each(p, &sb->s_files) {
		struct file *file = list_entry(p, struct file, f_list);
		struct inode *inode = file->f_dentry->d_inode;

		/* File with pending delete? */
		if (inode->i_nlink == 0)
			goto too_bad;

		/* Writeable file? */
		if (S_ISREG(inode->i_mode) && (file->f_mode & FMODE_WRITE))
			goto too_bad;
	}
	file_list_unlock();
	return 1; /* Tis' cool bro. */
too_bad:
	file_list_unlock();
	return 0;
}

void __init files_init(unsigned long mempages)
{ 
	int n; 
	/* 一个具有关联 inode 和 dcache 的文件大约为 1K。
	 * 默认情况下，不要将超过 10% 的内存用于文件。
	 */ 

	n = (mempages * (PAGE_SIZE / 1024)) / 10;
	files_stat.max_files = n; 
	if (files_stat.max_files < NR_FILE)
		files_stat.max_files = NR_FILE;
} 
