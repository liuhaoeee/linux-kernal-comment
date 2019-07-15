/*
 *  linux/fs/locks.c
 *
 *  Provide support for fcntl()'s F_GETLK, F_SETLK, and F_SETLKW calls.
 *  Doug Evans, 92Aug07, dje@sspiff.uucp.
 *
 *  Deadlock Detection added by Kelly Carmichael, kelly@[142.24.8.65]
 *  September 17, 1994.
 *
 *  FIXME: one thing isn't handled yet:
 *	- mandatory locks (requires lots of changes elsewhere)
 *
 *  Edited by Kai Petzke, wpp@marie.physik.tu-berlin.de
 *
 *  Converted file_lock_table to a linked list from an array, which eliminates
 *  the limits on how many active file locks are open - Chad Page
 *  (pageone@netcom.com), November 27, 1994 
 * 
 *  Removed dependency on file descriptors. dup()'ed file descriptors now
 *  get the same locks as the original file descriptors, and a close() on
 *  any file descriptor removes ALL the locks on the file for the current
 *  process. Since locks still depend on the process id, locks are inherited
 *  after an exec() but not after a fork(). This agrees with POSIX, and both
 *  BSD and SVR4 practice.
 *  Andy Walker (andy@keo.kvaerner.no), February 14, 1995
 *
 *  Scrapped free list which is redundant now that we allocate locks
 *  dynamically with kmalloc()/kfree().
 *  Andy Walker (andy@keo.kvaerner.no), February 21, 1995
 *
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
//可参看《深入理解Linux内核》中对文件锁的相关介绍
#define DEADLOCK_DETECTION

#include <asm/segment.h>

#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/fcntl.h>

#define OFFSET_MAX	((off_t)0x7fffffff)	/* FIXME: move elsewhere? */

static int copy_flock(struct file *filp, struct file_lock *fl, struct flock *l);
static int conflict(struct file_lock *caller_fl, struct file_lock *sys_fl);
static int overlap(struct file_lock *fl1, struct file_lock *fl2);
static int lock_it(struct file *filp, struct file_lock *caller);
static struct file_lock *alloc_lock(struct file_lock **pos, struct file_lock *fl);
static void free_lock(struct file_lock **fl);
#ifdef DEADLOCK_DETECTION
int locks_deadlocked(int my_pid,int blocked_pid);
#endif

//内核将所有的内核文件锁链接成一个双向链表，file_lock_table就指向该链表的头节点
//检查是否发生死锁会用到这个链表
static struct file_lock *file_lock_table = NULL;

/*
	这个接口是获取锁的相关信息： 这个接口会修改我们传入的struct flock。
	如果探测了一番，发现根本就没有进程对该文件指定数据段加锁，那么了l_type会被修改成F_UNLCK
	如果有进程持有了锁，那么了l_pid会返回持锁进程的PID 
*/
int fcntl_getlk(unsigned int fd, struct flock *l)
{
	int error;
	struct flock flock;
	struct file *filp;
	struct file_lock *fl,file_lock;

	if (fd >= NR_OPEN || !(filp = current->files->fd[fd]))
		return -EBADF;
	error = verify_area(VERIFY_WRITE,l, sizeof(*l));
	if (error)
		return error;
	//将l从用户空间复制到内核空间
	memcpy_fromfs(&flock, l, sizeof(flock));
	//如果要获取的锁信息表明其要获取没有上锁的锁
	//则返回EINVAL，因为锁要存在，就必然不是F_UNLCK类型的了
	if (flock.l_type == F_UNLCK)
		return -EINVAL;
	//根据请求锁变量flock初始化一个内核文件锁变量file_lock，用于与内核文件锁链表进行对比查找操作
	if (!copy_flock(filp, &file_lock, &flock))
		return -EINVAL;

	//遍历该文件当前所有的文件锁
	for (fl = filp->f_inode->i_flock; fl != NULL; fl = fl->fl_next) {
		//若链表中的锁有与由请求锁变量构造的文件锁冲突的，则表明查找到了我们想要
		//获取的锁 然后把此锁的相关信息复制到请求锁变量中，返回给用户
		if (conflict(&file_lock, fl)) {
			flock.l_pid = fl->fl_owner->pid;	//此锁所属的进程id
			flock.l_start = fl->fl_start;
			flock.l_len = fl->fl_end == OFFSET_MAX ? 0 :
				fl->fl_end - fl->fl_start + 1;
			flock.l_whence = fl->fl_whence;
			flock.l_type = fl->fl_type;
			memcpy_tofs(l, &flock, sizeof(flock));	//复制到用户空间
			return 0;
		}
	}

	//执行到这里，说明没有与之冲突的文件锁，即没有我们想要获取的锁
	//则将所类型设置为F_UNLCK，并返回给用户空间
	flock.l_type = F_UNLCK;			/* no conflict found */
	memcpy_tofs(l, &flock, sizeof(flock));
	return 0;
}

