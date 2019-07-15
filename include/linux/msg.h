#ifndef _LINUX_MSG_H
#define _LINUX_MSG_H
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

/* msgrcv options */
#define MSG_NOERROR     010000  /* no error if message is too big */
#define MSG_EXCEPT      020000  /* recv any msg except of specified type.*/

/* one msqid structure for each queue on the system */
//msgqid_ds 结构被系统内核用来保存消息队列对象有关数据。内核中存在的每个消息
//队列对象系统都保存一个msgqid_ds 结构的数据存放该对象的各种信息
struct msqid_ds {
    struct ipc_perm msg_perm;	//成员保存了消息队列的存取权限以及其他一些信息
    struct msg *msg_first;  /* first message on queue *///成员指针保存了消息队列（链表）中第一个成员的地址
    struct msg *msg_last;   /* last message in queue *///成员指针保存了消息队列中最后一个成员的地址
    time_t msg_stime;       /* last msgsnd time *///成员保存了最近一次队列接受消息的时间
    time_t msg_rtime;       /* last msgrcv time *///成员保存了最近一次从队列中取出消息的时间
    time_t msg_ctime;       /* last change time *///成员保存了最近一次队列发生改动的时间
    struct wait_queue *wwait;	//wwait 和rwait 是指向系统内部等待队列的指针
    struct wait_queue *rwait;
    ushort msg_cbytes;      /* current number of bytes on queue *///成员保存着队列总共占用内存的字节数
    ushort msg_qnum;        /* number of messages in queue *///成员保存着队列里保存的消息数目
    ushort msg_qbytes;      /* max number of bytes on queue *///成员保存着队列所占用内存的最大字节数
    ushort msg_lspid;       /* pid of last msgsnd *///成员保存着最近一次向队列发送消息的进程的pid
    ushort msg_lrpid;       /* last receive pid */// 成员保存着最近一次从队列中取出消息的进程的pid
};

/* message buffer for msgsnd and msgrcv calls */
//消息队列最大的灵活性在于，我们可以自己定义传递给队列的消息的数据类型的。不
//过这个类型并不是随便定义的，msgbuf 结构给了我们一个这类数据类型的基本结构定义
struct msgbuf {
    long mtype;         /* type of message *///一个正的长整型量，通过它来区分不同的消息数据类型
    char mtext[1];      /* message text *///是消息数据的内容
};

/* buffer for msgctl calls IPC_INFO, MSG_INFO */
struct msginfo {
    int msgpool;
    int msgmap; 
    int msgmax; 
    int msgmnb; 
    int msgmni; 
    int msgssz; 
    int msgtql; 
    ushort  msgseg; 
};

#define MSGMNI   128   /* <= 1K */     /* max # of msg queue identifiers */
#define MSGMAX  4056   /* <= 4056 */   /* max size of message (bytes) */
#define MSGMNB 16384   /* ? */        /* default max size of a message queue */

/* unused */
#define MSGPOOL (MSGMNI*MSGMNB/1024)  /* size in kilobytes of message pool */
#define MSGTQL  MSGMNB            /* number of system message headers */
#define MSGMAP  MSGMNB            /* number of entries in message map */
#define MSGSSZ  16                /* message segment size */
#define __MSGSEG ((MSGPOOL*1024)/ MSGSSZ) /* max no. of segments */
#define MSGSEG (__MSGSEG <= 0xffff ? __MSGSEG : 0xffff)

#ifdef __KERNEL__

/* one msg structure for each message */
//消息队列在系统内核中是以消息链表的形式出现的。而完成消息链表每个节点结构定
//义的就是msg结构
struct msg {
    struct msg *msg_next;   /* next message on queue */
							//成员是指向消息链表中下一个节点的指针，依靠它对整个消息链表进行访问
    long  msg_type;         //msg_type 和msgbuf 中mtype 成员的意义是一样的
    char *msg_spot;         /* message text address *///成员指针指出了消息内容（就是msgbuf 结构中的mtext）在内存中的位置
    short msg_ts;           /* message text size *///指出了消息内容的长度
};

/* ipcs ctl commands */
#define MSG_STAT 11
#define MSG_INFO 12

#endif /* __KERNEL__ */

#endif /* _LINUX_MSG_H */
