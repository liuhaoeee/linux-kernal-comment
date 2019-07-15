/*
 * linux/ipc/msg.c
 * Copyright (C) 1992 Krishna Balasubramanian 
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

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/msg.h>
#include <linux/stat.h>
#include <linux/malloc.h>

#include <asm/segment.h>
/*
	消息队列就是一个消息的链表。可以把消息看作一个记录，具有特定的格式以及特定的优先级
	对消息队列有写权限的进程可以向其中按照一定的规则添加新消息；对消息队列有读权限的进程
	则可以从消息队列中读走消息。消息队列是随内核持续的。
	消息队列就是一个消息的链表。每个消息队列都有一个队列头，用结构struct msg_queue来描述
	队列头中包含了该消息队列的大量信息，包括消息队列键值、用户ID、组ID、消息队列中消息数
	目等等，甚至记录了最近对消息队列读写进程的ID。进程可以访问这些信息，也可以设置其中的
	某些信息
*/

extern int ipcperms (struct ipc_perm *ipcp, short msgflg);

static void freeque (int id);
static int newque (key_t key, int msgflg);
static int findkey (key_t key);

//系统消息队列描述符指针数组
static struct msqid_ds *msgque[MSGMNI];
//系统中所有消息队列占用的内存总字节数
static int msgbytes = 0;
//系统中消息头数
static int msghdrs = 0;
static unsigned short msg_seq = 0;
//系统中有效的消息队列数
static int used_queues = 0;
//记录消息队列描述符指针数组中有效项的最大下标
static int max_msqid = 0;
static struct wait_queue *msg_lock = NULL;

//初始化系统的消息队列
void msg_init (void)
{
	int id;
	
	//遍历系统中的消息队列描述符指针数组，将每个消息队列描述符指针都置为未使用的状态
	for (id = 0; id < MSGMNI; id++) 
		msgque[id] = (struct msqid_ds *) IPC_UNUSED;
	msgbytes = msghdrs = msg_seq = max_msqid = used_queues = 0;
	msg_lock = NULL;
	return;
}

