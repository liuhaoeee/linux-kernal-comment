/*
 *  linux/fs/fcntl.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
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
	dup系统调用的服务例程为sys_dup函数，sys_dup()的代码也许称得上是最简单的之一了
	但是就是这么一个简单的系统调用，却成就了linux系统最著名的一个特性：输入/输出重定向
	sys_dup()的主要工作就是用来“复制”一个打开的文件号，并使两个文件号都指向同一个文件
*/
#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/string.h>

extern int fcntl_getlk(unsigned int, struct flock *);
extern int fcntl_setlk(unsigned int, unsigned int, struct flock *);
extern int sock_fcntl (struct file *, unsigned int cmd, unsigned long arg);

//复制当前进程的“复制”一个打开的文件号，并使两个文件号都指向同一个文件
//参数arg指定复制得到的最小文件号
/*
	返回一个如下描述的(文件)描述符：
	·最小的大于或等于arg的一个可用的描述符
	·与原始操作符一样的某对象的引用
	·如果对象是文件(file)的话，则返回一个新的描述符，这个描述符与arg共享相同的偏移量(offset)
	·相同的访问模式(读，写或读/写)
	·相同的文件状态标志(如：两个文件描述符共享相同的状态标志)
	·与新的文件描述符结合在一起的close-on-exec标志被设置成交叉式访问execve(2)的系统调用
	
	实际上调用dup(oldfd)；
	等效于
	fcntl(oldfd, F_DUPFD, 0);
	而调用dup2(oldfd, newfd)；
	等效于
	close(oldfd)；
	fcntl(oldfd, F_DUPFD, newfd)；

*/
static int dupfd(unsigned int fd, unsigned int arg)
{
	if (fd >= NR_OPEN || !current->files->fd[fd])
		return -EBADF;
	if (arg >= NR_OPEN)
		return -EINVAL;
	while (arg < NR_OPEN)
		if (current->files->fd[arg])
			arg++;
		else
			break;
	if (arg >= NR_OPEN)
		return -EMFILE;
	FD_CLR(arg, &current->files->close_on_exec);	// 从关闭队列中清除该记录
	(current->files->fd[arg] = current->files->fd[fd])->f_count++;
	return arg;
}

asmlinkage int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
	if (oldfd >= NR_OPEN || !current->files->fd[oldfd])
		return -EBADF;
	if (newfd == oldfd)
		return newfd;
	/*
	 * errno's for dup2() are slightly different than for fcntl(F_DUPFD)
	 * for historical reasons.
	 */
	if (newfd > NR_OPEN)	/* historical botch - should have been >= */
		return -EBADF;	/* dupfd() would return -EINVAL */
#if 1
	if (newfd == NR_OPEN)
		return -EBADF;	/* dupfd() does return -EINVAL and that may
				 * even be the standard!  But that is too
				 * weird for now.
				 */
#endif
	sys_close(newfd);
	return dupfd(oldfd,newfd);
}

