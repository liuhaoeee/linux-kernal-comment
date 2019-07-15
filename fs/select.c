/*
 * This file contains the procedures for the handling of select
 *
 * Created for Linux based loosely upon Mathius Lattner's minix
 * patches by Peter MacDonald. Heavily edited by Linus.
 *
 *  4 February 1994
 *     COFF/ELF binary emulation. If the process has the STICKY_TIMEOUTS
 *     flag set in its personality we do *not* modify the given timeout
 *     parameter to reflect time remaining.
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
	select系统调用是用来让我们的程序监视多个文件描述符(file descrīptor)的状态变化的。
	程序会停在select这里等待，直到被监视的文件描述符有某一个或多个发生了状态改变。
	select()的机制中提供一fd_set的数据结构，实际上是一long类型的数组， 每一个数组元素
	都能与一打开的文件描述符（不管是Socket描述符,还是其他 文件或命名管道或设备描述符）
	建立联系，建立联系的工作由程序员完成， 当调用select()时，由内核根据IO状态修改fd_set
	的内容，由此来通知执 行了select()的进程哪一Socket或文件可读
*/
#include <linux/types.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/personality.h>
#include <linux/mm.h>

#include <asm/segment.h>
#include <asm/system.h>

#define ROUND_UP(x,y) (((x)+(y)-1)/(y))

/*
 * Ok, Peter made a complicated, but straightforward multiple_wait() function.
 * I have rewritten this, taking some shortcuts: This code may not be easy to
 * follow, but it should be free of race-conditions, and it's practical. If you
 * understand what I'm doing here, then you understand how the linux
 * sleep/wakeup mechanism works.
 *
 * Two very simple procedures, select_wait() and free_wait() make all the work.
 * select_wait() is a inline-function defined in <linux/sched.h>, as all select
 * functions have to call it to add an entry to the select table.
 */

/*
 * I rewrote this again to make the select_table size variable, take some
 * more shortcuts, improve responsiveness, and remove another race that
 * Linus noticed.  -- jrs
 */

static void free_wait(select_table * p)
{
	struct select_table_entry * entry = p->entry + p->nr;

	while (p->nr > 0) {
		p->nr--;
		entry--;
		remove_wait_queue(entry->wait_address,&entry->wait);
	}
}

/*
 * The check function checks the ready status of a file using the vfs layer.
 *
 * If the file was not ready we were added to its wait queue.  But in
 * case it became ready just after the check and just before it called
 * select_wait, we call it again, knowing we are already on its
 * wait queue this time.  The second call is not necessary if the
 * select_table is NULL indicating an earlier file check was ready
 * and we aren't going to sleep on the select_table.  -- jrs
 */

 //select()函数是难以跟踪的，因为当前版本中的大多文件系统i节点并没有相应的
 //select()方法，只靠跟踪VFS层的的select()函数是难以理解其机制的
 //但终于找到一个跟踪路径：fs/pipe.c中管道的i节点实现了其select()方法
 //还有一个实现是网络部分，我想这并不是偶然的