//向msqid代表的消息队列发送一个消息，即将发送的消息存储在msgp指向的msgbuf结构中，消息的大小由msgze指定
/*
	msgflg：这个参数依然是是控制函数行为的标志，取值可以是：0,表示忽略；IPC_NOWAIT，如果消息队列为空
	则返回一个ENOMSG，并将控制权交回调用函数的进程。如果不指定这个参数，那么进程将被阻塞直到函数可以
	从队列中得到符合条件的消息为止。如果一个client正在等待消息的时候队列被删除，EIDRM 就会被返回
	如果进程在阻塞等待过程中收到了系统的中断信号，EINTR 就会被返回。MSG_NOERROR，如果函数取得的消息
	长度大于msgsz，将只返回msgsz长度的信息，剩下的部分被丢弃了。如果不指定这个参数，E2BIG 将被返回
	而消息则留在队列中不被取出。当消息从队列内取出后，相应的消息就从队列中删除了
*/
int sys_msgsnd (int msqid, struct msgbuf *msgp, int msgsz, int msgflg)
{
	int id, err;
	struct msqid_ds *msq;
	struct ipc_perm *ipcp;
	struct msg *msgh;
	long mtype;
	
	//检验消息大小以及消息id的合法性
	if (msgsz > MSGMAX || msgsz < 0 || msqid < 0)
		return -EINVAL;
	//msgp不能为空
	if (!msgp) 
		return -EFAULT;
	//对消息存储的内存空间做验证
	err = verify_area (VERIFY_READ, msgp->mtext, msgsz);
	if (err) 
		return err;
	//取消息数据类型
	if ((mtype = get_fs_long (&msgp->mtype)) < 1)
		return -EINVAL;
	id = (unsigned int) msqid % MSGMNI;
	//通过id取消息队列描述符对象
	msq = msgque [id];
	//如果消息描述符对象处于未使用的状态或者正在被分配或者销毁
	if (msq == IPC_UNUSED || msq == IPC_NOID)
		return -EINVAL;
	//取消息的权限信息结构
	ipcp = &msq->msg_perm; 

 slept:
	//在睡眠过程中，消息队列可能被删除 EIDRM：表示消息已删除
	//这里是，利用消息队列标识符来计算其序列号，参见下一个函数内的相关注释
	if (msq->msg_perm.seq != (unsigned int) msqid / MSGMNI) 
		return -EIDRM;
	//权限检查 S_IWUGO：用户可写？用户组可写？其他可写？
	if (ipcperms(ipcp, S_IWUGO)) 
		return -EACCES;
	
	//如果此消息的大小加上当前消息队列所占内存字节数大于消息队列所能占用的最大内存字节数
	if (msgsz + msq->msg_cbytes > msq->msg_qbytes) { 
		/* no space in queue */
		//当前队列没有足够的空间
		//如果指定了IPC_NOWAIT标志，则立即返回
		if (msgflg & IPC_NOWAIT)
			return -EAGAIN;
		//如果进程收到了非屏蔽信号
		if (current->signal & ~current->blocked)
			return -EINTR;
		//将进程睡眠在此消息队列上
		interruptible_sleep_on (&msq->wwait);
		goto slept;
	}
	
	/* allocate message header and text space*/ 
	//为消息头和消息内容申请分配内存空间
	msgh = (struct msg *) kmalloc (sizeof(*msgh) + msgsz, GFP_USER);
	if (!msgh)
		return -ENOMEM;
	//将消息头的msg_spot成员指向为消息内容所分配的内存空间
	msgh->msg_spot = (char *) (msgh + 1);
	//将消息内容从用户空间复制到内核空间
	memcpy_fromfs (msgh->msg_spot, msgp->mtext, msgsz); 
	
	//重新验证消息描述符对象，因为上面为消息头申请分配内存可能导致进程睡眠
	if (msgque[id] == IPC_UNUSED || msgque[id] == IPC_NOID
		|| msq->msg_perm.seq != (unsigned int) msqid / MSGMNI) {
		kfree(msgh);
		return -EIDRM;
	}

	//将消息头插入消息队列
	msgh->msg_next = NULL;
	if (!msq->msg_first)
		msq->msg_first = msq->msg_last = msgh;
	else {
		msq->msg_last->msg_next = msgh;
		msq->msg_last = msgh;
	}
	//设置消息内容长度
	msgh->msg_ts = msgsz;
	//设置消息类型
	msgh->msg_type = mtype;
	//更新消息队列当前所占用内存字节数
	msq->msg_cbytes += msgsz;
	//更新当前系统中所有消息队列占用的内存字节总数
	msgbytes  += msgsz;
	//增加系统中的消息头总数
	msghdrs++;
	//增加队列里保存的消息数目
	msq->msg_qnum++;
	msq->msg_lspid = current->pid;
	msq->msg_stime = CURRENT_TIME;
	if (msq->rwait)
		wake_up (&msq->rwait);
	return msgsz;
}
//该系统调用从msqid代表的消息队列中读取一个消息，并把消息存储在msgp指向的msgbuf结构中
/*
	msqid为消息队列描述字；消息返回后存储在msgp指向的地址，msgsz指定msgbuf的mtext成员的长度
	（即消息内容的长度），msgtyp为请求读取的消息类型；读消息标志msgflg可以为以下几个常值的或：
	IPC_NOWAIT 如果没有满足条件的消息，调用立即返回，此时，errno=ENOMSG
	IPC_EXCEPT 与msgtyp>0配合使用，返回队列中第一个类型不为msgtyp的消息
	IPC_NOERROR 如果队列中满足条件的消息内容大于所请求的msgsz字节，则把该消息截断，截断部分将丢失。
*/
int sys_msgrcv (int msqid, struct msgbuf *msgp, int msgsz, long msgtyp, 
		int msgflg)
{
	struct msqid_ds *msq;
	struct ipc_perm *ipcp;
	struct msg *tmsg, *leastp = NULL;
	struct msg *nmsg = NULL;
	int id, err;

	//验证消息id和消息大小的合法性
	if (msqid < 0 || msgsz < 0)
		return -EINVAL;
	//验证用户空间所要保存消息内容的指针
	//?难道用户在mtext中存一个0也不行吗?
	//I get it!因为在struct msgbuf结构体的定义中，mtext是一个数组首地址
	//即其是当前位置的地址，消息内容的地址 精辟
	if (!msgp || !msgp->mtext)
	    return -EFAULT;
	//验证用户空间所要保存消息内容的内存空间
	err = verify_area (VERIFY_WRITE, msgp->mtext, msgsz);
	if (err)
		return err;

	//因为消息队列的标示id是内核根据消息队列的序列号、消息队列描述符指针
	//在数组中的下标和MSGMNI等信息计算而来，所以，可以由消息队列的标示id
	//计算出其在消息队列描述指针数组中的下标
/*
	注意，这里是取余数。实际上，系统将0-n*MSGMNI范围内的空间全部当做
	msqid的有效范围。而n的值也就是消息队列序列号的最大值。因此，消息
	队列的标示id，其实包含了消息队列的序列号和其在消息队列描述符指针
	数组中的下标这两种基本信息。内核也由此能够做更多的判断和操作，比如
	判断进程睡眠过程中消息队列是否处于删除状态，取消息队列描述符等等。
	
	举个例子，假设当前msg_seq（序列号）等于5。此时新建一个消息队列，则
	其序列号就是5，而返回的消息队列标示id就是5*MSGMNI（消息队列描述符指
	针数组个数）+id（此消息队列对应的消息队列描述符指针项下标索引）
	此后，内核可根据消息队列标示id计算出其序列号和在消息队列描述符数组中
	的下标。当内核要删除一个消息队列的时候，现将消息队列的序列号自增，然后
	自增全局消息队列序列号（用户新建消息队列时初始化其序列号，因此，之后新
	建的消息队列就不会与要删除的消息队列产生冲突）。内核在将要删除的消息队列的
	序列号自增后，还会唤醒所有睡眠在其上的进程。这些被唤醒的进程醒来后会发现
	根据其消息队列表示id计算出来的消息队列序号与消息队列描述符中的序号不一致了
	（因为自增了），说明消息队列处于删除状态，于是返回相应的信息。读者继续向下
	阅读代码就清楚了。
	可参见《深入理解Linux内核》中第十九章的相关讲解
	IPC 标识符是一个32位整数 内核IPC序列号可用来减少访问冲突
	
	还有一点关于序列号的理解的是《Linux内核源代码分析》第六章的相关说明，感觉很不错
*/	
	id = (unsigned int) msqid % MSGMNI;
	//根据id取消息队列描述符对象
	msq = msgque [id];
	//如果此消息描述符对象处于正在被分配或销毁，或者处于未使用的状态
	if (msq == IPC_NOID || msq == IPC_UNUSED)
		return -EINVAL;
	//取该消息描述符对象的权限信息
	ipcp = &msq->msg_perm; 

	/* 
	 *  find message of correct type.
	 *  msgtyp = 0 => get first.
	 *  msgtyp > 0 => get first message of matching type.
	 *  msgtyp < 0 => get message with least type must be < abs(msgtype).  
	 */
	 /*
		msgtyp等于0 则返回队列的最早的一个消息。
		msgtyp大于0，则返回其类型为mtype的第一个消息。
		msgtyp小于0,则返回其类型小于或等于mtype参数的绝对值的最小的一个消息。
	 */
	while (!nmsg) {
		//进程睡眠过程中，消息队列可能处于删除状态
		//由于消息队列序列号和其标示id的产生方式，可以由标示id计算出其序列号
		//进程在删除消息队列时，会将消息队列的序列号自增，然后睡眠等待所有因等待
		//此消息队列而睡眠的进程。那么，本进程可能就属于被唤醒的进程，当执行到这里
		//发现序列号不一致了，说明消息队列处于删除状态
		/*注意，这里是取模运算*/
		if (msq->msg_perm.seq != (unsigned int) msqid / MSGMNI)
			return -EIDRM;
		//验证权限信息 用户可读？用户组可读？其他可读？
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		//msgtyp等于0 则返回队列的最早的一个消息
		if (msgtyp == 0) 
			//nmsg指向消息队列中的第一个消息
			nmsg = msq->msg_first;
		//msgtyp大于0，则返回其类型为mtype的第一个消息
		else if (msgtyp > 0) {
			//IPC_EXCEPT与msgtyp>0配合使用，返回队列中第一个类型不为msgtyp的消息
			if (msgflg & MSG_EXCEPT) { 
				for (tmsg = msq->msg_first; tmsg; 
				     tmsg = tmsg->msg_next)
					if (tmsg->msg_type != msgtyp)
						break;
				nmsg = tmsg;
			} else {
				for (tmsg = msq->msg_first; tmsg; 
				     tmsg = tmsg->msg_next)
					if (tmsg->msg_type == msgtyp)
						break;
				nmsg = tmsg;
			}
		//msgtyp小于0,则返回其类型小于或等于mtype参数的绝对值的最小的一个消息
		} else {
			//for循环试图找到一个最小的消息类型值
			for (leastp = tmsg = msq->msg_first; tmsg; 
			     tmsg = tmsg->msg_next) 
				if (tmsg->msg_type < leastp->msg_type) 
					leastp = tmsg;
			//如果此最小的消息类型值小于或等于mtype参数的绝对值的最小的一个消息
			if (leastp && leastp->msg_type <= - msgtyp)
				nmsg = leastp;
		}
		
		//如果找到了合适的消息
		if (nmsg) { /* done finding a message */
			//IPC_NOERROR：如果队列中满足条件的消息内容大于所请求的msgsz字节，则把该消息截断，截断部分将丢失
			if ((msgsz < nmsg->msg_ts) && !(msgflg & MSG_NOERROR))
				//E2BIG：消息文本长度大于msgsz，并且msgflg中没有指定MSG_NOERROR
				return -E2BIG;
			//如果消息内容大于所请求的msgsz参数，则将msgsz设置为消息内容大小
			msgsz = (msgsz > nmsg->msg_ts)? nmsg->msg_ts : msgsz;
			//将消息从消息队列中移除
			if (nmsg ==  msq->msg_first)
				msq->msg_first = nmsg->msg_next;
			else {
				for (tmsg = msq->msg_first; tmsg; 
				     tmsg = tmsg->msg_next)
					if (tmsg->msg_next == nmsg) 
						break;
				tmsg->msg_next = nmsg->msg_next;
				if (nmsg == msq->msg_last)
					msq->msg_last = tmsg;
			}
			//如果消息队列中没有消息了
			if (!(--msq->msg_qnum))
				//置空消息队列指针
				msq->msg_last = msq->msg_first = NULL;
			//更新最近一次队列接受消息的时间
			msq->msg_rtime = CURRENT_TIME;
			//更新最近一次向队列发送消息的进程的pid
			msq->msg_lrpid = current->pid;
			//更新系统中消息队列所占用的内存字节总数
			msgbytes -= nmsg->msg_ts; 
			//减少系统中消息头数
			msghdrs--; 
			//更新消息队列中当前占用的内存字节数
			msq->msg_cbytes -= nmsg->msg_ts;
			if (msq->wwait)
				wake_up (&msq->wwait);
			//将消息内容复制到用户空间缓冲结构中
			put_fs_long (nmsg->msg_type, &msgp->mtype);
			memcpy_tofs (msgp->mtext, nmsg->msg_spot, msgsz);
			//释放消息结构
			kfree(nmsg);
			//返回得到的消息字节数
			return msgsz;
		//否则，没有找到合适的消息
		} else {  /* did not find a message */
			//如果指定了IPC_NOWAIT，表示不要阻塞，则返回-ENOMSG;
			if (msgflg & IPC_NOWAIT)
				return -ENOMSG;
			//如果进程收到非阻塞信号
			if (current->signal & ~current->blocked)
				return -EINTR; 
			//将进程睡眠在此队列上
			interruptible_sleep_on (&msq->rwait);
		}
	} /* end while */
	return -1;
}

