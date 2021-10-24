#ifndef _LINUX_PID_H
#define _LINUX_PID_H

enum pid_type		// 标识符类型，不同类型的id共用一个id池
{
	PIDTYPE_PID,	// 进程ID，pid
	PIDTYPE_TGID,	// 线程组ID，线程组中leader线程的进程ID，tgid，
	PIDTYPE_PGID,	// 进程组ID，进程组中leader进程的进程ID，pgid
	PIDTYPE_SID,	// 会话ID，回话中leader进程的进程ID，sid
	PIDTYPE_MAX		// 表示类型的数量，为4
};

struct pid
{
	/* Try to keep pid_chain in the same cacheline as nr for find_pid */
	int nr;			// id号
	struct hlist_node pid_chain;		// 桶链表
	/* list of pids with the same nr, only one of them is in the hash */
	// 具有相同nr的pid列表，其中只有一个在哈希中
	struct list_head pid_list;			// nr相同时使用
};

#define pid_task(elem, type) \
	list_entry(elem, struct task_struct, pids[type].pid_list)

/*
 * attach_pid() and detach_pid() must be called with the tasklist_lock
 * write-held.
 */
extern int FASTCALL(attach_pid(struct task_struct *task, enum pid_type type, int nr));

extern void FASTCALL(detach_pid(struct task_struct *task, enum pid_type));

/*
 * look up a PID in the hash table. Must be called with the tasklist_lock
 * held.
 */
extern struct pid *FASTCALL(find_pid(enum pid_type, int));

extern int alloc_pidmap(void);
extern void FASTCALL(free_pidmap(int));
extern void switch_exec_pids(struct task_struct *leader, struct task_struct *thread);

#define do_each_task_pid(who, type, task)				\
	if ((task = find_task_by_pid_type(type, who))) {		\
		prefetch((task)->pids[type].pid_list.next);		\
		do {

#define while_each_task_pid(who, type, task)				\
		} while (task = pid_task((task)->pids[type].pid_list.next,\
						type),			\
			prefetch((task)->pids[type].pid_list.next),	\
			hlist_unhashed(&(task)->pids[type].pid_chain));	\
	}								\

#endif /* _LINUX_PID_H */