/*
 * This function implements both F_SETLK and F_SETLKW.
 */
//为指定的文件上锁，函数内部首先查找该文件是否存在和请求锁
//所冲突的锁，若有的话：
//如果是F_SETLK标志，则立即返回
//如果是F_SETLKW标志，将进程阻塞，等待与之冲突的锁解锁，然后上锁
int fcntl_setlk(unsigned int fd, unsigned int cmd, struct flock *l)
{
	int error;
	struct file *filp;
	struct file_lock *fl,file_lock;
	struct flock flock;

	/*
	 * Get arguments and validate them ...
	 */

	if (fd >= NR_OPEN || !(filp = current->files->fd[fd]))
		return -EBADF;
	error = verify_area(VERIFY_READ, l, sizeof(*l));
	if (error)
		return error;
	memcpy_fromfs(&flock, l, sizeof(flock));
	if (!copy_flock(filp, &file_lock, &flock))
		return -EINVAL;
	//请求锁的类型
	switch (file_lock.fl_type) {
	//如果是读锁
	case F_RDLCK :
		//如果文件不是以读方式打开的
		if (!(filp->f_mode & 1))
			return -EBADF;
		break;
	//如果是写锁
	case F_WRLCK :
		//如果文件不是以写方式打开的
		if (!(filp->f_mode & 2))
			return -EBADF;
		break;
	//如果是共享锁
	case F_SHLCK :
		//如果文件不可读也不可写
		if (!(filp->f_mode & 3))
			return -EBADF;
		file_lock.fl_type = F_RDLCK;
		break;
	//如果是排他锁
	case F_EXLCK :
		//如果文件不可读也不可写
		if (!(filp->f_mode & 3))
			return -EBADF;
		file_lock.fl_type = F_WRLCK;
		break;
	//这个标志应该是要求内核解锁相应的锁
	case F_UNLCK :
		break;
	}

  	/*
  	 * Scan for a conflicting lock ...
  	 */
	//如果不是要求内核解锁相应的锁，即是要求设置相应的锁
	if (file_lock.fl_type != F_UNLCK) {
repeat:
		//遍历该文件的内核文件锁链表
		for (fl = filp->f_inode->i_flock; fl != NULL; fl = fl->fl_next) {
			//如果内核文件锁与请求锁不冲突（即无关），则继续遍历下一项文件锁
			//否则进行下面的处理，若cmd == F_SETLKW，则内核会阻塞调用进程
			//直到被唤醒(独占锁被解锁？)...
			if (!conflict(&file_lock, fl))
				continue;
			/*
			 * File is locked by another process. If this is
			 * F_SETLKW wait for the lock to be released.
			 */
			//和F_SETLK几乎一样，唯一的区别，这厮是个死心眼的主儿，申请不到，就傻等
			//除了共享锁或独占锁被其他的锁阻塞这种情况外，这个命令和F_SETLK是一样的。如果共享锁或独占锁被其他的锁阻塞，
		    //进程将等待直到这个请求能够完成。当fcntl()正在等待文件的某个区域的时候捕捉到一个信号，如果这个信号没有被
			//指定SA_RESTART, fcntl将被中断,当一个共享锁被set到一个文件的某段的时候，其他的进程可以set共享锁到这个段或
			//这个段的一部分。共享锁阻止任何其他进程set独占锁到这段保护区域的任何部分。如果文件描述符没有以读的访问方式
			//打开的话，共享锁的设置请求会失败。独占锁阻止任何其他的进程在这段保护区域任何位置设置共享锁或独占锁。
			//如果文件描述符不是以写的访问方式打开的话，独占锁的请求会失败
			if (cmd == F_SETLKW) {
				//如果进程有除了阻塞的其他信号，则返回ERESTARTSYS，标志内核处理信号之后重新启动此调用
				if (current->signal & ~current->blocked)
					return -ERESTARTSYS;
#ifdef DEADLOCK_DETECTION
				//检查是否产生死锁
				if (locks_deadlocked(file_lock.fl_owner->pid,fl->fl_owner->pid))
					return -EDEADLOCK;
#endif
				interruptible_sleep_on(&fl->fl_wait);
				//睡眠期间，进程可能收到信号
				if (current->signal & ~current->blocked)
					return -ERESTARTSYS;
				goto repeat;	//退出大循环的条件是，当文件锁链表中没有与请求锁冲突的锁时
			}
			//执行到这里说明cmd==F_SETLK
			//F_SETLK被用来实现共享(或读)锁(F_RDLCK)或独占(写)锁(F_WRLCK)，同样可以去掉这两种锁(F_UNLCK)。
			//如果共享锁或独占锁不能被设置，fcntl()将立即返回EAGAIN
			return -EAGAIN;
  		}
  	}

	/*
	 * Lock doesn't conflict with any other lock ...
	 */
	//执行到这里若file_lock.fl_type == F_UNLCK，则请求锁要求内核解锁此锁
	//lock_it函数会根据file_lock文件锁的类型变量来决定是要上锁还是要解锁
	return lock_it(filp, &file_lock);
}