//根据key值找到消息描述符对象在数组中的下标
static int findkey (key_t key)
{
	int id;
	struct msqid_ds *msq;
	
	for (id = 0; id <= max_msqid; id++) {
		//如果消息描述符对象处于分配或销毁状态，则睡眠等待
		while ((msq = msgque[id]) == IPC_NOID) 
			interruptible_sleep_on (&msg_lock);
		if (msq == IPC_UNUSED)
			continue;
		if (key == msq->msg_perm.key)
			return id;
	}
	return -1;
}

//该函数用于建立一个新的消息队列资源并返回其资源标识符
static int newque (key_t key, int msgflg)
{
	int id;
	struct msqid_ds *msq;
	struct ipc_perm *ipcp;

	//遍历消息队列描述符数组
	for (id = 0; id < MSGMNI; id++) 
		//如果该项处于未使用的状态
		if (msgque[id] == IPC_UNUSED) {
			//将其置为正在被分配的状态
			msgque[id] = (struct msqid_ds *) IPC_NOID;
			goto found;
		}
	//执行到这里，说明没有找到空闲的消息队列描述符
	return -ENOSPC;

found:
	//为此消息队列描述符分配内存空间 虽然可能导致进程睡眠，但是消息描述符处于“锁定”状态(IPC_NOID)
	msq = (struct msqid_ds *) kmalloc (sizeof (*msq), GFP_KERNEL);
	//如果申请失败
	if (!msq) {
		//将此项重新设置为未使用的状态
		msgque[id] = (struct msqid_ds *) IPC_UNUSED;
		//如果有进程睡眠在消息队列上，则唤醒之
		if (msg_lock)
			wake_up (&msg_lock);
		return -ENOMEM;
	}
	//取消息队列描述符的权限信息结构指针，并作初始化
	ipcp = &msq->msg_perm;
	//根据创建者指定的消息队列flag来设定消息队列的权限信息
	ipcp->mode = (msgflg & S_IRWXUGO);
	ipcp->key = key;
	ipcp->cuid = ipcp->uid = current->euid;
	ipcp->gid = ipcp->cgid = current->egid;
	//初始化消息队列描述符对象
	//初始化消息队列的序列号
	msq->msg_perm.seq = msg_seq;
	//置空消息队列指针
	msq->msg_first = msq->msg_last = NULL;
	msq->rwait = msq->wwait = NULL;
	msq->msg_cbytes = msq->msg_qnum = 0;
	msq->msg_lspid = msq->msg_lrpid = 0;
	msq->msg_stime = msq->msg_rtime = 0;
	//限定消息队列占用内存字节数的最大值
	msq->msg_qbytes = MSGMNB;
	msq->msg_ctime = CURRENT_TIME;
	//max_msqid保存了当前消息描述符指针数组的最大有效项的下标
	if (id > max_msqid)
		max_msqid = id;
	msgque[id] = msq;
	//增加系统中的有效消息队列数
	used_queues++;
	if (msg_lock)
		wake_up (&msg_lock);
	//根据消息队列的序列号 返回消息队列的一个标示id
	return (unsigned int) msq->msg_perm.seq * MSGMNI + id;
}

