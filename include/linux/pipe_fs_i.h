#ifndef _LINUX_PIPE_FS_I_H
#define _LINUX_PIPE_FS_I_H
//Linux操作系统内核核心就是这样 曾经风云变幻 到最后却被千万行的代码淹没的无影无踪  
//幸好 他有时又会留下一点蛛丝马迹 引领我们穿越操作系统核心所有的秘密
/*
	The PDF documents about understanding Linux Kernel 1.2 is made by Liu Yihao
	这份学习Linux内核的PDF文档由刘以浩同学整理完成
	包含了作者在学习内核过程中搜集到的大量有关Linux
	内核的知识和自己的一些理解以及注释
	当然，最宝贵的还是这份原生的内核代码
	NOTE：如果在阅读过程中遇到什么问题，欢迎和我交流讨论
	Email：liuyihaolovem@163.com
*/
struct pipe_inode_info {
	struct wait_queue * wait;	//不管是读进程还是写进程，都睡眠在同一个队列中，
								//我想这是为了在每次睡眠唤醒操作中，都对每个睡眠的进程做信号检测
	char * base;
	unsigned int start;
	unsigned int len;
	unsigned int lock;
	unsigned int rd_openers;	//以读方式打开此管道而被阻塞（因为没有写进程）的进程总数 也即睡眠在此管道节点上的读进程数
	unsigned int wr_openers;	//以写方式打开此管道而被阻塞（因为没有读进程）的进程总数 也即睡眠在此管道节点上的写进程数
	unsigned int readers;	//读进程总数
	unsigned int writers;	//写进程总数
};

#define PIPE_WAIT(inode)	((inode).u.pipe_i.wait)
#define PIPE_BASE(inode)	((inode).u.pipe_i.base)
#define PIPE_START(inode)	((inode).u.pipe_i.start)
#define PIPE_LEN(inode)		((inode).u.pipe_i.len)
#define PIPE_RD_OPENERS(inode)	((inode).u.pipe_i.rd_openers)
#define PIPE_WR_OPENERS(inode)	((inode).u.pipe_i.wr_openers)
#define PIPE_READERS(inode)	((inode).u.pipe_i.readers)
#define PIPE_WRITERS(inode)	((inode).u.pipe_i.writers)
#define PIPE_LOCK(inode)	((inode).u.pipe_i.lock)
#define PIPE_SIZE(inode)	PIPE_LEN(inode)

#define PIPE_EMPTY(inode)	(PIPE_SIZE(inode)==0)
#define PIPE_FULL(inode)	(PIPE_SIZE(inode)==PIPE_BUF)
#define PIPE_FREE(inode)	(PIPE_BUF - PIPE_LEN(inode))
#define PIPE_END(inode)		((PIPE_START(inode)+PIPE_LEN(inode))&\
							   (PIPE_BUF-1))
#define PIPE_MAX_RCHUNK(inode)	(PIPE_BUF - PIPE_START(inode))
#define PIPE_MAX_WCHUNK(inode)	(PIPE_BUF - PIPE_END(inode))

#endif
