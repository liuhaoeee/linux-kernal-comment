#ifndef _LINUX_RESOURCE_H
#define _LINUX_RESOURCE_H

/*
 * Resource control/accounting header file for linux
 */
//Linux操作系统内核核心就是这样 曾经风云变幻 到最后却被千万行的代码淹没的无影无踪  
//幸好 他有时又会留下一点蛛丝马迹 引领我们穿越操作系统核心所有的秘密
/*
	The PDF documents about understanding Linux Kernel 1.2 are made by Liu Yihao
	这份学习Linux内核的PDF文档由刘以浩同学整理完成
	包含了作者在学习内核过程中搜集到的大量有关Linux
	内核的知识和自己的一些理解以及注释
	当然，最宝贵的还是这份原生的内核代码
	NOTE：如果在阅读过程中遇到什么问题，欢迎和我交流讨论
	Email：liuyihaolovem@163.com
*/
/*
 * Definition of struct rusage taken from BSD 4.3 Reno
 * 
 * We don't support all of these yet, but we might as well have them....
 * Otherwise, each time we add new items, programs which depend on this
 * structure will lose.  This reduces the chances of that happening.
 */
#define	RUSAGE_SELF	0
#define	RUSAGE_CHILDREN	(-1)
#define RUSAGE_BOTH	(-2)		/* sys_wait4() uses this */

struct	rusage {
	struct timeval ru_utime;	/* user time used */
	struct timeval ru_stime;	/* system time used */
	long	ru_maxrss;		/* maximum resident set size */
	long	ru_ixrss;		/* integral shared memory size */
	long	ru_idrss;		/* integral unshared data size */
	long	ru_isrss;		/* integral unshared stack size */
	long	ru_minflt;		/* page reclaims */
	long	ru_majflt;		/* page faults */
	long	ru_nswap;		/* swaps */
	long	ru_inblock;		/* block input operations */
	long	ru_oublock;		/* block output operations */
	long	ru_msgsnd;		/* messages sent */
	long	ru_msgrcv;		/* messages received */
	long	ru_nsignals;		/* signals received */
	long	ru_nvcsw;		/* voluntary context switches */
	long	ru_nivcsw;		/* involuntary " */
};

/*
 * Resource limits
 */
/*
	最大允许的CPU使用时间，秒为单位。当进程达到软限制，内核将给其发送SIGXCPU信号
	这一信号的默认行为是终止进程的执行。然而，可以捕捉信号，处理句柄可将控制返回
	给主程序。如果进程继续耗费CPU时间，核心会以每秒一次的频率给其发送SIGXCPU信号
	直到达到硬限制，那时将给进程发送 SIGKILL信号终止其执行。
*/
#define RLIMIT_CPU	0		/* CPU time in ms */
#define RLIMIT_FSIZE	1		/* Maximum filesize *///进程可建立的文件的最大长度。如果进程试图超出这一限制时
													  //核心会给其发送SIGXFSZ信号，默认情况下将终止进程的执行
#define RLIMIT_DATA	2		/* max data size *///进程数据段的最大值
#define RLIMIT_STACK	3		/* max stack size *///最大的进程堆栈，以字节为单位
#define RLIMIT_CORE	4		/* max core file size *///内核转存文件的最大长度
#define RLIMIT_RSS	5		/* max resident set size */
#define RLIMIT_NPROC	6		/* max number of processes *///用户可拥有的最大进程数
#define RLIMIT_NOFILE	7		/* max number of open files *///指定比进程可打开的最大文件描述词大一的值，超出此值，将会产生EMFILE错误

#ifdef notdef
#define RLIMIT_MEMLOCK	8		/* max locked-in-memory address space*///进程可为POSIX消息队列分配的最大字节数
#endif

#define RLIM_NLIMITS	8

#define RLIM_INFINITY	((long)(~0UL>>1))

/*
	soft limit是指内核所能支持的资源上限。比如对于RLIMIT_NOFILE(一个进程能打开的最大文件数
	内核默认是1024)，soft limit最大也只能达到1024。对于RLIMIT_CORE(core文件的大小，内核不
	做限制)，soft limit最大能是unlimited。
	hard limit在资源中只是作为soft limit的上限。当你设置hard limit后，你以后设置的soft limit
	只能小于hard limit。要说明的是，hard limit只针对非特权进程，也就是进程的有效用户ID(effective 
	user ID)不是0的进程。具有特权级别的进程(具有属性CAP_SYS_RESOURCE)，soft limit则只有内核上限。
*/
//描述资源软硬限制的结构体
struct rlimit {
	long	rlim_cur;	//soft limit
	long	rlim_max;	//hard limit
};

#define	PRIO_MIN	(-99)
#define	PRIO_MAX	14

#define	PRIO_PROCESS	0
#define	PRIO_PGRP	1
#define	PRIO_USER	2

#endif