//获取与某个键关联的消息队列标识。
/*
	消息队列被建立的情况有两种：
	1.如果键的值是IPC_PRIVATE。 
	2.或者键的值不是IPC_PRIVATE，并且键所对应的消息队列不存在，同时标志中指定IPC_CREAT
*/
int sys_msgget (key_t key, int msgflg)
{
	int id;
	struct msqid_ds *msq;
	
	//如果键值是IPC_PRIVATE，则要建立一个新的消息队列
	if (key == IPC_PRIVATE) 
		return newque(key, msgflg);
	//如果指定的键值没有被使用，即系统中现存的消息队列的键值没有等于key的
	if ((id = findkey (key)) == -1) { /* key not used */
		//如果没有指定IPC_CREAT标志
		if (!(msgflg & IPC_CREAT))
			return -ENOENT;
		//如果指定了IPC_CREAT标志，则也建立一个新的消息队列
		return newque(key, msgflg);
	}
	//执行到这里，说明此key有对应的消息队列
/*
	IPC_CREAT ：如果这个队列在内核中不存在，则创建它。 
	IPC_EXCL ：当与IPC_CREAT一起使用时，如果这个队列已存在，则创建失败
	如果IPC_CREAT单独使用，semget()为一个新创建的消息队列返回标识号，
	或者返回具有相同键值的已存在队列的标识号。如果IPC_EXCL与IPC_CREAT
	一起使用，要么创建一个新的队列，要么对已存在的队列返回-1。 
	IPC_EXCL单独是没有用的，当与IPC_CREAT结合起来使用时，可以保证新创建队列的打开和存取
*/	
	if (msgflg & IPC_CREAT && msgflg & IPC_EXCL)
		return -EEXIST;
	//msq指向具有此key值的消息队列
	msq = msgque[id];
	if (msq == IPC_UNUSED || msq == IPC_NOID)
		return -EIDRM;
	if (ipcperms(&msq->msg_perm, msgflg))
		return -EACCES;
	return (unsigned int) msq->msg_perm.seq * MSGMNI + id;
} 