asmlinkage int sys_dup(unsigned int fildes)
{
	return dupfd(fildes,0);
}
/*
	[描述]
	fcntl()针对(文件)描述符提供控制。参数fd是被参数cmd操作(如下面的描述)的描述符。
	针对cmd的值，fcntl能够接受第三个参数int arg。

	[返回值]
	fcntl()的返回值与命令有关。
	fcntl函数有5种功能：
	1. 复制一个现有的描述符(cmd=F_DUPFD).
	2. 获得／设置文件描述符标记(cmd=F_GETFD或F_SETFD).
	3. 获得／设置文件状态标记(cmd=F_GETFL或F_SETFL).
	4. 获得／设置异步I/O所有权(cmd=F_GETOWN或F_SETOWN).
	5. 获得／设置记录锁(cmd=F_GETLK , F_SETLK或F_SETLKW).
	flock系统调用本质是给文件上锁，它比较死心眼，一锁就是整个文件，要求flock系统调用给某文件前40个字节上锁，
	不好意思，flock他老人家太老了，这么细的活儿干不了。但是fcntl不同了，它属于江湖晚辈，做的就比较细致了，
	他能够精确打击，让它给文件的某一个字节加锁，他都能办得到
	文件记录加锁相关的cmd 分三种（fcntl这厮还有其他于加锁无关的cmd）：
	F_SETLK
		申请锁（读锁F_RDLCK，写锁F_WRLCK）或者释放所（F_UNLCK），但是如果kernel无法将锁授予本进程
		（被其他进程抢了先，占了锁），不傻等，返回error
	F_SETLKW
		和F_SETLK几乎一样，唯一的区别，这厮是个死心眼的主儿，申请不到，就傻等。
	F_GETLK
		这个接口是获取锁的相关信息： 这个接口会修改我们传入的struct flock。
		如果探测了一番，发现根本就没有进程对该文件指定数据段加锁，那么了l_type会被修改成F_UNLCK
		如果有进程持有了锁，那么了l_pid会返回持锁进程的PID 
*/
asmlinkage int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;
	struct task_struct *p;
	int task_found = 0;

	if (fd >= NR_OPEN || !(filp = current->files->fd[fd]))
		return -EBADF;
	switch (cmd) {
		//复制一个现有的描述符
		case F_DUPFD:
			return dupfd(fd,arg);
		//取得与文件描述符fd联合的close-on-exec标志，类似FD_CLOEXEC。
		//如果返回值和FD_CLOEXEC进行与运算结果是0的话，文件保持交叉式访问exec()，
		//否则如果通过exec运行的话，文件将被关闭(arg 被忽略)  
		case F_GETFD:
			return FD_ISSET(fd, &current->files->close_on_exec);
		//设置close-on-exec标志，该标志以参数arg的FD_CLOEXEC位决定，
		//应当了解很多现存的涉及文件描述符标志的程序并不使用常数 FD_CLOEXEC，
		//而是将此标志设置为0(系统默认，在exec时不关闭)或1(在exec时关闭)
		//在修改文件描述符标志或文件状态标志时必须谨慎，先要取得现在的标志值，
		//然后按照希望修改它，最后设置新标志值。不能只是执行F_SETFD或F_SETFL命令，这样会关闭以前设置的标志位。 
		case F_SETFD:
			if (arg&1)
				FD_SET(fd, &current->files->close_on_exec);
			else
				FD_CLR(fd, &current->files->close_on_exec);
			return 0;
		//取得fd的文件状态标志，如同下面的描述一样(arg被忽略)，在说明open函数时，已说明
		//了文件状态标志。不幸的是，三个存取方式标志 (O_RDONLY , O_WRONLY , 以及O_RDWR)并不各占1位。
		//(这三种标志的值各是0 , 1和2，由于历史原因，这三种值互斥 — 一个文件只能有这三种值之一。) 
		//因此首先必须用屏蔽字O_ACCMODE相与取得存取方式位，然后将结果与这三种值相比较。      
		case F_GETFL:
			return filp->f_flags;
		//设置给arg描述符状态标志，可以更改的几个标志是：O_APPEND，O_NONBLOCK，O_SYNC 和 O_ASYNC。
		//而fcntl的文件状态标志总共有7个：O_RDONLY , O_WRONLY , O_RDWR , O_APPEND , O_NONBLOCK , O_SYNC和O_ASYNC
		//可更改的几个标志如下面的描述：
		//O_NONBLOCK   非阻塞I/O，如果read(2)调用没有可读取的数据，或者如果write(2)操作将阻塞，
						//则read或write调用将返回-1和EAGAIN错误
		//O_APPEND     强制每次写(write)操作都添加在文件大的末尾，相当于open(2)的O_APPEND标志
		//O_DIRECT     最小化或去掉reading和writing的缓存影响。系统将企图避免缓存你的读或写的数据。
					//如果不能够避免缓存，那么它将最小化已经被缓存了的数据造成的影响。如果这个标志用的不够好，将大大的降低性能
		//O_ASYNC      当I/O可用的时候，允许SIGIO信号发送到进程组，例如：当有数据可以读的时候
		case F_SETFL:
			/*
			 * In the case of an append-only file, O_APPEND
			 * cannot be cleared
			 */
			if (IS_APPEND(filp->f_inode) && !(arg & O_APPEND))
				return -EPERM;
			if ((arg & FASYNC) && !(filp->f_flags & FASYNC) &&
			    filp->f_op->fasync)
				filp->f_op->fasync(filp->f_inode, filp, 1);	//启动文件的FASYNC
			if (!(arg & FASYNC) && (filp->f_flags & FASYNC) &&
			    filp->f_op->fasync)
				filp->f_op->fasync(filp->f_inode, filp, 0);	//关闭文件的FASYNC
			filp->f_flags &= ~(O_APPEND | O_NONBLOCK | FASYNC);
			filp->f_flags |= arg & (O_APPEND | O_NONBLOCK |
						FASYNC);
			return 0;
		//通过第三个参数arg(一个指向flock的结构体)取得第一个阻塞lock description指向的锁。
		//取得的信息将覆盖传到fcntl()的flock结构的信息。如果没有发现能够阻止本次锁(flock)生成的锁，
		//这个结构将不被改变，除非锁的类型被设置成F_UNLCK
		case F_GETLK:
			return fcntl_getlk(fd, (struct flock *) arg);
		//按照指向结构体flock的指针的第三个参数arg所描述的锁的信息设置或者清除一个文件的segment锁。
		//F_SETLK被用来实现共享(或读)锁(F_RDLCK)或独占(写)锁(F_WRLCK)，同样可以去掉这两种锁(F_UNLCK)。
		//如果共享锁或独占锁不能被设置，fcntl()将立即返回EAGAIN 
		case F_SETLK:
			return fcntl_setlk(fd, cmd, (struct flock *) arg);
		//除了共享锁或独占锁被其他的锁阻塞这种情况外，这个命令和F_SETLK是一样的。如果共享锁或独占锁被其他的锁阻塞，
		//进程将等待直到这个请求能够完成。当fcntl()正在等待文件的某个区域的时候捕捉到一个信号，如果这个信号没有被
		//指定SA_RESTART, fcntl将被中断,当一个共享锁被set到一个文件的某段的时候，其他的进程可以set共享锁到这个段或
		//这个段的一部分。共享锁阻止任何其他进程set独占锁到这段保护区域的任何部分。如果文件描述符没有以读的访问方式
		//打开的话，共享锁的设置请求会失败。独占锁阻止任何其他的进程在这段保护区域任何位置设置共享锁或独占锁。
		//如果文件描述符不是以写的访问方式打开的话，独占锁的请求会失败
		case F_SETLKW:
			return fcntl_setlk(fd, cmd, (struct flock *) arg);
		// 取得当前正在接收SIGIO或者SIGURG信号的进程id或进程组id，进程组id返回的是负值(arg被忽略)  
		case F_GETOWN:
			/*
			 * XXX If f_owner is a process group, the
			 * negative return value will get converted
			 * into an error.  Oops.  If we keep the the
			 * current syscall conventions, the only way
			 * to fix this will be in libc.
			 */
			return filp->f_owner;
		//设置将接收SIGIO和SIGURG信号的进程id或进程组id，进程组id通过提供负值的arg来说明(arg绝对值的一个进程组ID)，
		//否则arg将被认为是进程id
		//参见本文件最下方关于异步通信的注释
		case F_SETOWN:
			/*
			 *	Add the security checks - AC. Without
			 *	this there is a massive Linux security
			 *	hole here - consider what happens if
			 *	you do something like
			 * 
			 *		fcntl(0,F_SETOWN,some_root_process);
			 *		getchar();
			 * 
			 *	and input a line!
			 * 
			 * BTW: Don't try this for fun. Several Unix
			 *	systems I tried this on fall for the
			 *	trick!
			 * 
			 * I had to fix this botch job as Linux
			 *	kill_fasync asserts priv making it a
			 *	free all user process killer!
			 *
			 * Changed to make the security checks more
			 * liberal.  -- TYT
			 */
			if (current->pgrp == -arg || current->pid == arg)
				goto fasync_ok;
			
			for_each_task(p) {
				if ((p->pid == arg) || (p->pid == -arg) || 
				    (p->pgrp == -arg)) {
					task_found++;
					if ((p->session != current->session) &&
					    (p->uid != current->uid) &&
					    (p->euid != current->euid) &&
					    !suser())
						return -EPERM;
					break;
				}
			}
			if ((task_found == 0) && !suser())
				return -EINVAL;
		fasync_ok:
			filp->f_owner = arg;
			if (S_ISSOCK (filp->f_inode->i_mode))
				sock_fcntl (filp, F_SETOWN, arg);
			return 0;
		default:
			/* sockets need a few special fcntls. */
			if (S_ISSOCK (filp->f_inode->i_mode))
			  {
			     return (sock_fcntl (filp, cmd, arg));
			  }
			return -EINVAL;
	}
}