#ifdef DEADLOCK_DETECTION
/*
 * This function tests for deadlock(死锁) condition before putting a process to sleep
 * this detection scheme is recursive... we may need some test as to make it
 * exit if the function gets stuck due to bad lock data.
 */

 //检查在等待锁之间的进程有没有产生死锁条件 然后把当前进程插入到阻塞进程的循环链表中 并挂起当前进程
int locks_deadlocked(int my_pid,int blocked_pid)
{
	int ret_val;
	struct wait_queue *dlock_wait;
	struct file_lock *fl;
	for (fl = file_lock_table; fl != NULL; fl = fl->fl_nextlink) {
		if (fl->fl_owner == NULL) continue;	/* not a used lock */
		if (fl->fl_owner->pid != my_pid) continue;
		if (fl->fl_wait == NULL) continue;	/* no queues */
		dlock_wait = fl->fl_wait;
		do {
			if (dlock_wait->task != NULL) {
				if (dlock_wait->task->pid == blocked_pid)
					return -EDEADLOCK;
				ret_val = locks_deadlocked(dlock_wait->task->pid,blocked_pid);
				if (ret_val)
					return -EDEADLOCK;
			}
			dlock_wait = dlock_wait->next;
		} while (dlock_wait != fl->fl_wait);
	}
	return 0;
}
#endif

/*
 * This function is called when the file is closed.
 */

 //释放指定文件的锁锁主为指定进程的锁
void fcntl_remove_locks(struct task_struct *task, struct file *filp)
{
	struct file_lock *fl;
	struct file_lock **before;

	/* Find first lock owned by caller ... */
 
	//before指向该文件的文件锁链表头
	before = &filp->f_inode->i_flock;
	//遍历该文件锁链表，找到锁主进程是task的锁
	while ((fl = *before) && task != fl->fl_owner)
		before = &fl->fl_next;

	/* The list is sorted by owner and fd ... */

	//上面的注释指出：文件锁链表已经按照锁主和fd排序
	//所以以下的while循环可以遍历到该文件锁锁主是task的锁
	//然后释放锁
	while ((fl = *before) && task == fl->fl_owner)
		free_lock(before);
}

