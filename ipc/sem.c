/*
 * linux/ipc/sem.c
 * Copyright (C) 1992 Krishna Balasubramanian
 * Copyright (C) 1995 Eric Schenk, Bruno Haible
 *
 * IMPLEMENTATION NOTES ON CODE REWRITE (Eric Schenk, January 1995):
 * This code underwent a massive rewrite in order to solve some problems
 * with the original code. In particular the original code failed to
 * wake up processes that were waiting for semval to go to 0 if the
 * value went to 0 and was then incremented rapidly enough. In solving
 * this problem I have also modified the implementation so that it
 * processes pending operations in a FIFO manner, thus give a guarantee
 * that processes waiting for a lock on the semaphore won't starve
 * unless another locking process fails to unlock.
 * In addition the following two changes in behavior have been introduced:
 * - The original implementation of semop returned the value
 *   last semaphore element examined on success. This does not
 *   match the manual page specifications, and effectively
 *   allows the user to read the semaphore even if they do not
 *   have read permissions. The implementation now returns 0
 *   on success as stated in the manual page.
 * - There is some confusion over whether the set of undo adjustments
 *   to be performed at exit should be done in an atomic manner.
 *   That is, if we are attempting to decrement the semval should we queue
 *   up and wait until we can do so legally?
 *   The original implementation attempted to do this.
 *   The current implementation does not do so. This is because I don't
 *   think it is the right thing (TM) to do, and because I couldn't
 *   see a clean way to get the old behavior with the new design.
 *   The POSIX standard and SVID should be consulted to determine
 *   what behavior is mandated.
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
	信号量是一个计数器，常用于处理进程或线程的同步问题，尤其是对临界资源的访问
	临界资源可以简单地说是一段代码，一个变量或某种硬件资源等。信号量的值大于0
	表示可供并发进程使用的资源的实体数，小于0表示正在等待使用该种资源的进程数
*/
#include <linux/errno.h>
#include <asm/segment.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/sem.h>
#include <linux/ipc.h>
#include <linux/stat.h>
#include <linux/malloc.h>

extern int ipcperms (struct ipc_perm *ipcp, short semflg);
static int newary (key_t, int, int);
static int findkey (key_t key);
static void freeary (int id);

//信号量描述符指针数组
static struct semid_ds *semary[SEMMNI];
static int used_sems = 0, used_semids = 0;
static struct wait_queue *sem_lock = NULL;
static int max_semid = 0;

static unsigned short sem_seq = 0;

//初始化信号量描述符指针数组
void sem_init (void)
{
	int i;

	sem_lock = NULL;
	used_sems = used_semids = max_semid = sem_seq = 0;
	//将数组中的每一项都置为未使用的状态
	for (i = 0; i < SEMMNI; i++)
		semary[i] = (struct semid_ds *) IPC_UNUSED;
	return;
}

static int findkey (key_t key)
{
	int id;
	struct semid_ds *sma;

	for (id = 0; id <= max_semid; id++) {
		while ((sma = semary[id]) == IPC_NOID)
			interruptible_sleep_on (&sem_lock);
		if (sma == IPC_UNUSED)
			continue;
		if (key == sma->sem_perm.key)
			return id;
	}
	return -1;
}