//释放指定的消息队列
static void freeque (int id)
{
	struct msqid_ds *msq = msgque[id];
	struct msg *msgp, *msgh;

	//增加此消息队列的序列号
	//这就表明了消息队列处于删除的、无效的状态
	//因为下面可能发生进程调度 所以需要这么一个标志
	//当进程读取或写入该消息队列的消息时，会根据消息队列的序列号计算其id值
	//若id不一致，则说明消息队列处于删除状态
	msq->msg_perm.seq++;
	msg_seq = (msg_seq+1) % ((unsigned)(1<<31)/MSGMNI); /* increment, but avoid overflow */
	//减少系统中消息队列所占内存字节数
	msgbytes -= msq->msg_cbytes;
	//更新max_msqid值
	if (id == max_msqid)
		while (max_msqid && (msgque[--max_msqid] == IPC_UNUSED));
	//将此消息队列描述符指针对象置为未使用状态
	msgque[id] = (struct msqid_ds *) IPC_UNUSED;
	//减少系统中消息队列数
	used_queues--;
	//唤醒所有等待此消息队列中的消息的进程
	while (msq->rwait || msq->wwait) {
		if (msq->rwait)
			wake_up (&msq->rwait); 
		if (msq->wwait)
			wake_up (&msq->wwait);
		schedule(); 
	}
	//销毁消息队列中的所有消息
	for (msgp = msq->msg_first; msgp; msgp = msgh ) {
		msgh = msgp->msg_next;
		//减少消息头结构数
		msghdrs--;
		//释放消息结构
		kfree(msgp);
	}
	//释放消息队列描述符对象
	kfree(msq);
}

