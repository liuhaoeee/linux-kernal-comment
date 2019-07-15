#ifndef _LINUX_IPC_H
#define _LINUX_IPC_H
#include <linux/types.h>

typedef int key_t; 		/* should go in <types.h> type for IPC key */
#define IPC_PRIVATE ((key_t) 0)  
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

//Linux为每个IPC对象设置了一个ipc_perm结构体并在创建IPC对象的时候进行初始化。
//这个结构体中定义了IPC对象的访问权限和所有者:
struct ipc_perm
{
  key_t  key;
  ushort uid;   /* owner euid and egid */
  ushort gid;
  ushort cuid;  /* creator euid and egid */
  ushort cgid;
  ushort mode;  /* access modes see mode flags below */
  ushort seq;   /* sequence number *///这是系统保存的IPC对象的序列号 
									//详细的说明请参考msg.c中的相关注释以及《深入理解linux内核》第十九章关于资源标识符的说明
};


/* resource get request flags */
#define IPC_CREAT  00001000   /* create if key is nonexistent */
#define IPC_EXCL   00002000   /* fail if key exists */
#define IPC_NOWAIT 00004000   /* return error on wait */


/* 
 * Control commands used with semctl, msgctl and shmctl 
 * see also specific commands in sem.h, msg.h and shm.h
 */
#define IPC_RMID 0     /* remove resource */
#define IPC_SET  1     /* set ipc_perm options */
#define IPC_STAT 2     /* get ipc_perm options */
#define IPC_INFO 3     /* see ipcs */

#ifdef __KERNEL__

/* special shmsegs[id], msgque[id] or semary[id]  values */
#define IPC_UNUSED	((void *) -1)
#define IPC_NOID	((void *) -2)		/* being allocated/destroyed */

/* 
 * These are used to wrap system calls. See ipc/util.c.
 */
struct ipc_kludge {
    struct msgbuf *msgp;
    long msgtyp;
};

#define SEMOP	 	1
#define SEMGET 		2
#define SEMCTL 		3
#define MSGSND 		11
#define MSGRCV 		12
#define MSGGET 		13
#define MSGCTL 		14
#define SHMAT 		21
#define SHMDT 		22
#define SHMGET 		23
#define SHMCTL 		24

#define IPCCALL(version,op)	((version)<<16 | (op))

#endif /* __KERNEL__ */

#endif /* _LINUX_IPC_H */