/*
 * Verify a "struct flock" and copy it to a "struct file_lock" ...
 * Result is a boolean indicating success.
 */

 //根据用户空间传来的请求锁结构填充一个内核文件锁结构
static int copy_flock(struct file *filp, struct file_lock *fl, struct flock *l)
{
	off_t start;

	if (!filp->f_inode)	/* just in case */
		return 0;
	//验证请求锁的类型是否在有效范围内
	if (l->l_type != F_UNLCK && l->l_type != F_RDLCK && l->l_type != F_WRLCK
	 && l->l_type != F_SHLCK && l->l_type != F_EXLCK)
		return 0;
	switch (l->l_whence) {
	case 0 /*SEEK_SET*/ : start = 0; break;
	case 1 /*SEEK_CUR*/ : start = filp->f_pos; break;
	case 2 /*SEEK_END*/ : start = filp->f_inode->i_size; break;
	default : return 0;
	}
	//start += l->l_start得到相对文件开始位置处的绝对偏移地址
	//因为l->l_start是相对于参数l->l_whence的一个偏移值
	if ((start += l->l_start) < 0 || l->l_len < 0)
		return 0;
	fl->fl_type = l->l_type;
	fl->fl_start = start;	/* we record the absolute position */
	fl->fl_whence = 0;	/* FIXME: do we record {l_start} as passed? */
	if (l->l_len == 0 || (fl->fl_end = start + l->l_len - 1) < 0)
		fl->fl_end = OFFSET_MAX;
	fl->fl_owner = current;
	fl->fl_wait = NULL;		/* just for cleanliness */
	return 1;
}

/*
 * Determine if lock {sys_fl} blocks lock {caller_fl} ...
 */

 //检测两个文件锁是否产生冲突
static int conflict(struct file_lock *caller_fl, struct file_lock *sys_fl)
{
	//如果两个文件锁的拥有者相同，返回0表示不会冲突
	//我想，应该是1.2内核不支持多线程，所以于同一个进程
	//对一个文件的读写顺序是一定的，不会产生问题
	//即相冲突的锁必然属于不同的进程
	if (caller_fl->fl_owner == sys_fl->fl_owner)
		return 0;
	//如果两个锁锁住的文件范围没有重叠部分，则返回0，表明不冲突
	if (!overlap(caller_fl, sys_fl))
		return 0;
	//执行到此处，说明两个锁锁住的文件范围有重叠部分，则两个锁可能有冲突
	//说”可能“是因为冲突还取决于两个锁的类型
	switch (caller_fl->fl_type) {
	//如果此锁是读锁
	case F_RDLCK :
		//当另一个锁也是读锁时，返回0，表明不冲突，反之，冲突
		return sys_fl->fl_type != F_RDLCK;
	//此锁是写锁
	case F_WRLCK :
		//写锁总是与其它锁冲突
		return 1;	/* overlapping region not owned by caller */
	}
	return 0;	/* shouldn't get here, but just in case */
}

//验证两个锁锁住的文件范围是否有重叠部分
static int overlap(struct file_lock *fl1, struct file_lock *fl2)
{
	return fl1->fl_end >= fl2->fl_start && fl2->fl_end >= fl1->fl_start;
}

/*
 * Add a lock to a file ...
 * Result is 0 for success or -ENOLCK.
 *
 * We merge(合并) adjacent locks whenever possible.
 *
 * WARNING: We assume the lock doesn't conflict with any other lock.
 */
  
/*
 * Rewritten by Kai Petzke:
 * We sort the lock list first by owner, then by the starting address.
 *
 * To make freeing a lock much faster, we keep a pointer to the lock before the
 * actual one. But the real gain of the new coding was, that lock_it() and
 * unlock_it() became one function.
 *
 * To all purists: Yes, I use a few goto's. Just pass on to the next function.
 */

 //锁链表首先以锁主进程排序，再以开始地址排序
 //新旧锁若类型不一致，新锁会覆盖与旧锁重叠的范围
 //理解了这一点，代码中的left\right就很容易理解了
 //注：新旧锁锁主一致
 //函数内的+1和-1操作是为了保证两个锁之间没有重叠的边界范围，即对于文件内的任一字节，只属于一个锁
