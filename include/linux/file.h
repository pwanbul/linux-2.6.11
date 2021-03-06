/*
 * Wrapper functions for accessing the file_struct fd array.
 */

#ifndef __LINUX_FILE_H
#define __LINUX_FILE_H

#include <asm/atomic.h>
#include <linux/posix_types.h>
#include <linux/compiler.h>
#include <linux/spinlock.h>

/*
 * 默认的 fd 数组需要至少为 BITS_PER_LONG，因为这是 copy_fdset() 返回的粒度。
 */
#define NR_OPEN_DEFAULT BITS_PER_LONG

/*
 * Open file table structure
 * 打开的文件描述符表
 */
struct files_struct {
	atomic_t count;         // 共享该表的进程数
	spinlock_t file_lock;     /* Protects all the below members.  Nests inside tsk->alloc_lock */
	int max_fds;            // 当前文件对象的最大数，32
	int max_fdset;          // 当前文件描述符的最大数，1024
	int next_fd;            // 已分配的文件描述符加1，0

	struct file ** fd;      /* current fd array 指向文件对象指针数组的指针，在fork中初始化时指向fd_array */
	fd_set *close_on_exec;  // 指向执行exec( )时需要关闭的文件描述符，在fork中初始化时指向close_on_exec_init
	fd_set *open_fds;       // 指向打开文件描述符的指针，在fork中初始化时指向open_fds_init
	fd_set close_on_exec_init;  // 执行exec( )时需要关闭的文件描述符的初值集合
	fd_set open_fds_init;       // 文件描述符的初值集合
	struct file * fd_array[NR_OPEN_DEFAULT];     // 文件对象指针的初始化数组，32
};

extern void FASTCALL(__fput(struct file *));
extern void FASTCALL(fput(struct file *));

static inline void fput_light(struct file *file, int fput_needed)
{
	if (unlikely(fput_needed))
		fput(file);
}

extern struct file * FASTCALL(fget(unsigned int fd));
extern struct file * FASTCALL(fget_light(unsigned int fd, int *fput_needed));
extern void FASTCALL(set_close_on_exec(unsigned int fd, int flag));
extern void put_filp(struct file *);
extern int get_unused_fd(void);
extern void FASTCALL(put_unused_fd(unsigned int fd));
struct kmem_cache_s;
extern void filp_ctor(void * objp, struct kmem_cache_s *cachep, unsigned long cflags);
extern void filp_dtor(void * objp, struct kmem_cache_s *cachep, unsigned long dflags);

extern struct file ** alloc_fd_array(int);
extern void free_fd_array(struct file **, int);

extern fd_set *alloc_fdset(int);
extern void free_fdset(fd_set *, int);

extern int expand_files(struct files_struct *, int nr);

static inline struct file * fcheck_files(struct files_struct *files, unsigned int fd)
{
	struct file * file = NULL;

	if (fd < files->max_fds)
		file = files->fd[fd];
	return file;
}

/*
 * Check whether the specified fd has an open file.
 */
#define fcheck(fd)	fcheck_files(current->files, fd)

extern void FASTCALL(fd_install(unsigned int fd, struct file * file));

struct task_struct;

struct files_struct *get_files_struct(struct task_struct *);
void FASTCALL(put_files_struct(struct files_struct *fs));

#endif /* __LINUX_FILE_H */