//创建一个新的信号量集合 参数nsems指的是在新创建的集合中信号量的个数
static int newary (key_t key, int nsems, int semflg)
{
	int id;
	struct semid_ds *sma;
	struct ipc_perm *ipcp;
	int size;

	if (!nsems)
		return -EINVAL;
	if (used_sems + nsems > SEMMNS)
		return -ENOSPC;
	//在信号量描述符指针数组中寻找一项未使用的项
	for (id = 0; id < SEMMNI; id++)
		//如果找到了
		if (semary[id] == IPC_UNUSED) {
			//将其置为正在被分配的状态
			semary[id] = (struct semid_ds *) IPC_NOID;
			goto found;
		}
	return -ENOSPC;
found:
	//size是信号量描述符结构和其信号量数组的字节数
	size = sizeof (*sma) + nsems * sizeof (struct sem);
	//增加系统中信号量数量
	used_sems += nsems;
	//分配内存空间
	sma = (struct semid_ds *) kmalloc (size, GFP_KERNEL);
	//如果分配失败
	if (!sma) {
		//将此信号量描述符置为未使用的状态
		semary[id] = (struct semid_ds *) IPC_UNUSED;
		//减少系统中信号量数量
		used_sems -= nsems;
		if (sem_lock)
			wake_up (&sem_lock);
		return -ENOMEM;
	}
	//对申请的内存空间进行清零操作
	memset (sma, 0, size);
	//sem_base指向信号量数组中的第一个信号量
	//sma是一个struct semid_ds类型的变量，由于此变量所占内存后面
	//紧跟着就是信号量集合数组，所以&sma[1]指向这个信号量数组首地址
/*
	semid_ds结构的sem_base指向一个信号量数组，允许操作这些信号量集合
	的进程可以利用系统调用执行操作。注意，信号量与信号量集合的区别
	从上面可以看出，信号量用“sem” 结构描述，而信量集合用“semid_ds"结构描述
*/
	sma->sem_base = (struct sem *) &sma[1];
	//初始化IPC权限结构
	ipcp = &sma->sem_perm;
	ipcp->mode = (semflg & S_IRWXUGO);
	ipcp->key = key;
	ipcp->cuid = ipcp->uid = current->euid;
	ipcp->gid = ipcp->cgid = current->egid;
	sma->sem_perm.seq = sem_seq;
	/* sma->sem_pending = NULL; */
	sma->sem_pending_last = &sma->sem_pending;
	/* sma->undo = NULL; */
	sma->sem_nsems = nsems;
	sma->sem_ctime = CURRENT_TIME;
	//更新max_semid的值，使其总是信号量描述符指针数组中最大的有效项下标索引
	if (id > max_semid)
		max_semid = id;
	//增加系统中的信号量集合数
	used_semids++;
	//将此信号量描述符指针指向初始化了的sma
	semary[id] = sma;
	if (sem_lock)
		wake_up (&sem_lock);
	//返回信号量标识id
	return (unsigned int) sma->sem_perm.seq * SEMMNI + id;
}

//为了创建一个新的信号量集合，或者存取一个已存在的集合，要使用segget()系统调用
/*
	(1) nsems>0  : 创建一个信的信号量集，指定集合中信号量的数量，一旦创建就不能更改。
	(2) nsems==0 : 访问一个已存在的集合
	(3) 返回的是一个称为信号量标识符的整数，semop和semctl函数将使用它。
	(4) 创建成功后信号量结构被设置：
    .sem_perm 的uid和gid成员被设置成的调用进程的有效用户ID和有效组ID
    .oflag 参数中的读写权限位存入sem_perm.mode
    .sem_otime 被置为0,sem_ctime被设置为当前时间
    .sem_nsems 被置为nsems参数的值
    该集合中的每个信号量不初始化，这些结构是在semctl，用参数SET_VAL，SETALL初始化的。
    semget函数执行成功后，就产生了一个由内核维持的类型为semid_ds结构体的信号量集，
	返回semid就是指向该信号量集的引索
*/
/*
	A)第一个参数key是整数值（唯一非零），不相关的进程可以通过它访问一个信号量，它代表程
	序可能要使用的某个资源，程序对所有信号量的访问都是间接的，程序先通过调用semget函数
	并提供一个键，再由系统生成一个相应的信号标识符（semget函数的返回值），只有semget函
	数才直接使用信号量键，所有其他的信号量函数使用由semget函数返回的信号量标识符。如果
	多个程序使用相同的key值，key将负责协调工作。
	B)第二个参数num_sems指定需要的信号量数目，它的值几乎总是1。
	C)第三个参数sem_flags是一组标志，当想要当信号量不存在时创建一个新的信号量，可以和值
	IPC_CREAT做按位或操作。设置了IPC_CREAT标志后，即使给出的键是一个已有信号量的键，也
	不会产生错误。而IPC_CREAT | IPC_EXCL则可以创建一个新的，唯一的信号量，如果信号量已
	存在，返回一个错误
*/
int sys_semget (key_t key, int nsems, int semflg)
{
	int id;
	struct semid_ds *sma;

	if (nsems < 0 || nsems > SEMMSL)
		return -EINVAL;	
	if (key == IPC_PRIVATE)
		return newary(key, nsems, semflg);
	if ((id = findkey (key)) == -1) {  /* key not used */
		//IPC_CREAT ：如果内核中没有新创建的信号量集合，则创建它
		if (!(semflg & IPC_CREAT))
			return -ENOENT;
		return newary(key, nsems, semflg);
	}
	//IPC_EXCL ：当与IPC_CREAT一起使用时，但信号量集合已经存在，则创建失败
	if (semflg & IPC_CREAT && semflg & IPC_EXCL)
		return -EEXIST;
	//执行到这里，说明是要取现有的信号量集合，而不是创建
	sma = semary[id];
	//如果指定的参数nsems大于此信号量集合中的信号量数
	//如果你是显式地打开一个现有的集合，则nsems参数可以忽略
	if (nsems > sma->sem_nsems)
		return -EINVAL;
	//权限检验
	if (ipcperms(&sma->sem_perm, semflg))
		return -EACCES;
	//返回此信号量集合的标识id
	return (unsigned int) sma->sem_perm.seq * SEMMNI + id;
}