//参见下方的注释
void kill_fasync(struct fasync_struct *fa, int sig)
{
	while (fa) {
		if (fa->magic != FASYNC_MAGIC) {
			printk("kill_fasync: bad magic number in "
			       "fasync_struct!\n");
			return;
		}
		if (fa->fa_file->f_owner > 0)
			kill_proc(fa->fa_file->f_owner, sig, 1);
		else
			kill_pg(-fa->fa_file->f_owner, sig, 1);
		fa = fa->fa_next;
	}
}
/*
			异步通知：

	使用异步通知机制可以提高查询设备的效率。通过使用异步通知，应用程序可以在数据可用时收到一个信号，而无需不停地轮询。
	设置异步通知的步骤（针对应用层来说的）：
	1.首先制定一个进程作为文件的属主。通过使用fcntl系统调用执行F_SETOWN命令时，属主进程的ID号就会保存在filp->f_owner中，
	目的是为了让内核知道应该通知哪个进程。
	2.在设备中设置FASYNC标志。通过fcntl调用的F_SETFL来完成。
	设置晚以上两步后，输入文件就可以在新数据到达时请求发送一个SIGIO信号，该信号被发送到存放在filp->f_owner中的进程。
	实例：启用stdin输入文件到当前进程的异步通知机制
	signal(SIGIO,&input_handler);
	fcntl(STDIN_FILENO,F_SETOWN,getpid());//设置STDIN_FILENO的属主为当前进程
	oflags=fcntl(STDIN_FILENO,F_GETFL);//获得STDIN_FILENO的描述符
	fcntl(STDIN_FILENO,F_SETFL,oflags|FASYNC);//从新设置描述符
	注意的问题：
	1.应用程序通常只假设套接字和终端具备异步通知功能。
	2.当进程受到SIGIO信号时，它并不知道哪个输入文件有了新的输入。如果有多于一个文件可以异步通知输入的进程，
	则应用程序必须借助于poll或select来确定输入的来源。
	驱动程序的实现：
	1、F_SETOWN被调用时对filp->f_owner赋值。
	2.在执行F_SETFL启用FASYNC时，调用驱动程序的fasync方法。只要filp->f_owner中的FASYNC标志发生了变化，
	就会调用该方法，以便把这个变化通知驱动程序，使其正确响应。文件打开时，默认fasync标志是被清除的。
	3.当数据到达时，所有注册为异步通知的进程都会收到一个SIGIO信号。
	linux 这种调用方法基于一个数据结构和两个函数，对于驱动开发而言主要关注两个函数，内核会自己维护该数据结构，
	为驱动程序服务（书上对该数据结构也并未给出多少解释）。包含在头文件<linux/fs.h>中。

	struct fasync_struct {
		int    magic;
		int    fa_fd;
		struct    fasync_struct    *fa_next; /* singly linked list */
	/*    struct    file         *fa_file;
	};

	/*
	两个函数如下：
	int fasnyc_helper(int fd,struct file *filp,int mode,struct fasync_struct **fa);	//此版本对应tty_fasync和sock_fasync函数
	void kill_fasync(struct fasync_struct **fa,int sig,int band);
	当一个打开的文件的FASYNC标志被修改时，调用fasync_helper以便从相关的进程列表中增加或删除文件。当数据到达时，
	可使用kill_fasync通知所有的相关进程。它的参数是要发送的信号（sig）和带宽（band）。
	注意的地方：1.对于struct fasync_struct这个数据结构在编写驱动时并不需要特别关注，它会由内核来维护，
	驱动程序中调用它即可。如同poll的底层实现上poll_table这个内存页链表也是由内核来维护，驱动程序使用它即可。
	2.对于用来通知可读的异步通知：band几乎总是为poll_in

	对于用来通知可写的异步通知：band几乎总是为poll_out

	3.wait_enevt_interruptible(dev->inq,(dev->rp != dev->wp));

	wake_up_interruptible(&dev->inq);

	在使进程休眠的时候使用的是值传递，但在唤醒进程的时候使用的指针传递。

	实现:

	static int scull_p_fasync(int fd,struct file *filp,int mode)

	{

	   struct scull_pipe *dev = filp->private;

	   fasync_helper(fd,filp,mod,&dev->async_queue);

	}
	接着当数据到达时，必须执行下面的语句来通知异步读取进程。由于提供scullpipe的
	读取进程的新数据是在某个进程调用wirte产生的，所以这条语句在scullpipe的write方法中实现：

	if(dev->async_queue)
      kill_fasync(&dev->async_queue,SIGIO,POLL_IN);

	最后注意在文件关闭之前，必须调用fasync方法，以便从活动的异步读取进程列表中删除文件。
	scull_p_fasync(-1,filp,0);
*/
/*
	linux中驱动异步通知应用程序的方法
	驱动程序运行在内核空间中，应用程序运行在用户空间中，两者是不能直接通信的。

但在实际应用中，在设备已经准备好的时候，我们希望通知用户程序设备已经ok，

用户程序可以读取了，这样应用程序就不需要一直查询该设备的状态，从而节约了资源，这就是异步通知。
好，那下一个问题就来了，这个过程如何实现呢？简单，两方面的工作。
一 驱动方面：
1. 在设备抽象的数据结构中增加一个struct fasync_struct的指针
2. 实现设备操作中的fasync函数，这个函数很简单，其主体就是调用内核的fasync_helper函数。
3. 在需要向用户空间通知的地方(例如中断中)调用内核的kill_fasync函数。
4. 在驱动的release方法中调用前面定义的fasync函数
呵呵，简单吧，就三点。其中fasync_helper和kill_fasync都是内核函数，我们只需要调用就可以了。在

1中定义的指针是一个重要参数，fasync_helper和kill_fasync会使用这个参数。
二 应用层方面
1. 利用signal或者sigaction设置SIGIO信号的处理函数
2. fcntl的F_SETOWN指令设置当前进程为设备文件owner
3. fcntl的F_SETFL指令设置FASYNC标志
完成了以上的工作的话，当内核执行到kill_fasync函数，用户空间SIGIO函数的处理函数就会被调用了。
呵呵，看起来不是很复杂把，让我们结合具体代码看看就更明白了。
先从应用层代码开始吧：
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#define MAX_LEN 100
void input_handler(int num)
//处理函数，没什么好讲的，用户自己定义
{
　char data[MAX_LEN];
　int len; 
　//读取并输出STDIN_FILENO上的输入
　len = read(STDIN_FILENO, &data, MAX_LEN);
　data[len] = 0;
　printf("input available:%s/n", data);
}
 
main()
{
　int oflags;
 
　//启动信号驱动机制
　signal(SIGIO, input_handler);
  /*
  将SIGIO信号同input_handler函数关联起来，
  一旦产生SIGIO信号,就会执行input_handler，
   */