static int lock_it(struct file *filp, struct file_lock *caller)
{
	struct file_lock *fl;
	struct file_lock *left = 0;
	struct file_lock *right = 0;
	struct file_lock **before;
	int added = 0;

	/*
	 * Find the first old lock with the same owner as the new lock.
	 */

	//在文件锁链表中找到首个和指定锁锁主进程一样的锁
	before = &filp->f_inode->i_flock;
	while ((fl = *before) && caller->fl_owner != fl->fl_owner)
		before = &fl->fl_next;

	/*
	 * Look up all locks of this owner.
	 */

	 //遍历所有和指定锁锁主进程一样的锁
	while ((fl = *before) && caller->fl_owner == fl->fl_owner) {
		/*
		 * Detect(检查) adjacent(相邻) or overlapping regions (if same lock type)
		 */
		if (caller->fl_type == fl->fl_type) {
			//由于相同锁主的锁文件是相邻的，并且按照锁住的文件开始的地址大小排序的
			//所以我们需要将新的文件锁插入合适的位置上：
			if (fl->fl_end < caller->fl_start - 1)
				goto next_lock;
			/*
			 * If the next lock in the list has entirely bigger
			 * addresses than the new one, insert the lock here.
			 */
			if (fl->fl_start > caller->fl_end + 1)
				break;	//找到一个完全可以容纳新锁的锁区域，则break
						//直接跳到后面caller = alloc_lock(before, caller)函数处
						//将新锁插入链表即可,如下图所示：
						//fl:				------
						//caller:	------

			//执行到这里，有四种情况：（注意重叠的部分）
			//直线左边是锁范围start，右边是end
	//		A.
		//		fl:		------
		//		caller:		------
	//		B.
		//		fl:			--------
		//		caller:	--------
	//		C.
		//		fl:			--------
		//		caller:		   ----
	//		D.
		//		fl:			  ----
		//		caller:		--------
		//则根据情况将caller锁定的范围合并到fl锁中去，因为两者是类型一致的锁，所以可以合并
		
			/*
			 * If we come here, the new and old lock are of the
			 * same type and adjacent or overlapping. Make one
			 * lock yielding from the lower start address of both
			 * locks to the higher end address.
			 */
			if (fl->fl_start > caller->fl_start)
				fl->fl_start = caller->fl_start;
			else
				caller->fl_start = fl->fl_start;
			if (fl->fl_end < caller->fl_end)
				fl->fl_end = caller->fl_end;
			else
				caller->fl_end = fl->fl_end;
			//added应该是一个“合并”标志，记录函数之前的循环是否做过锁的合并操作
			if (added) {	//若之前做过合并，则由于上面又进行了一次合并，所以可以释放一个锁结构
				free_lock(before);
				continue;
			}
			caller = fl;
			added = 1;	//设置合并标志
			goto next_lock;	//检查下一个锁 如果两个锁类型一致，不会进入到下面不同类型锁的处理
			//由合并操作可以想到，文件的锁单链表中相同类型的锁必定是互不重叠的，但相邻锁之间可能存在空洞
		}
		/*
		 * Processing for different lock types is a bit more complex.
		 */
		 //一下四行和上面分析一致
		if (fl->fl_end < caller->fl_start)
			goto next_lock;
		if (fl->fl_start > caller->fl_end)
			break;
		//执行到这里，有四种情况：（注意重叠的部分）
			//直线左边是锁范围start，右边是end
	//		A.
		//		fl:		------
		//		caller:		------
	//		B.
		//		fl:			--------
		//		caller:	--------
	//		C.
		//		fl:			--------
		//		caller:		   ----
	//		D.
		//		fl:			  ----
		//		caller:		--------
		if (caller->fl_type == F_UNLCK)
			added = 1;
		if (fl->fl_start < caller->fl_start)
			left = fl;
		/*
		 * If the next lock in the list has a higher end address than
		 * the new one, insert the new one here.
		 */
		if (fl->fl_end > caller->fl_end) {
			right = fl;
			break;
		}
		if (fl->fl_start >= caller->fl_start) {
			/*
			 * The new lock completely replaces an old one (This may
			 * happen several times).
			 */
			if (added) {
				free_lock(before);
				continue;
			}
			/*
			 * Replace the old lock with the new one. Wake up
			 * anybody waiting for the old one, as the change in
			 * lock type might satisfy his needs.
			 */
			wake_up(&fl->fl_wait);
			fl->fl_start = caller->fl_start;
			fl->fl_end   = caller->fl_end;
			fl->fl_type  = caller->fl_type;
			caller = fl;
			added = 1;
		}
		/*
		 * Go on to next lock.
		 */
next_lock:
		before = &(*before)->fl_next;
	}

	//如果没有进行过合并操作，则进入下面的语句段
	//会为新锁申请一个内核锁，并插入链表
	if (! added) {
		if (caller->fl_type == F_UNLCK) {
/*
 * XXX - under iBCS-2, attempting to unlock a not-locked region is 
 * 	not considered an error condition, although I'm not sure if this 
 * 	should be a default behavior (it makes porting to native Linux easy)
 * 	or a personality option.
 *
 *	Does Xopen/1170 say anything about this?
 *	- drew@Colorado.EDU
 */
#if 0
			return -EINVAL;
#else
			return 0;
#endif
		}
		if (! (caller = alloc_lock(before, caller)))
			return -ENOLCK;
	}
	if (right) {
		if (left == right) {
			/*
			 * The new lock breaks the old one in two pieces, so we
			 * have to allocate one more lock (in this case, even
			 * F_UNLCK may fail!).
			 */
			if (! (left = alloc_lock(before, right))) {
				if (! added)
					free_lock(before);
				return -ENOLCK;
			}
		}
		right->fl_start = caller->fl_end + 1;
	}
	if (left)
		left->fl_end = caller->fl_start - 1;
	return 0;
}