//对消息队列的控制处理函数
/*
	msgid   消息队列的ID,也就是msgget（）函数的返回值
	cmd   命令，对消息队列的处理
	IPC_RMID  从系统内核中删除消息队列，相当于我们在终端输入ipcrm   -q  消息队列id
	IPC_STAT  获取消息队列的详细消息。包含权限，各种时间，id等
			  取出系统保存的消息队列的msqid_ds 数据，并将其存入参数buf 指向的msqid_ds 结构中
	IPC_SET   设置消息队列的信息。 设定消息队列的msqid_ds 数据中的msg_perm 成员。设定的值由buf 指向的msqid_ds结构给出
	buf: 存放消息队列状态的结构体的地址
*/
int sys_msgctl (int msqid, int cmd, struct msqid_ds *buf)
{
	int id, err;
	struct msqid_ds *msq;
	struct msqid_ds tbuf;
	struct ipc_perm *ipcp;
	
	if (msqid < 0 || cmd < 0)
		return -EINVAL;
	switch (cmd) {
/*
	在IPC_INFO和MSG_INFO情况中，调用者需要有关消息队列实现的属性信息。
	它可能要用这些信息来选择消息容量，比如说，在最大消息容量较大的机器上，
	调用进程可以提高它自己在每个消息中发送的信息量界限。所有清晰地在消息队
	列实现中定义那些缺省界限的常数都是通过struct msginfo对象复制回来的 
	假如cmd是MSG_INFO而不是IPC_INFO时，还要包括一些额外信息
	注意一下调用程序的缓存buf，它被定义成了指向一种不同类型struct msqid_ds的指针
	不过没有关系。复制是由copy_to_user函数完成的，它并不关心它的参数的类型
	尽管当被要求向一块不可访问的内存写入时该函数也会产生错误。如果调用者提供了一个指
	向一块足够大空间的指针，sys_msgctl函数将把请求的数据复制到那里；使得类型（或至少
	是容量）正确是取决于调用程序的。如果复制成功，sys_msgctl函数返回一个附加的信息段
	即max_msqid。注意这种情况完全忽略了msqid参数。这样做有重要的意义，因为它返回了有
	关消息队列执行情况的总体信息，而不是某个特别的消息队列的具体信息。不过，就这种情况
	下是否应该拒绝负的msqid值仍是一个各人看法不同的问题。不可否认的是，即使没有使用
	msqid时也拒绝一个无效的msqid值一定能够简化代码
*/
	case IPC_INFO: 
	case MSG_INFO: 
		if (!buf)
			return -EFAULT;
	{ 
		struct msginfo msginfo;
		msginfo.msgmni = MSGMNI;
		msginfo.msgmax = MSGMAX;
		msginfo.msgmnb = MSGMNB;
		msginfo.msgmap = MSGMAP;
		msginfo.msgpool = MSGPOOL;
		msginfo.msgtql = MSGTQL;
		msginfo.msgssz = MSGSSZ;
		msginfo.msgseg = MSGSEG;
		if (cmd == MSG_INFO) {
			msginfo.msgpool = used_queues;
			msginfo.msgmap = msghdrs;
			msginfo.msgtql = msgbytes;
		}
		err = verify_area (VERIFY_WRITE, buf, sizeof (struct msginfo));
		if (err)
			return err;
		memcpy_tofs (buf, &msginfo, sizeof(struct msginfo));
		return max_msqid;
	}
/*
	MSG_STAT请求内核对给定消息队列持续作出的统计性信息――它的当前和最大容量、它的最近的读者和写者的PID，等等。
	如果msqid参数不合法，在给定位指处没有队列存在，或者调用者缺少访问该队列的许可，则返回一个错误。
	因此，队列上的读许可不仅意味着是对入队消息的读许可，而且也是对关于队列本身“元数据（metadata）”的读许可。
	顺便提及一下，要注意命令MSG_STAT假定msqid只是msgque下标，并不包括序列编号。
	调用者通过了测试。sys_msgctl函数把请求的信息复制到一个临时变量中，然后再把临时变量复制回调用者的缓存。
	返回“完全的”标识符――序列编号现在已经被编码在其中了
	还剩下三种情况：IPC_SET、IPC_STAT，和IPC_RMID。与读者迄今为止所见的那些情况有所不同的是，那些情况都在
	switch语句里被完全的处理了，而剩余的这三种在此仅进行部分处理。第一种情况，IPC_SET只要确保用户提供的缓
	冲区非空，就将它复制到tbuf里以便后面函数的进一步处理。
	剩余三种情况中的第二种，IPC_STAT仅仅执行一次健全性检查――它的真正工作还在后边的函数体中。最后一种情形
	IPC_RMID在这个语句中不工作；它所有的工作都推迟到后边的函数中完成。
	
*/
	//MSG_STAT请求内核对给定消息队列持续作出的统计性信息――它的当前和最大容量、它的最近的读者和写者的PID，等等。
	case MSG_STAT:
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_WRITE, buf, sizeof (*buf));
		if (err)
			return err;
		if (msqid > max_msqid)
			return -EINVAL;
		//取消息队列描述符
		msq = msgque[msqid];
		if (msq == IPC_UNUSED || msq == IPC_NOID)
			return -EINVAL;
		if (ipcperms (&msq->msg_perm, S_IRUGO))
			return -EACCES;
		//id是消息队列标识id
		id = (unsigned int) msq->msg_perm.seq * MSGMNI + msqid;
		//复制此消息描述符对象内容
		tbuf.msg_perm   = msq->msg_perm;
		tbuf.msg_stime  = msq->msg_stime;
		tbuf.msg_rtime  = msq->msg_rtime;
		tbuf.msg_ctime  = msq->msg_ctime;
		tbuf.msg_cbytes = msq->msg_cbytes;
		tbuf.msg_qnum   = msq->msg_qnum;
		tbuf.msg_qbytes = msq->msg_qbytes;
		tbuf.msg_lspid  = msq->msg_lspid;
		tbuf.msg_lrpid  = msq->msg_lrpid;
		//复制到用户空间
		memcpy_tofs (buf, &tbuf, sizeof(*buf));
		return id;
	case IPC_SET:
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_READ, buf, sizeof (*buf));
		if (err)
			return err;
		memcpy_fromfs (&tbuf, buf, sizeof (*buf));
		break;
	case IPC_STAT:
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_WRITE, buf, sizeof(*buf));
		if (err)
			return err;
		break;
	}//end of switch (cmd)