　fcntl(STDIN_FILENO, F_SETOWN, getpid());
  /*
    STDIN_FILENO是打开的设备文件描述符,
    F_SETOWN用来决定操作是干什么的, 
    getpid()是个系统调用，功能是返回当前进程的进程号
    整个函数的功能是STDIN_FILENO设置这个设备文件的拥有者为当前进程。
  */
　oflags = fcntl(STDIN_FILENO, F_GETFL);
  /*得到打开文件描述符的状态*/
　fcntl(STDIN_FILENO, F_SETFL, oflags | FASYNC);
  /*
   设置文件描述符的状态为oflags | FASYNC属性,
   一旦文件描述符被设置成具有FASYNC属性的状态，
   也就是将设备文件切换到异步操作模式。
   这时系统就会自动调用驱动程序的fasync方法。
  */
 
　//最后进入一个死循环，程序什么都不干了，只有信号能激发input_handler的运行
　//如果程序中没有这个死循环，会立即执行完毕
　while (1);
}
再看驱动层代码，驱动层其他部分代码不变，就是增加了一个fasync方法的实现以及一些改动
static struct fasync_struct *fasync_queue;
/*首先是定义一个结构体，其实这个结构体存放的是一个列表，这个列表保存的是
  一系列设备文件，SIGIO信号就发送到这些设备上*/
