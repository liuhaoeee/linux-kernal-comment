#ifndef _LINUX_WAIT_H
#define _LINUX_WAIT_H

#define WNOHANG		0x00000001
#define WUNTRACED	0x00000002

#define __WCLONE	0x80000000

#ifdef __KERNEL__

//等待队列节点
struct wait_queue {
	struct task_struct * task;	//睡眠的进程
	struct wait_queue * next;	//指向下一个等待节点
};

struct semaphore {
	int count;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { 1, NULL })
#define MUTEX_LOCKED ((struct semaphore) { 0, NULL })

struct select_table_entry {
	struct wait_queue wait;	//睡眠队列节点
	struct wait_queue ** wait_address;	//指向睡眠队列
};

typedef struct select_table_struct {
	int nr;	//当前select_table页面中有效的select_table_entry数
	struct select_table_entry * entry;	//指向select_table_entry链表头
} select_table;

#define __MAX_SELECT_TABLE_ENTRIES (4096 / sizeof (struct select_table_entry))

#endif /* __KERNEL__ */

#endif