/* Manage the doubly linked list sma->sem_pending as a FIFO:
 * insert new queue elements at the tail sma->sem_pending_last.
 */
static inline void insert_into_queue (struct semid_ds * sma, struct sem_queue * q)
{
	*(q->prev = sma->sem_pending_last) = q;
	*(sma->sem_pending_last = &q->next) = NULL;
}
static inline void remove_from_queue (struct semid_ds * sma, struct sem_queue * q)
{
	*(q->prev) = q->next;
	if (q->next)
		q->next->prev = q->prev;
	else /* sma->sem_pending_last == &q->next */
		sma->sem_pending_last = q->prev;
	q->prev = NULL; /* mark as removed */
}

/* Determine whether a sequence of semaphore operations would succeed
 * all at once. Return 0 if yes, 1 if need to sleep, else return error code.
 */
 /*
	sem_id是由semget返回的信号量标识符，sembuf结构的定义如下：
	struct sembuf{  
    short sem_num;//所要操作的信号量在信号量集合中的索引值
    short sem_op;//信号量在一次操作中需要改变的数据，通常是两个数，一个是-1，即P（等待）操作，  
                    //一个是+1，即V（发送信号）操作。  
    short sem_flg;//通常为SEM_UNDO,使操作系统跟踪信号，  
                    //并在进程没有释放该信号量而终止时，操作系统释放信号量  
	}; 
	sembuf是一个结构体,这个结构体定义了信号量的一些操作，应该是从用户空间传递进来的
 */
static int try_semop (struct semid_ds * sma, struct sembuf * sops, int nsops)
{
	int result = 0;
	int i = 0;

	//遍历这nsops个信号量
	while (i < nsops) {
		struct sembuf * sop = &sops[i];
		//curr指向当前要操作的信号量
		struct sem * curr = &sma->sem_base[sop->sem_num];
		//若当前要操作的信号量的值改变之后，大于最大的信号量值
		if (sop->sem_op + curr->semval > SEMVMX) {
			result = -ERANGE;
			break;
		}
		if (!sop->sem_op && curr->semval) {
			if (sop->sem_flg & IPC_NOWAIT)
				result = -EAGAIN;
			else
				result = 1;
			break;
		}
		i++;
		//改变当前要操作的信号量的值
		curr->semval += sop->sem_op;
		//如果改变之后的信号量值小于0
		if (curr->semval < 0) {
			//如果指定了IPC_NOWAIT标志，则返回-EAGAIN
			if (sop->sem_flg & IPC_NOWAIT)
				result = -EAGAIN;
			else
				result = 1;
			break;
		}
	}
	//再将改变了的信号量值复原
	while (--i >= 0) {
		struct sembuf * sop = &sops[i];
		struct sem * curr = &sma->sem_base[sop->sem_num];
		curr->semval -= sop->sem_op;
	}
	return result;
}

/* Actually perform a sequence of semaphore operations. Atomically. */
/* This assumes that try_semop() already returned 0. */
static int do_semop (struct semid_ds * sma, struct sembuf * sops, int nsops,
		     struct sem_undo * un, int pid)
{
	int i;

