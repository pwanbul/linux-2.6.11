#ifndef _LINUX_NAMEI_H
#define _LINUX_NAMEI_H

#include <linux/linkage.h>

// 文件查找相关

struct vfsmount;

struct open_intent {
	int	flags;
	int	create_mode;
};

enum { MAX_NESTED_LINKS = 5 };
/*
 * 查找过程涉及到很多函数调用，在这些调用过程中，
 * nameidata起到了很重要的作用：
 * 1. 向查找函数传递参数；
 * 2. 保存查找结果。
 *
 * 查找完成后@dentry 包含了找到文件的dentry目录项。
 * @mnt 包含了文件目录项所在的vfsmount。
 * @last 包含了需要查找的名称，这是一个快速字符串，除了路径字符串本身外，还包含字符串的长度和一个散列值。
 * @depth 当前路径深度。
 * @saved_names：由于在符号链接处理时，nd的名字一直发生变化，这里用来保存符号链接处理中的路径名。
 * */
struct nameidata {
	struct dentry	*dentry;		// 目录项
	struct vfsmount *mnt;		// 挂载点
	struct qstr	last;		// 快速字符串
	unsigned int	flags;
	int		last_type;
	unsigned	depth;		// 符号连接相关
	char *saved_names[MAX_NESTED_LINKS + 1];

	/* 意图数据 */
	union {
		struct open_intent open;
	} intent;
};

/*
 * LOOKUP_PARENT 上最后一个组件的类型
 */
enum {LAST_NORM, LAST_ROOT, LAST_DOT, LAST_DOTDOT, LAST_BIND};

/*
 * 查找事件的位掩码：
 *  - 按照最后的链接
 *  - 需要一个目录
 *  - 即使对于不存在的文件，结束斜线也可以
 *  - 内部“有更多路径组件”标志
 *  - 使用 dcache_lock 完成查找时锁定
 */
#define LOOKUP_FOLLOW		 1
#define LOOKUP_DIRECTORY	 2
#define LOOKUP_CONTINUE		 4
#define LOOKUP_PARENT		16
#define LOOKUP_NOALT		32
/*
 * Intent data
 */
#define LOOKUP_OPEN		(0x0100)
#define LOOKUP_CREATE		(0x0200)
#define LOOKUP_ACCESS		(0x0400)

extern int FASTCALL(__user_walk(const char __user *, unsigned, struct nameidata *));
#define user_path_walk(name,nd) \
	__user_walk(name, LOOKUP_FOLLOW, nd)
#define user_path_walk_link(name,nd) \
	__user_walk(name, 0, nd)
extern int FASTCALL(path_lookup(const char *, unsigned, struct nameidata *));
extern int FASTCALL(path_walk(const char *, struct nameidata *));
extern int FASTCALL(link_path_walk(const char *, struct nameidata *));
extern void path_release(struct nameidata *);
extern void path_release_on_umount(struct nameidata *);

extern struct dentry * lookup_one_len(const char *, struct dentry *, int);
extern struct dentry * lookup_hash(struct qstr *, struct dentry *);

extern int follow_down(struct vfsmount **, struct dentry **);
extern int follow_up(struct vfsmount **, struct dentry **);

extern struct dentry *lock_rename(struct dentry *, struct dentry *);
extern void unlock_rename(struct dentry *, struct dentry *);

static inline void nd_set_link(struct nameidata *nd, char *path)
{
	nd->saved_names[nd->depth] = path;
}

static inline char *nd_get_link(struct nameidata *nd)
{
	return nd->saved_names[nd->depth];
}

#endif /* _LINUX_NAMEI_H */