/*
 * File_lock() inserts a lock at the position pos of the linked list.
 */
static struct file_lock *alloc_lock(struct file_lock **pos,
				    struct file_lock *fl)
{
	struct file_lock *tmp;

	/* Okay, let's make a new file_lock structure... */
	tmp = (struct file_lock *)kmalloc(sizeof(struct file_lock), GFP_KERNEL);
	if (!tmp)
		return tmp;
	//将新的锁插入内核文件锁链表中
	tmp->fl_nextlink = file_lock_table;
	tmp->fl_prevlink = NULL;
	if (file_lock_table != NULL)
		file_lock_table->fl_prevlink = tmp;
	file_lock_table = tmp;

	//将文件锁插入文件单链表中
	tmp->fl_next = *pos;	/* insert into file's list */
	*pos = tmp;

	tmp->fl_owner = current;
	tmp->fl_wait = NULL;

	tmp->fl_type = fl->fl_type;
	tmp->fl_whence = fl->fl_whence;
	tmp->fl_start = fl->fl_start;
	tmp->fl_end = fl->fl_end;

	return tmp;
}

/*
 * Free up a lock...
 */

 //释放指定的文件锁，就是将其从文件锁链表中移除
 //并释放锁结构
 //现将锁从文件的锁单链表中移除
 //再将其从内核文件锁双向链表中移除
static void free_lock(struct file_lock **fl_p)
{
	struct file_lock *fl;

	fl = *fl_p;
	*fl_p = (*fl_p)->fl_next;	//将锁从文件的锁单链表中移除

	//将锁从内核文件锁双向链表中移除
	if (fl->fl_nextlink != NULL)
		fl->fl_nextlink->fl_prevlink = fl->fl_prevlink;

	if (fl->fl_prevlink != NULL)
		fl->fl_prevlink->fl_nextlink = fl->fl_nextlink;
	else
		file_lock_table = fl->fl_nextlink;

	//唤醒等待该锁解锁的进程
	wake_up(&fl->fl_wait);

	kfree(fl);

	return;
}