	for (i = 0; i < nsops; i++) {
		struct sembuf * sop = &sops[i];
		//curr指向当前要操作的信号量
		struct sem * curr = &sma->sem_base[sop->sem_num];
		//若当前要操作的信号量的值改变之后，大于最大的信号量值
		if (sop->sem_op + curr->semval > SEMVMX) {
			printk("do_semop: race\n");
			break;
		}
		if (!sop->sem_op) {
			if (curr->semval) {
				printk("do_semop: race\n");
				break;
			}
		} else {
			curr->semval += sop->sem_op;
			if (curr->semval < 0) {
				printk("do_semop: race\n");
				break;
			}
			if (sop->sem_flg & SEM_UNDO)
				un->semadj[sop->sem_num] -= sop->sem_op;
		}
		/*在信号量上最后一次操作的进程识别号 */
		curr->sempid = pid;
	}
	sma->sem_otime = CURRENT_TIME;

	/* Previous implementation returned the last semaphore's semval.
	 * This is wrong because we may not have checked read permission,
	 * only write permission.
	 */
	return 0;
}

/* Go through the pending queue for the indicated semaphore
 * looking for tasks that can be completed. Keep cycling through
 * the queue until a pass is made in which no process is woken up.
 */
static void update_queue (struct semid_ds * sma)
{
	int wokeup, error;
	struct sem_queue * q;

	do {
		wokeup = 0;
		//遍历此信号量集合上待处理的挂起操作
		for (q = sma->sem_pending; q; q = q->next) {
			error = try_semop(sma, q->sops, q->nsops);
			/* Does q->sleeper still need to sleep? */
			if (error > 0)
				continue;
			/* Perform the operations the sleeper was waiting for */
			//只有try_semop()函数返回0，才会执行do_semop()函数
			if (!error)
				error = do_semop(sma, q->sops, q->nsops, q->undo, q->pid);
			q->status = error;
			/* Remove it from the queue */
			remove_from_queue(sma,q);
			/* Wake it up */
			wake_up_interruptible(&q->sleeper); /* doesn't sleep! */
			wokeup++;
		}
	} while (wokeup);
}

/* The following counts are associated to each semaphore:
 *   semncnt        number of tasks waiting on semval being nonzero
 *   semzcnt        number of tasks waiting on semval being zero
 * This model assumes that a task waits on exactly one semaphore.
 * Since semaphore operations are to be performed atomically, tasks actually
 * wait on a whole sequence of semaphores simultaneously（一齐，同时地）.
 * The counts we return here are a rough approximation, but still
 * warrant that semncnt+semzcnt>0 if the task is on the pending queue.
 */
 //统计阻塞在指定信号量集合sma上指定的信号semnum上的，并且信号量操作值小于0的进程数
static int count_semncnt (struct semid_ds * sma, ushort semnum)
{
	int semncnt;
	struct sem_queue * q;

	semncnt = 0;
	//遍历此信号量集合上待处理的挂起操作
	for (q = sma->sem_pending; q; q = q->next) {
		//sembuf指向挂起操作结构的数组
		struct sembuf * sops = q->sops;
		//nsops为操作个数
		int nsops = q->nsops;
		int i;
		//遍历操作数组
		for (i = 0; i < nsops; i++)
			//如果当前的操作信号是指定的信号，并且信号量操作值小于0，并且没有指定不要阻塞标志
			if (sops[i].sem_num == semnum
			    && (sops[i].sem_op < 0)
			    && !(sops[i].sem_flg & IPC_NOWAIT))
				semncnt++;
	}
	return semncnt;
}
//统计阻塞在指定信号量集合sma上指定的信号semnum上的，并且信号量操作值等于0的进程数
static int count_semzcnt (struct semid_ds * sma, ushort semnum)
{
	int semzcnt;
	struct sem_queue * q;

	semzcnt = 0;
	//遍历此信号量集合上待处理的挂起操作
	for (q = sma->sem_pending; q; q = q->next) {
		struct sembuf * sops = q->sops;
		int nsops = q->nsops;
		int i;
		//遍历操作数组
		for (i = 0; i < nsops; i++)
			if (sops[i].sem_num == semnum
			    && (sops[i].sem_op == 0)
			    && !(sops[i].sem_flg & IPC_NOWAIT))
				semzcnt++;
	}
	return semzcnt;
}