static int check(int flag, select_table * wait, struct file * file)
{
	struct inode * inode;
	struct file_operations *fops;
	int (*select) (struct inode *, struct file *, int, select_table *);

	inode = file->f_inode;
	if ((fops = file->f_op) && (select = fops->select))
		return select(inode, file, flag, wait)
		    || (wait && select(inode, file, flag, NULL));
	if (flag != SEL_EX)
		return 1;
	return 0;
}
/*
	int select(nfds, readfds, writefds, exceptfds, timeout) 
	int nfds; 
	fd_set *readfds, *writefds, *exceptfds; 
	struct timeval *timeout; 

	ndfs：select监视的文件句柄数，视进程中打开的文件数而定,一般设为呢要监视各文件 
	中的最大文件号加一。 
	readfds：select监视的可读文件句柄集合。 
	writefds: select监视的可写文件句柄集合。 
	exceptfds：select监视的异常文件句柄集合。 
	timeout：本次select()的超时结束时间。（见/usr/sys/select.h， 
	可精确至百万分之一秒！） 

	当readfds或writefds中映象的文件可读或可写或超时，本次select() 
	就结束返回。程序员利用一组系统提供的宏在select()结束时便可判 
	断哪一文件可读或可写。对Socket编程特别有用的就是readfds。 
	几只相关的宏解释如下： 

	FD_ZERO(fd_set *fdset)：清空fdset与所有文件句柄的联系。 
	FD_SET(int fd, fd_set *fdset)：建立文件句柄fd与fdset的联系。 
	FD_CLR(int fd, fd_set *fdset)：清除文件句柄fd与fdset的联系。 
	FD_ISSET(int fd, fdset *fdset)：检查fdset联系的文件句柄fd是否 
	可读写，>0表示可读写。
*/
static int do_select(int n, fd_set *in, fd_set *out, fd_set *ex,
	fd_set *res_in, fd_set *res_out, fd_set *res_ex)
{
	int count;
	select_table wait_table, *wait;
	struct select_table_entry *entry;
	unsigned long set;
	int i,j;
	int max = -1;

	for (j = 0 ; j < __FDSET_LONGS ; j++) {
		i = j << 5;
		if (i >= n)
			break;
		//可以得到一个所有要求要监视的文件句柄的集合
		set = in->fds_bits[j] | out->fds_bits[j] | ex->fds_bits[j];
		//检验这些要求监视的文件是否已经被当前进程打开或文件是否存在
		for ( ; set ; i++,set >>= 1) {
			if (i >= n)
				goto end_check;
			//若此位不为1，表示不要求监视，文件存不存在无所谓，跳过
			if (!(set & 1))
				continue;
			//当前进程是否已经打开此文件
			if (!current->files->fd[i])
				return -EBADF;
			//此文件对应的i节点是否存在
			if (!current->files->fd[i]->f_inode)
				return -EBADF;
			max = i;
		}
	}
end_check:
	//内核对参数n的一个"修复"，或许可以提高效率
	n = max + 1;
	if(!(entry = (struct select_table_entry*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	FD_ZERO(res_in);
	FD_ZERO(res_out);
	FD_ZERO(res_ex);
	count = 0;
	wait_table.nr = 0;
	wait_table.entry = entry;
	wait = &wait_table;
repeat:
	//现将当前进程状态设置为TASK_INTERRUPTIBLE（可中断睡眠状态）
	//但由于内核还没有调用调度程序 所以不会引起进程切换
	//但是若发生中断 中断返回会发生进程调度吗？？？应该不会
	current->state = TASK_INTERRUPTIBLE;
	//循环检测每一个要求检测的文件，假设两种情况（可跟踪fs/pipe.c文件中的相关函数验证）：
	/*
		A.要求检测的所有文件，都没有准备好相应的状态
		  这种情况下，直到for循环结束时wait都不为NULL
		  并且check()函数内部会调用sche.h文件中定义的select_wait()函数
		  将当前进程睡眠在当前检测的“文件”睡眠队列中
		  然后，check()返回0（也正因为如此，wait始终不为NULL）
		  其中每次添加进相关“文件”睡眠队列中的wait节点来自于wait_table锁代表的一页物理内存
		  这种情况其实是为当前进程分配许多的wait节点，然后将其睡眠在每个相关的检测“文件”睡眠队列中
		  最后，for循环结束，但由于没有文件的状态符合要求，所以count为0
		  之后会发生进程调度，由于上面已经将进程设置为TASK_INTERRUPTIBLE
		  所以进程睡眠，直到相关的文件中有一个文件状态发生变化时，唤醒本进程
		  于是，进程又返回for循环中，但由于并没有将唤醒进程的文件的睡眠队列中的相关wait节点移除队列
		  所以，如果依然没有文件状态符合要求，进程又会去睡眠在每个要求监测的文件上
		  这里还有一个问题就是，select_table代表的一页物理内存可以最大限度的容纳所有可能要监测的文件的wait节点吗？
		  我想，答案应该是肯定的
		B.要求监测的文件中，至少有一个文件的状态符合条件
		  此时，check()将会返回1，并且check()函数内部不会调用到相关文件i节点select()函数中的的select_wait()函数，
		  故进程不会睡眠在此符合条件的文件睡眠队列中了
		  而且会将wait设置为NULL，而且将会在之后的循环中一直保持NULL
		  因为文件集中有至少一个文件的状态符合要求了，进程就没必要睡眠等待了
		  而且结束for循环后，此函数可能会终止
		  
		  以上的分析基于对pipe.c文件相关函数的分析
	*/
	for (i = 0 ; i < n ; i++) {
		//FD_ISSET():检查fdset联系的文件句柄fd是否可读写，>0表示可读写。 
		if (FD_ISSET(i,in) && check(SEL_IN,wait,current->files->fd[i])) {	
			FD_SET(i, res_in);
			count++;
			wait = NULL;
		}
		if (FD_ISSET(i,out) && check(SEL_OUT,wait,current->files->fd[i])) {
			FD_SET(i, res_out);
			count++;
			wait = NULL;
		}
		if (FD_ISSET(i,ex) && check(SEL_EX,wait,current->files->fd[i])) {
			FD_SET(i, res_ex);
			count++;
			wait = NULL;
		}
	}
	wait = NULL;
	//如果没有找到符合要求的文件并且设置了超时值并且没有收到信号，则调度其他程序运行
	//如果没有设置超时值，则立即返回，不阻塞
	if (!count && current->timeout && !(current->signal & ~current->blocked)) {
		schedule();
		goto repeat;
	}
	//将wait从各个睡眠队列中移除
	free_wait(&wait_table);
	//释放wait_table占用的物理内存
	free_page((unsigned long) entry);
	current->state = TASK_RUNNING;
	return count;
}

/*
 * We do a VERIFY_WRITE here even though we are only reading this time:
 * we'll write to it eventually..
 */
 
 //将fs_pointer指向的文件描述符集中的内容复制到fdset指向的文件描述符集中
static int __get_fd_set(int nr, unsigned long * fs_pointer, unsigned long * fdset)
{
	int error;

	FD_ZERO(fdset);
	if (!fs_pointer)
		return 0;
	error = verify_area(VERIFY_WRITE,fs_pointer,sizeof(fd_set));
	if (error)
		return error;
	while (nr > 0) {
		*fdset = get_fs_long(fs_pointer);
		fdset++;
		fs_pointer++;
		nr -= 32;
	}
	return 0;
}

//将fs_pointer指向的文件描述符集中的内容复制到fdset指向的文件描述符集中
static void __set_fd_set(int nr, unsigned long * fs_pointer, unsigned long * fdset)
{
	if (!fs_pointer)
		return;
	while (nr > 0) {
		put_fs_long(*fdset, fs_pointer);
		fdset++;
		fs_pointer++;
		nr -= 32;
	}
}

#define get_fd_set(nr,fsp,fdp) \
__get_fd_set(nr, (unsigned long *) (fsp), (unsigned long *) (fdp))

#define set_fd_set(nr,fsp,fdp) \
__set_fd_set(nr, (unsigned long *) (fsp), (unsigned long *) (fdp))

 /*
	Select在Socket编程中还是比较重要的，可是对于初学Socket的人来说都不太爱用Select写程序，
	他们只是习惯写诸如connect、accept、recv或recvfrom这样的阻塞程序（所谓阻塞方式block，
	顾名思义，就是进程或是线程执行到这些函数时必须等待某个事件的发生，如果事件没有发生，
	进程或线程就被阻塞，函数不能立即返回）。可是使用Select就可以完成非阻塞（所谓非阻塞方
	式non-block，就是进程或线程执行此函数时不必非要等待事件的发生，一旦执行肯定返回，以返
	回值的不同来反映函数的执行情况，如果事件发生则与阻塞方式相同，若事件没有发生则返回一个
	代码来告知事件未发生，而进程或线程继续执行，所以效率较高）方式工作的程序，它能够监视我
	们需要监视的文件描述符的变化情况——读写或是异常。下面详细介绍一下！ 

	Select的函数格式(我所说的是Unix系统下的伯克利socket编程，和windows下的有区别，一会儿说明)： 
	int select(int maxfdp,fd_set *readfds,fd_set *writefds,fd_set *errorfds,struct timeval *timeout); 
	先说明两个结构体：
	第一，struct fd_set可以理解为一个集合，这个集合中存放的是文件描述符(file descriptor)，即文件句柄，
	这可以是我们所说的普通意义的文件，当然Unix下任何设备、管道、FIFO等都是文件形式，全部包括在内，所以
	毫无疑问一个socket就是一个文件，socket句柄就是一个文件描述符。fd_set集合可以通过一些宏由人为来操作，
	比如清空集合FD_ZERO(fd_set *)，将一个给定的文件描述符加入集合之中FD_SET(int ,fd_set *)，将一个给定
	的文件描述符从集合中删除FD_CLR(int ,fd_set*)，检查集合中指定的文件描述符是否可以读写FD_ISSET(int ,fd_set* )。
	一会儿举例说明。 

	第二，struct timeval是一个大家常用的结构，用来代表时间值，有两个成员，一个是秒数，另一个是毫秒数。 

	具体解释select的参数： 

	int maxfdp是一个整数值，是指集合中所有文件描述符的范围，即所有文件描述符的最大值加1，不能错！
	在Windows中这个参数的值无所谓，可以设置不正确。 

	fd_set *readfds是指向fd_set结构的指针，这个集合中应该包括文件描述符，我们是要监视这些文件描
	述符的读变化的，即我们关心是否可以从这些文件中读取数据了，如果这个集合中有一个文件可读，select
	就会返回一个大于0的值，表示有文件可读，如果没有可读的文件，则根据timeout参数再判断是否超时，
	若超出timeout的时间，select返回0，若发生错误返回负值。可以传入NULL值，表示不关心任何文件的读变化。 

	fd_set *writefds是指向fd_set结构的指针，这个集合中应该包括文件描述符，我们是要监视这些文件描述符的
	写变化的，即我们关心是否可以向这些文件中写入数据了，如果这个集合中有一个文件可写，select就会返回一个
	大于0的值，表示有文件可写，如果没有可写的文件，则根据timeout参数再判断是否超时，若超出timeout的时间
	，select返回0，若发生错误返回负值。可以传入NULL值，表示不关心任何文件的写变化。 

	fd_set *errorfds同上面两个参数的意图，用来监视文件错误异常。 

	struct timeval* timeout是select的超时时间，这个参数至关重要，它可以使select处于三种状态，第一，
	若将NULL以形参传入，即不传入时间结构，就是将select置于阻塞状态，一定等到监视文件描述符集合中某个
	文件描述符发生变化为止；第二，若将时间值设为0秒0毫秒，就变成一个纯粹的非阻塞函数，不管文件描述符
	是否有变化，都立刻返回继续执行，文件无变化返回0，有变化返回一个正值；第三，timeout的值大于0，这就
	是等待的超时时间，即select在timeout时间内阻塞，超时时间之内有事件到来就返回了，否则在超时后不管怎
	样一定返回，返回值同上述。 

 */
 /*
 * We can actually return ERESTARTSYS instead of EINTR, but I'd
 * like to be certain this leads to no problems. So I return
 * EINTR just for safety.
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
//select()系统调用可以允许调用者通知预期的read、write系统调用是否会阻塞
asmlinkage int sys_select( unsigned long *buffer )
{
/* Perform the select(nd, in, out, ex, tv) system call. */
	int i;
	fd_set res_in, in, *inp;
	fd_set res_out, out, *outp;
	fd_set res_ex, ex, *exp;
	int n;
	struct timeval *tvp;
	unsigned long timeout;

	i = verify_area(VERIFY_READ, buffer, 20);
	if (i)
		return i;
	//获取要监视的文件中文件句柄最大值+1
	n = get_fs_long(buffer++);
	if (n < 0)
		return -EINVAL;
	if (n > NR_OPEN)
		n = NR_OPEN;
	//inp指向要监视读变化的文件描述符集合(从用户空间复制文件描述符的指针到内核空间)
	inp = (fd_set *) get_fs_long(buffer++);	
	//inp指向要监视写变化的文件描述符集合(从用户空间复制文件描述符的指针到内核空间)
	outp = (fd_set *) get_fs_long(buffer++);
	//inp指向要监视异常变化的文件描述符集合(从用户空间复制文件描述符的指针到内核空间)
	exp = (fd_set *) get_fs_long(buffer++);
	//tvp指向超时时间(从用户空间复制文件描述符的指针到内核空间)
	tvp = (struct timeval *) get_fs_long(buffer);
	//将文件描述符从用户空间复制到内核空间变量inp、outp、exp中
	if ((i = get_fd_set(n, inp, &in)) ||
	    (i = get_fd_set(n, outp, &out)) ||
	    (i = get_fd_set(n, exp, &ex))) return i;	//如果出错，返回
	timeout = ~0UL;
	if (tvp) {
		i = verify_area(VERIFY_WRITE, tvp, sizeof(*tvp));
		if (i)
			return i;
		timeout = ROUND_UP(get_fs_long((unsigned long *)&tvp->tv_usec),(1000000/HZ));
		timeout += get_fs_long((unsigned long *)&tvp->tv_sec) * HZ;
		//如果设置了超时值
		if (timeout)
			timeout += jiffies + 1;
	}
	current->timeout = timeout;
	i = do_select(n, &in, &out, &ex, &res_in, &res_out, &res_ex);
	//如果超时了
	if (current->timeout > jiffies)
		timeout = current->timeout - jiffies;
	else
		timeout = 0;
	current->timeout = 0;
	if (tvp && !(current->personality & STICKY_TIMEOUTS)) {
		put_fs_long(timeout/HZ, (unsigned long *) &tvp->tv_sec);
		timeout %= HZ;
		timeout *= (1000000/HZ);
		put_fs_long(timeout, (unsigned long *) &tvp->tv_usec);
	}
	if (i < 0)
		return i;
	//如果没有找到符合条件的文件，但是收到了信号 则返回-ERESTARTNOHAND
	if (!i && (current->signal & ~current->blocked))
		return -ERESTARTNOHAND;
	//将结果文件描述符集复制到用户空间中
	set_fd_set(n, inp, &res_in);
	set_fd_set(n, outp, &res_out);
	set_fd_set(n, exp, &res_ex);
	return i;
}
