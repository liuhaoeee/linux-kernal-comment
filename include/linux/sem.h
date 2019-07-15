#ifndef _LINUX_SEM_H
#define _LINUX_SEM_H
#include <linux/ipc.h>
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

/* semop flags */
#define SEM_UNDO        0x1000  /* undo the operation on exit */

/* semctl Command Definitions. */
#define GETPID  11       /* get sempid */
#define GETVAL  12       /* get semval */
#define GETALL  13       /* get all semval's */
#define GETNCNT 14       /* get semncnt */
#define GETZCNT 15       /* get semzcnt */
#define SETVAL  16       /* set semval */
#define SETALL  17       /* set all semval's */

/* One semid data structure for each set of semaphores in the system. */
//系统中表示信号量集合(set)的数据结构(semid_ds) 
struct semid_ds {
  struct ipc_perm sem_perm;            /* permissions .. see ipc.h *//* IPC权限 */ 
  time_t          sem_otime;           /* last semop time *//* 最后一次对信号量操作(semop)的时间 */ 
  time_t          sem_ctime;           /* last change time *//* 对这个结构最后一次修改的时间 */
  struct sem      *sem_base;           /* ptr to first semaphore in array */ /* 在信号量数组中指向第一个信号量的指针 */ 
  struct sem_queue *sem_pending;       /* pending operations to be processed *//* 待处理的挂起操作*/ 
  struct sem_queue **sem_pending_last; /* last pending operation */ /* 最后一个挂起操作 */ 
  struct sem_undo *undo;	       /* undo requests on this array *//* 在这个数组上的undo 请求 */ 
  ushort          sem_nsems;           /* no. of semaphores in array *//* 信号量数组个数 */ 
};

/* semop system calls takes an array of these. */
struct sembuf {
  ushort  sem_num;        /* semaphore index in array *//* 在数组中信号量的索引值 */ 
  short   sem_op;         /* semaphore operation *//* 信号量操作值(正数、负数或0) */ 
  short   sem_flg;        /* operation flags *//* 操作标志，为IPC_NOWAIT或SEM_UNDO*/ 
};

/* arg for semctl system calls. */
//	val当执行SETVAL命令时使用。buf在IPC_STAT/IPC_SET命令中使用。代表了内核中使用的信号量的数据结构。
//	array在使用GETALL/SETALL命令时使用的指针。
union semun {
  int val;			/* value for SETVAL */
  struct semid_ds *buf;		/* buffer for IPC_STAT & IPC_SET */
  ushort *array;		/* array for GETALL & SETALL */
  struct seminfo *__buf;	/* buffer for IPC_INFO */
  void *__pad;
};

struct  seminfo {
    int semmap;
    int semmni;
    int semmns;
    int semmnu;
    int semmsl;
    int semopm;
    int semume;
    int semusz;
    int semvmx;
    int semaem;
};

#define SEMMNI  128             /* ?  max # of semaphore identifiers */
#define SEMMSL  32              /* <= 512 max num of semaphores per id */
#define SEMMNS  (SEMMNI*SEMMSL) /* ? max # of semaphores in system */
#define SEMOPM  32	        /* ~ 100 max num of ops per semop call */
#define SEMVMX  32767           /* semaphore maximum value */

/* unused */
#define SEMUME  SEMOPM          /* max num of undo entries per process */
#define SEMMNU  SEMMNS          /* num of undo structures system wide */
#define SEMAEM  (SEMVMX >> 1)   /* adjust on exit max value */
#define SEMMAP  SEMMNS          /* # of entries in semaphore map */
#define SEMUSZ  20		/* sizeof struct sem_undo */

#ifdef __KERNEL__

/* One semaphore structure for each semaphore in the system. */
//系统中每个信号量的数据结构(sem) 
struct sem {
  short   semval;         /* current value *//* 信号量的当前值 */ 
  short   sempid;         /* pid of last operation *//*在信号量上最后一次操作的进程识别号 * /
};

/* ipcs ctl cmds */
#define SEM_STAT 18
#define SEM_INFO 19

/* One queue for each semaphore set in the system. */
//系统中每一信号量集合的队列结构(sem_queue) 
struct sem_queue {
    struct sem_queue *	next;	 /* next entry in the queue *//* 队列中下一个节点 */ 
    struct sem_queue **	prev;	 /* previous entry in the queue, *(q->prev) == q *//* 队列中前一个节点, *(q->prev) == q */ 
    struct wait_queue *	sleeper; /* sleeping process *//* 正在睡眠的进程 */ 	
    struct sem_undo *	undo;	 /* undo structure *//* undo 结构*/ 
    int    		pid;	 /* process id of requesting process *//* 请求进程的进程识别号 */ 
    int    		status;	 /* completion status of operation *//* 操作的完成状态 */ 
    struct semid_ds *	sma;	 /* semaphore array for operations *//*有操作的信号量集合数组 */ 
    struct sembuf *	sops;	 /* array of pending operations */ /* 挂起操作的数组 */ 
    int			nsops;	 /* number of operations *//* 操作的个数 */ 
};
/*
	和信号量操作相关的概念还有“死锁”。当某个进程修改了信号量而进入临界区之后
	却因为崩溃或被“杀死(kill)"而没有退出临界区，这时，其他被挂起在信号量上的
	进程永远得不到运行机会，这就是所谓的死锁。Linux 通过维护一个信号量数组的
	调整列表(semadj)来避免这一问题。其基本思想是，当应用这些“调整”时，让信号
	量的状态退回到操作实施前的状态。 
	关于调整的描述是在sem_undo数据结构中，描述如下： 
*/
/* Each task has a list of undo requests. They are executed automatically
 * when the process exits.
 */
struct sem_undo {
    struct sem_undo *  proc_next; /* next entry on this process *//*在这个进程上的下一个sem_undo节点 */ 
    struct sem_undo *  id_next;	  /* next entry on this semaphore set */ /* 在这个信号量集	上的下一个sem_undo节点*/ 
    int		       semid;	  /* semaphore set identifier *//* 信号量集的标识号*/ 
    short *	       semadj;	  /* array of adjustments, one per semaphore */ /* 信号量数组的调整，每个进程一个*/ 
};

#endif /* __KERNEL__ */

#endif /* _LINUX_SEM_H */