/* Free a semaphore set. */
//释放一个信号量集合
static void freeary (int id)
{
	//取此信号量集合描述符
	struct semid_ds *sma = semary[id];
	struct sem_undo *un;
	struct sem_queue *q;

	/* Invalidate this semaphore set */
	//通过自增此信号量集合的序列号来使其无效
	sma->sem_perm.seq++;
	sem_seq = (sem_seq+1) % ((unsigned)(1<<31)/SEMMNI); /* increment, but avoid overflow */
	//减少系统中的信号量数
	used_sems -= sma->sem_nsems;
	//更新max_semid值
	if (id == max_semid)
		while (max_semid && (semary[--max_semid] == IPC_UNUSED));
	//将此信号量集合描述符指针项置为未使用的状态
	semary[id] = (struct semid_ds *) IPC_UNUSED;
	//减少系统中使用的信号量集合数
	used_semids--;

	/* Invalidate the existing undo structures for this semaphore set.
	 * (They will be freed without any further action in sem_exit().)
	 */
	for (un = sma->undo; un; un = un->id_next)
		un->semid = -1;

	/* Wake up all pending processes and let them fail with EIDRM. */
	for (q = sma->sem_pending; q; q = q->next) {
		q->status = -EIDRM;
		q->prev = NULL;
		wake_up_interruptible(&q->sleeper); /* doesn't sleep! */
	}

	kfree(sma);
}
/*
	第一个参数是信号量集IPC标识符。第二个参数是操作信号在信号集中的编号，第一个信号的编号是0
	参数cmd中可以使用的命令如下：
	·IPC_STAT读取一个信号量集的数据结构semid_ds，并将其存储在semun中的buf参数中。
	·IPC_SET设置信号量集的数据结构semid_ds中的元素ipc_perm，其值取自semun中的buf参数。
	·IPC_RMID将信号量集从内存中删除。
	·GETALL用于读取信号量集中的所有信号量的值。
	·GETNCNT返回正在等待资源的进程数目。
	·GETPID返回最后一个执行semop操作的进程的PID。
	·GETVAL返回信号量集中的一个单个的信号量的值。
	·GETZCNT返回这在等待完全空闲的资源的进程数目。
	·SETALL设置信号量集中的所有的信号量的值。
	·SETVAL设置信号量集中的一个单独的信号量的值。
	参数arg代表一个semun的实例。semun是在linux/sem.h中定义的：
	/*arg for semctl systemcalls.*/
//	union semun{
//	int val;/*value for SETVAL*/
//	struct semid_ds *buf;/*buffer for IPC_STAT&IPC_SET*/
//	ushort *array;/*array for GETALL&SETALL*/
//	struct seminfo *__buf;/*buffer for IPC_INFO*/
//	void *__pad;
//	val当执行SETVAL命令时使用。buf在IPC_STAT/IPC_SET命令中使用。代表了内核中使用的信号量的数据结构。
//	array在使用GETALL/SETALL命令时使用的指针。