/*
	这段代码对所有剩余的情况都是共同的，而且大家现在都应该对它比较熟悉了：它从msqid里提取出数组下标，确保
	在指定的下标处存在着一个有效的消息队列，并验证序列编号的合法性。
	处理IPC_STAT命令的剩余部分。假如用户有从队列中读出的许可，sys_msgctl函数就把统计信息复制进调用者的缓冲区里
	如果你认为这与先前MSG_STAT的情形非常类似，那你就是对的。这两者之间的唯一不同之处在于：正如读者所见
	MSG_STAT期望一个“不完全”的msqid，而IPC_STAT却期望一个“完全”的msqid（就是说包括序列编号）。
	复制统计数据到用户空间。如果按照如下方式重写这三行代码，那么运行速度或许稍快一些：
	毕竟，对于写入用户空间来说成功要肯定比失败更为普遍。基于同样的原因，MSG_STAT情况下的相应的代码如果被重写成
	以下形式也可能更快：或者，下边的一个甚至可能更快，因为没有一次多余的赋值操作：
	然而和直觉相反的是，我对所有这三种修改都作了测试，结果却发现是内核的版本执行起来更快。这必然与gcc生成目标
	代码的方式有关：显然，我的版本中的一条额外跳转要比内核版本的额外赋值所花费的代价高得多。（从C源代码来考虑额
	外的跳转并不直观――你不得不考察gcc的汇编输出代码。）回想前边章节所讨论过的，跳转会带来明显的性能损失，这是因为
	它们会使得CPU丧失其内在的并行性所带来的好处。CPU的设计者们竭尽全力要避免分支造成的性能损失影响，不过很明显，
	他们并不总是成功的。最终，对gcc优化器的进一步改善可能消除内核版本和我的代码之间的差别。每当两种形式逻辑相
	同而一个较快时，假如gcc能够发现这种等价并为两者生成同样的代码，那将非常令人愉快。不过这个问题是要比看上去
	难得多的。为了生成最快的代码，gcc将需要能够猜测哪一次赋值最易发生――另一种情况则涉及了分支。（对gcc的最近版
	本所作的工作已为这样的改进打下了基础。）
*/
	id = (unsigned int) msqid % MSGMNI;
	msq = msgque [id];
	if (msq == IPC_UNUSED || msq == IPC_NOID)
		return -EINVAL;
	if (msq->msg_perm.seq != (unsigned int) msqid / MSGMNI)
		return -EIDRM;
	ipcp = &msq->msg_perm;

	switch (cmd) {
	case IPC_STAT:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		tbuf.msg_perm   = msq->msg_perm;
		tbuf.msg_stime  = msq->msg_stime;
		tbuf.msg_rtime  = msq->msg_rtime;
		tbuf.msg_ctime  = msq->msg_ctime;
		tbuf.msg_cbytes = msq->msg_cbytes;
		tbuf.msg_qnum   = msq->msg_qnum;
		tbuf.msg_qbytes = msq->msg_qbytes;
		tbuf.msg_lspid  = msq->msg_lspid;
		tbuf.msg_lrpid  = msq->msg_lrpid;
		memcpy_tofs (buf, &tbuf, sizeof (*buf));
		return 0;
/*
	在IPC_SET情形里，调用者需要设置消息队列的某些参数：它的最大容量、属主，和模式（mode）。
	为了操纵消息队列的参数，调用者必须拥有该队列或者拥有CAP_SYS_ADMIN 权能
	把消息队列中最大字节数的界限提高到正常限制以上，这就类似于提高任何其它资源的硬界限一样
	因此它也需要与之相同的权能，即CAP_SYS_RESOURCE
	调用者应该被允许执行该操作，所以被选择的参数根据调用者提供的tbuf被设置
*/
	case IPC_SET:
		if (!suser() && current->euid != ipcp->cuid && 
		    current->euid != ipcp->uid)
			return -EPERM;
		if (tbuf.msg_qbytes > MSGMNB && !suser())
			return -EPERM;
		msq->msg_qbytes = tbuf.msg_qbytes;
		ipcp->uid = tbuf.msg_perm.uid;
		ipcp->gid =  tbuf.msg_perm.gid;
		ipcp->mode = (ipcp->mode & ~S_IRWXUGO) | 
			(S_IRWXUGO & tbuf.msg_perm.mode);
		msq->msg_ctime = CURRENT_TIME;
		return 0;
/*
	IPC_RMID意味着删除特定的队列――不是队列中的消息，而是队列本身。假如调用者拥有该队列
	或者有CAP_SYS_ADMIN权能，这个队列就可以用freeque函数调用来释放
*/
	case IPC_RMID:
		if (!suser() && current->euid != ipcp->cuid && 
		    current->euid != ipcp->uid)
			return -EPERM;
		freeque (id); 
		return 0;
	default:
		return -EINVAL;
	}
}