static int my_fasync(int fd, struct file * filp, int on) 
/*fasync方法的实现*/
{
    int retval;
    retval=fasync_helper(fd,filp,on,&fasync_queue);
    /*将该设备登记到fasync_queue队列中去*/
    if(retval<0)
      return retval;
    return 0;
}
在驱动的release方法中我们再调用my_fasync方法
int my_release(struct inode *inode, struct file *filp)
{
 /*..processing..*/
        drm_fasync(-1, filp, 0);
        /*..processing..*/
}
 /*
这样后我们在需要的地方（比如中断）调用下面的代码,就会向fasync_queue队列里的设备发送SIGIO信号
，应用程序收到信号，执行处理程序
    if (fasync_queue)
      kill_fasync(&fasync_queue, SIGIO, POLL_IN);
好了，这下大家知道该怎么用异步通知机制了吧？
 
以下是几点说明[1]： 
1 两个函数的原型
int fasync_helper(struct inode *inode, struct file *filp, int mode, struct fasync_struct **fa); 
一个"帮忙者", 来实现 fasync 设备方法. mode 参数是传递给方法的相同的值, 而 fa 指针指向一个设
备特定的 fasync_struct *
 
void kill_fasync(struct fasync_struct *fa, int sig, int band); 
如果这个驱动支持异步通知, 这个函数可用来发送一个信号到登记在 fa 中的进程.
 
2. 
fasync_helper 用来向等待异步信号的设备链表中添加或者删除设备文件,   kill_fasync
被用来通知拥有相关设备的进程. 它的参数是被传递的信号(常常是 SIGIO)和 band, 这几乎都是 POLL_IN[25](但
是这可用来发送"紧急"或者带外数据, 在网络代码里).
 
*/