int sys_semctl (int semid, int semnum, int cmd, union semun arg)
{
	struct semid_ds *buf = NULL;
	struct semid_ds tbuf;
	int i, id, val = 0;
	struct semid_ds *sma;
	struct ipc_perm *ipcp;
	struct sem *curr = NULL;
	struct sem_undo *un;
	unsigned int nsems;
	ushort *array = NULL;
	ushort sem_io[SEMMSL];

	if (semid < 0 || semnum < 0 || cmd < 0)
		return -EINVAL;

	switch (cmd) {
		case IPC_INFO:
		case SEM_INFO:
		{
			struct seminfo seminfo, *tmp = arg.__buf;
			seminfo.semmni = SEMMNI;
			seminfo.semmns = SEMMNS;
			seminfo.semmsl = SEMMSL;
			seminfo.semopm = SEMOPM;
			seminfo.semvmx = SEMVMX;
			seminfo.semmnu = SEMMNU;
			seminfo.semmap = SEMMAP;
			seminfo.semume = SEMUME;
			seminfo.semusz = SEMUSZ;
			seminfo.semaem = SEMAEM;
			if (cmd == SEM_INFO) {
				seminfo.semusz = used_semids;
				seminfo.semaem = used_sems;
			}
			i = verify_area(VERIFY_WRITE, tmp, sizeof(struct seminfo));
			if (i)
				return i;
			memcpy_tofs (tmp, &seminfo, sizeof(struct seminfo));
			return max_semid;
		}

		case SEM_STAT:
			buf = arg.buf;
			i = verify_area (VERIFY_WRITE, buf, sizeof (*buf));
			if (i)
				return i;
			if (semid > max_semid)
				return -EINVAL;
			sma = semary[semid];
			if (sma == IPC_UNUSED || sma == IPC_NOID)
				return -EINVAL;
			//验证读权限
			if (ipcperms (&sma->sem_perm, S_IRUGO))
				return -EACCES;
			id = (unsigned int) sma->sem_perm.seq * SEMMNI + semid;
			tbuf.sem_perm   = sma->sem_perm;
			tbuf.sem_otime  = sma->sem_otime;
			tbuf.sem_ctime  = sma->sem_ctime;
			tbuf.sem_nsems  = sma->sem_nsems;
			memcpy_tofs (buf, &tbuf, sizeof(*buf));
			return id;
	}//end of switch (cmd)

	id = (unsigned int) semid % SEMMNI;
	sma = semary [id];
	if (sma == IPC_UNUSED || sma == IPC_NOID)
		return -EINVAL;
	ipcp = &sma->sem_perm;
	nsems = sma->sem_nsems;
	if (sma->sem_perm.seq != (unsigned int) semid / SEMMNI)
		return -EIDRM;

	switch (cmd) {
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case SETVAL:
		if (semnum >= nsems)
			return -EINVAL;
		curr = &sma->sem_base[semnum];
		break;
	}

	switch (cmd) {
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		switch (cmd) {
		case GETVAL : return curr->semval;
		case GETPID : return curr->sempid;
		case GETNCNT: return count_semncnt(sma,semnum);
		case GETZCNT: return count_semzcnt(sma,semnum);
		case GETALL:
			array = arg.array;
			i = verify_area (VERIFY_WRITE, array, nsems*sizeof(ushort));
			if (i)
				return i;
		}
		break;
	case SETVAL:
		val = arg.val;
		if (val > SEMVMX || val < 0)
			return -ERANGE;
		break;
	case IPC_RMID:
		if (suser() || current->euid == ipcp->cuid || current->euid == ipcp->uid) {
			freeary (id);
			return 0;
		}
		return -EPERM;
	case SETALL: /* arg is a pointer to an array of ushort */
		array = arg.array;
		if ((i = verify_area (VERIFY_READ, array, nsems*sizeof(ushort))))
			return i;
		memcpy_fromfs (sem_io, array, nsems*sizeof(ushort));
		for (i = 0; i < nsems; i++)
			if (sem_io[i] > SEMVMX)
				return -ERANGE;
		break;
	case IPC_STAT:
		buf = arg.buf;
		if ((i = verify_area (VERIFY_WRITE, buf, sizeof(*buf))))
			return i;
		break;
	case IPC_SET:
		buf = arg.buf;
		if ((i = verify_area (VERIFY_READ, buf, sizeof (*buf))))
			return i;
		memcpy_fromfs (&tbuf, buf, sizeof (*buf));
		break;
	}

	if (semary[id] == IPC_UNUSED || semary[id] == IPC_NOID)
		return -EIDRM;
	if (sma->sem_perm.seq != (unsigned int) semid / SEMMNI)
		return -EIDRM;

	switch (cmd) {
	case GETALL:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		for (i = 0; i < sma->sem_nsems; i++)
			sem_io[i] = sma->sem_base[i].semval;
		memcpy_tofs (array, sem_io, nsems*sizeof(ushort));
		break;
	case SETVAL:
		if (ipcperms (ipcp, S_IWUGO))
			return -EACCES;
		for (un = sma->undo; un; un = un->id_next)
			un->semadj[semnum] = 0;
		curr->semval = val;
		sma->sem_ctime = CURRENT_TIME;
		/* maybe some queued-up processes were waiting for this */
		update_queue(sma);
		break;
	case IPC_SET:
		if (suser() || current->euid == ipcp->cuid || current->euid == ipcp->uid) {
			ipcp->uid = tbuf.sem_perm.uid;
			ipcp->gid = tbuf.sem_perm.gid;
			ipcp->mode = (ipcp->mode & ~S_IRWXUGO)
				| (tbuf.sem_perm.mode & S_IRWXUGO);
			sma->sem_ctime = CURRENT_TIME;
			return 0;
		}
		return -EPERM;
	case IPC_STAT:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		tbuf.sem_perm   = sma->sem_perm;
		tbuf.sem_otime  = sma->sem_otime;
		tbuf.sem_ctime  = sma->sem_ctime;
		tbuf.sem_nsems  = sma->sem_nsems;
		memcpy_tofs (buf, &tbuf, sizeof(*buf));
		break;
	case SETALL:
		if (ipcperms (ipcp, S_IWUGO))
			return -EACCES;
		for (i = 0; i < nsems; i++)
			sma->sem_base[i].semval = sem_io[i];
		for (un = sma->undo; un; un = un->id_next)
			for (i = 0; i < nsems; i++)
				un->semadj[i] = 0;
		sma->sem_ctime = CURRENT_TIME;
		/* maybe some queued-up processes were waiting for this */
		update_queue(sma);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int sys_semop (int semid, struct sembuf *tsops, unsigned nsops)
{
	int i, id, size, error;
	struct semid_ds *sma;
	struct sembuf sops[SEMOPM], *sop;
	struct sem_undo *un;
	int undos = 0, alter = 0;

	if (nsops < 1 || semid < 0)
		return -EINVAL;
	if (nsops > SEMOPM)
		return -E2BIG;
	if (!tsops)
		return -EFAULT;
	//验证tsops所指向的用户空间
	if ((i = verify_area (VERIFY_READ, tsops, nsops * sizeof(*tsops))))
		return i;
	//将tsops指向的用户空间操作数组数据从用户空间复制到内核空间sops[SEMOPM]操作数组中
	memcpy_fromfs (sops, tsops, nsops * sizeof(*tsops));
	//取信号量集合标识id
	id = (unsigned int) semid % SEMMNI;
	//如果此信号量集合描述符指针项为未使用的状态或者正在被分配或销毁的状态
	if ((sma = semary[id]) == IPC_UNUSED || sma == IPC_NOID)
		return -EINVAL;
	//如果序列号不一致，说明信号量集合处于被删除的状态
	if (sma->sem_perm.seq != (unsigned int) semid / SEMMNI)
		return -EIDRM;
	//遍历操作数组
	for (i = 0; i < nsops; i++) {
		sop = &sops[i];
		//如果当前操作指定的信号号大于此信号量集合中的信号量数
		if (sop->sem_num >= sma->sem_nsems)
			return -EFBIG;
		//统计要执行的undo请求数
		if (sop->sem_flg & SEM_UNDO)
			undos++;
		if (sop->sem_op)
			alter++;
	}
	//如果alter为0，表示要读取当前信号量值
	if (ipcperms(&sma->sem_perm, alter ? S_IWUGO : S_IRUGO))
		return -EACCES;
	error = try_semop(sma, sops, nsops);
	if (error < 0)
		return error;
	//如果undo请求数不为0，就要记录
	if (undos) {
		/* Make sure we have an undo structure
		 * for this process and this semaphore set.
		 */
		//遍历当前进程的semundo队列，试图找到相应的信号量集的操作记录结构
		for (un = current->semundo; un; un = un->proc_next)
			if (un->semid == semid)
				break;
		//如果没有找到相应的信号量集的操作记录结构
		if (!un) {
			//分配一个sem_undo结构
			size = sizeof(struct sem_undo) + sizeof(short)*sma->sem_nsems;
			un = (struct sem_undo *) kmalloc(size, GFP_ATOMIC);
			if (!un)
				return -ENOMEM;
			memset(un, 0, size);
			//semadj指向sem_undo结构变量后面紧跟的信号量数组，用于undo操作
			un->semadj = (short *) &un[1];
			un->semid = semid;
			//将此sem_undo结构插入当前进程的semundo队列
			un->proc_next = current->semundo;
			current->semundo = un;
			//将此sem_undo结构插入到所属信号量集的undo队列
			un->id_next = sma->undo;
			sma->undo = un;
		}
	} else
		un = NULL;
	//error为0，表示try_semop()函数返回0，即所要进行操作的所有操作信号量都满足操作条件
	if (error == 0) {
		/* the operations go through immediately */
		error = do_semop(sma, sops, nsops, un, current->pid);
		/* maybe some queued-up（排队等候） processes were waiting for this */
		update_queue(sma);
		return error;
	//否则，对给定的所有信号量的操作不能作为一个整体来一次性完成，进程就要睡眠在此信号量集上
	} else {
		/* We need to sleep on this operation, so we put the current
		 * task into the pending queue and go to sleep.
		 */
		//分配一个sem_queue类型的局部变量
		struct sem_queue queue;

		//初始化sem_queue
		queue.sma = sma;
		queue.sops = sops;
		queue.nsops = nsops;
		queue.undo = un;
		queue.pid = current->pid;
		queue.status = 0;
		//将queue插入到此信号量集的睡眠队列中
		insert_into_queue(sma,&queue);
		queue.sleeper = NULL;
		current->semsleeping = &queue;
		interruptible_sleep_on(&queue.sleeper);
		current->semsleeping = NULL;
		/* When we wake up, either the operation is finished,
		 * or some kind of error happened.
		 */
		if (!queue.prev) {
			/* operation is finished, update_queue() removed us */
			return queue.status;
		} else {
			remove_from_queue(sma,&queue);
			return -EINTR;
		}
	}
}

/*
 * add semadj values to semaphores, free undo structures.
 * undo structures are not freed when semaphore arrays are destroyed
 * so some of them may be out of date.
 * IMPLEMENTATION NOTE: There is some confusion over whether the
 * set of adjustments that needs to be done should be done in an atomic
 * manner or not. That is, if we are attempting to decrement the semval
 * should we queue up and wait until we can do so legally?
 * The original implementation attempted to do this (queue and wait).
 * The current implementation does not do so. The POSIX standard
 * and SVID should be consulted to determine what behavior is mandated.
 */
void sem_exit (void)
{
	struct sem_queue *q;
	struct sem_undo *u, *un = NULL, **up, **unp;
	struct semid_ds *sma;
	int nsems, i;

	/* If the current process was sleeping for a semaphore,
	 * remove it from the queue.
	 */
	//如果当前进程睡眠在一个信号量集合上 则将其从此信号量集合的睡眠队列中移除
	if ((q = current->semsleeping)) {
		if (q->prev)
			remove_from_queue(q->sma,q);
		current->semsleeping = NULL;
	}

	//遍历当前进程的semundo队列
	for (up = &current->semundo; (u = *up); *up = u->proc_next, kfree(u)) {
		if (u->semid == -1)
			continue;
		//取当前被遍历的semundo队列元素对应的信号量集合描述符结构
		sma = semary[(unsigned int) u->semid % SEMMNI];
		//如果此信号量集合描述符结构处于未使用的状态或者正在被分配或销毁的状态
		if (sma == IPC_UNUSED || sma == IPC_NOID)
			continue;
		//如果此信号量集合处于被删除的状态
		if (sma->sem_perm.seq != (unsigned int) u->semid / SEMMNI)
			continue;
		/* remove u from the sma->undo list */
		//遍历此信号量集合描述符结构对象中的undo队列，找到相应的undo结构并移除
		for (unp = &sma->undo; (un = *unp); unp = &un->id_next) {
			if (u == un)
				goto found;
		}
		printk ("sem_exit undo list error id=%d\n", u->semid);
		break;
found:
		*unp = un->id_next;
		/* perform adjustments registered in u */
		//取此信号量集合中的信号量数组大小
		nsems = sma->sem_nsems;
		//遍历此信号量集合中的信号量数组
		for (i = 0; i < nsems; i++) {
			//取当前数组项
			struct sem * sem = &sma->sem_base[i];
			//做undo操作
			sem->semval += u->semadj[i];
			if (sem->semval < 0)
				sem->semval = 0; /* shouldn't happen */
			sem->sempid = current->pid;
		}
		sma->sem_otime = CURRENT_TIME;
		/* maybe some queued-up processes were waiting for this */
		update_queue(sma);
	}
	//置空当前进程的semundo队列
	current->semundo = NULL;
}
