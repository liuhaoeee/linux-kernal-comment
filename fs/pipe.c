/*
 *  linux/fs/pipe.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

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

 
#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/termios.h>
#include <linux/mm.h>


/* We don't use the head/tail construction any more. Now we use the start/len*/
/* construction providing full use of PIPE_BUF (multiple of PAGE_SIZE) */
/* Florian Coosmann (FGC)                                ^ current = 1       */
/* Additionally, we now use locking technique. This prevents race condition  */
/* in case of paging and multiple read/write on the same pipe. (FGC)         */

//读管道
static int pipe_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	int chars = 0, size = 0, read = 0;
    char *pipebuf;

	//如果设置O_NONBLOCK
	if (filp->f_flags & O_NONBLOCK) {
		if (PIPE_LOCK(*inode))	//如果管道锁住了，则返回EAGAIN
			return -EAGAIN;
		if (PIPE_EMPTY(*inode))	//如果管道数据为空
			if (PIPE_WRITERS(*inode))	//如果有写进程，则返回EAGAIN，否则返回0
				return -EAGAIN;
			else
				return 0;
	//没有设置O_NONBLOCK，则可能会阻塞，使用while循环等待条件满足，并在循环中处理可能出现的中断信号
	} else while (PIPE_EMPTY(*inode) || PIPE_LOCK(*inode)) {
		//如果管道是空的，没有数据
		if (PIPE_EMPTY(*inode)) {
			//如果没有写管道
			if (!PIPE_WRITERS(*inode))
				return 0;
		}
		/*正常执行的系统调用是不会被信号中断的，但某些系统调用可能需要很长的等待时间来完成，对于这种情况，
		*允许信号中断它的执行提供了更好的程序控制能力。要达到允许被信号中断的功能，系统调用的实现上需要满足如下条件： 
		*系统调用必须在需要等待的时候将进程转入睡眠状态，主动让出CPU。因为作为内核态的程序，系统调用的执行是不可抢占的，
		*不主动放弃CPU的系统调用会一直运行直到结束。而且当前进程的睡眠状态必须设置为TASK_INTERRUPTABLE，只有在这个状态下，
		*当信号被发送给进程时，进程的状态被（信号发送函数）唤醒，并重新在运行队列中排队等待调度。 
		*当进程获得了一个CPU时间片后，它接着睡眠时的下一条指令开始运行（还在系统调用内），系统调用判断出收到了信号，
		*会设置一个与信号有关的退出标志并迅速结束。退出标志为：ERESTARTNOHAND，ERESTARTSYS，ERESTARTNOINTR中的一种，
		*这些标志都是和接收信号程序do_signal通信用的，它们和进程对特定信号的处理标志一起，决定了系统调用中断后是否重新执行。 
		*ERESTARTNOINTR：要求系统调用被无条件重新执行。 
		*ERESTARTSYS： 要求系统调用重新执行，但允许信号处理程序根据自己的标志来做选择。 
		*ERESTARTNOHAND：在信号没有设置自己的信号处理函数时系统调用重新执行。 
		*/	
		if (current->signal & ~current->blocked)
			return -ERESTARTSYS;
		interruptible_sleep_on(&PIPE_WAIT(*inode));
	}
	//如果执行到这里，说明已经满足了read所需要的条件
	PIPE_LOCK(*inode)++;	//加锁 在这里可以保证操作的原子性吗？？？而且，加锁和解锁有必要吗？内核是不可抢占的啊
							//而且在加锁和解锁之间，没有导致进程睡眠的条件啊
							//OK,I get it!It is because of the paging!
							//虽然内核程序代码常驻内存，但是本函数中的buf来自用户空间，没有保证其在内存中，所以
							//有可能产生缺页中断 导致进程睡眠 嗯 明白了这一点很重要
							//有点问题，buf貌似在上层内核函数中已经经过验证了，以保证其不会产生缺页中断 
							//just in case？
							//或许是因为本函数可能调用interruptible_sleep_on()而睡眠的过程中，将buf交换到了磁盘上
							//所以在while循环中，可能会产生缺页中断 虽然一般中断并不会引起进程睡眠和切换
							//但缺页中断不一样 缺页中断是和当前进程相关的 是由当前进程引起的 缺页中断也是为当前进程
							//服务的 所以在缺页中断中 进程可以睡眠从而调度其他程序执行 直到请求的页可用 参见《深入理解Linux内核》中
							//有关中断的讲解
							//一般中断并不会引起进程睡眠和切换 中断返回后 继续执行被中断的进程或内核过程
							//又由于这是在内核中，内核是不可抢占的 所以应该可以保证锁的原子性
	//开始读取数据
	while (count>0 && (size = PIPE_SIZE(*inode))) {	//size = PIPE_LEN(inode) 管道中有效数据长度
		chars = PIPE_MAX_RCHUNK(*inode);	//#define PIPE_MAX_RCHUNK(inode)	(PIPE_BUF - PIPE_START(inode))
		//调整本次要读取的字节数
		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;
		read += chars;	//增加已读字节数
        pipebuf = PIPE_BASE(*inode)+PIPE_START(*inode);
		PIPE_START(*inode) += chars;	//重新调整管道中有效数据开始的位置
		PIPE_START(*inode) &= (PIPE_BUF-1);	//对管道缓冲长度取模，以形成环形管道
		PIPE_LEN(*inode) -= chars;	//重新调整管道的有效数据长度
		count -= chars;	//调整要读的字节数
		memcpy_tofs(buf, pipebuf, chars );	//将当前读取的数据复制到buf中去
											//由于buf可能产生缺页中断 所以进程可能睡眠 从而调度其他程序的执行
											//但由于管道已经上锁 所以可以避免竞争条件 而且不会有两个进程进入到此临界区
		buf += chars;	//重新调整buf下次开始接受数据的位置
	} 
	//执行到这里，说明已经读取完数据，或者管道没有有效数据
	PIPE_LOCK(*inode)--;	//对管道解锁
	//唤醒等待在此管道上的其他进程
	wake_up_interruptible(&PIPE_WAIT(*inode));
	//如果读取了数据
	if (read)
		return read;
	//执行到这里，read=0，如果没有写进程，返回-EAGAIN
	if (PIPE_WRITERS(*inode))
		return -EAGAIN;
	return 0;
}

//写管道
static int pipe_write(struct inode * inode, struct file * filp, char * buf, int count)
{
	int chars = 0, free = 0, written = 0;
	char *pipebuf;

	//如果当前没有读进程
	if (!PIPE_READERS(*inode)) { /* no readers */
		//向本进程发送SIGPIPE信号
		//系统调用返回用户空间时，处理信号
		send_sig(SIGPIPE,current,0);
		return -EPIPE;
	}
/* if count <= PIPE_BUF, we have to make it atomic */
	//如果要写入管道的字节数小于管道长度，我们可以一次性写入管道
	if (count <= PIPE_BUF)
		free = count;
	//否则，管道当前空闲区域容不下我们要写入的数据
	//我们等待管道有空闲的时候再写入
	else
		free = 1; /* can't do it atomically, wait for any free space */
	//大循环 写入要写的字节数
	while (count>0) {
		//当管道中空闲字节数小于free或者管道被锁
		//当第一次循环时，若free==0，即代表着管道当前的可用空闲字节数可以容纳要写入的字节数
		//则while循环条件中的第一个条件不可能为真，若此时管道未上锁，则可进入真正的写循环
		//第一次循环时，若free==1，即代表着当前管道的可用空闲字节数不足以容纳要写入的字节数
		//需要所次写入、等待管道有空闲空间、再写入、再等待管道有空闲空间。。。。。。
		//此后的while循环判断中free将一直是1，表示只要管道空闲空间字节数小于1，就循环等待
		while ((PIPE_FREE(*inode) < free) || PIPE_LOCK(*inode)) {
			//如果当前没有读进程
			if (!PIPE_READERS(*inode)) { /* no readers */
				send_sig(SIGPIPE,current,0);
				return written? :-EPIPE;
			}
			if (current->signal & ~current->blocked)
				return written? :-ERESTARTSYS;
			if (filp->f_flags & O_NONBLOCK)
				return written? :-EAGAIN;
			interruptible_sleep_on(&PIPE_WAIT(*inode));
		}
		PIPE_LOCK(*inode)++;	//对此管道上锁
		//此循环会将数据写入管道当前空闲的区域中去
		//直到要写的字节数全部写完或者管道没有空闲字节数为止
		//注意，管道是一个环形缓冲区，写到管道末尾了，会循环到管道头开始写
		while (count>0 && (free = PIPE_FREE(*inode))) {	
			chars = PIPE_MAX_WCHUNK(*inode);	//(PIPE_BUF - PIPE_END(inode))
			if (chars > count)
				chars = count;
			if (chars > free)
				chars = free;
            pipebuf = PIPE_BASE(*inode)+PIPE_END(*inode);
			written += chars;	//增加写入的字节数
			PIPE_LEN(*inode) += chars;	//增加管道中的有效数据
			count -= chars;	//减少还要写入的字节数
			memcpy_fromfs(pipebuf, buf, chars );	//将chars个字节数写入到pipebuf中 可能导致缺页中断 进程睡眠 因此加锁有必要
			buf += chars;	//调整下次要写的位置
		}
		PIPE_LOCK(*inode)--;	//解锁
		//唤醒等待此管道的进程
		wake_up_interruptible(&PIPE_WAIT(*inode));
		free = 1;
	}
	return written;
}

static int pipe_lseek(struct inode * inode, struct file * file, off_t offset, int orig)
{
	return -ESPIPE;
}

static int pipe_readdir(struct inode * inode, struct file * file, struct dirent * de, int count)
{
	return -ENOTDIR;
}

static int bad_pipe_rw(struct inode * inode, struct file * filp, char * buf, int count)
{
	return -EBADF;
}

static int pipe_ioctl(struct inode *pino, struct file * filp,
	unsigned int cmd, unsigned long arg)
{
	int error;

	switch (cmd) {
		//得到管道缓冲区里有多少字节可以被读取，然后将字节数放入arg里面
		case FIONREAD:
			error = verify_area(VERIFY_WRITE, (void *) arg,4);
			if (!error)
				put_fs_long(PIPE_SIZE(*pino),(unsigned long *) arg);
			return error;
		default:
			return -EINVAL;
	}
}
//SEL_IN read
//SEL_OUT write
//SEL_EX exception
static int pipe_select(struct inode * inode, struct file * filp, int sel_type, select_table * wait)
{
	switch (sel_type) {
		case SEL_IN:
			//如果管道不为空（说明有数据可读）
			//或者管道为空，并且没有写进程
			if (!PIPE_EMPTY(*inode) || !PIPE_WRITERS(*inode))
				return 1;	//返回1表示文件可读
			//执行到这里，说明管道为空，并且有写进程
			//等待
			select_wait(&PIPE_WAIT(*inode), wait);
			return 0;	//返回1表示文件不可读
		case SEL_OUT:	
			//如果管道没有满（说明可以写入数据）
			//或者管道满了，但没有读进程
			if (!PIPE_FULL(*inode) || !PIPE_READERS(*inode))
				return 1;	//返回1表示文件可写
			//执行到这里，说明管道满，并且有读进程
			select_wait(&PIPE_WAIT(*inode), wait);
			return 0;	//返回1表示文件不可写
		case SEL_EX:
			//如果没有读进程
			//或者有读进程，但是没有写进程
			if (!PIPE_READERS(*inode) || !PIPE_WRITERS(*inode))
				return 1;
			select_wait(&inode->i_wait,wait);
			return 0;
	}
	return 0;
}

/*
 * Arggh. Why does SunOS have to have different select() behaviour
 * for pipes and fifos? Hate-Hate-Hate. See difference in SEL_IN..
 */
static int fifo_select(struct inode * inode, struct file * filp, int sel_type, select_table * wait)
{
	switch (sel_type) {
		case SEL_IN:
			if (!PIPE_EMPTY(*inode))
				return 1;
			select_wait(&PIPE_WAIT(*inode), wait);
			return 0;
		case SEL_OUT:
			if (!PIPE_FULL(*inode) || !PIPE_READERS(*inode))
				return 1;
			select_wait(&PIPE_WAIT(*inode), wait);
			return 0;
		case SEL_EX:
			if (!PIPE_READERS(*inode) || !PIPE_WRITERS(*inode))
				return 1;
			select_wait(&inode->i_wait,wait);
			return 0;
	}
	return 0;
}

/*
 * The 'connect_xxx()' functions are needed for named pipes when
 * the open() code hasn't guaranteed a connection (O_NONBLOCK),
 * and we need to act differently until we do get a writer..
 */
 //connect_xxx()函数用于当命名管道以非阻塞方式的读方式打开时，由于其并不保证其真的可读
 //所以要用connect_xxx()函数来进行之后的读处理，直到产生一个写进程为止
static int connect_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	//当管道中没有有效数据时
	while (!PIPE_SIZE(*inode)) {
		//如果有写进程，则break
		if (PIPE_WRITERS(*inode))
			break;
		//如果文件有O_NONBLOCK标志，则返回-EAGAIN
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		//唤醒睡眠在此管道上的进程
		wake_up_interruptible(& PIPE_WAIT(*inode));
		if (current->signal & ~current->blocked)
			return -ERESTARTSYS;
		//将本进程睡眠在此管道睡眠队列中
		interruptible_sleep_on(& PIPE_WAIT(*inode));
	}
	//执行到这里，说明管道中又有效数据了
	filp->f_op = &read_fifo_fops;
	return pipe_read(inode,filp,buf,count);
}

static int connect_select(struct inode * inode, struct file * filp, int sel_type, select_table * wait)
{
	switch (sel_type) {
		case SEL_IN:
			if (!PIPE_EMPTY(*inode)) {
				filp->f_op = &read_fifo_fops;
				return 1;
			}
			select_wait(&PIPE_WAIT(*inode), wait);
			return 0;
		case SEL_OUT:
			if (!PIPE_FULL(*inode))
				return 1;
			select_wait(&PIPE_WAIT(*inode), wait);
			return 0;
		case SEL_EX:
			if (!PIPE_READERS(*inode) || !PIPE_WRITERS(*inode))
				return 1;
			select_wait(&inode->i_wait,wait);
			return 0;
	}
	return 0;
}

/*
 * Ok, these three routines NOW keep track of readers/writers,
 * Linus previously did it with inode->i_count checking.
 */
static void pipe_read_release(struct inode * inode, struct file * filp)
{
	PIPE_READERS(*inode)--;
	wake_up_interruptible(&PIPE_WAIT(*inode));
}

static void pipe_write_release(struct inode * inode, struct file * filp)
{
	PIPE_WRITERS(*inode)--;
	wake_up_interruptible(&PIPE_WAIT(*inode));
}

static void pipe_rdwr_release(struct inode * inode, struct file * filp)
{
	PIPE_READERS(*inode)--;
	PIPE_WRITERS(*inode)--;
	wake_up_interruptible(&PIPE_WAIT(*inode));
}

/*
 * The file_operations structs are not static because they
 * are also used in linux/fs/fifo.c to do operations on fifo's.
 */
struct file_operations connecting_fifo_fops = {
	pipe_lseek,
	connect_read,
	bad_pipe_rw,
	pipe_readdir,
	connect_select,
	pipe_ioctl,
	NULL,		/* no mmap on pipes.. surprise */
	NULL,		/* no special open code */
	pipe_read_release,
	NULL
};

struct file_operations read_fifo_fops = {
	pipe_lseek,
	pipe_read,
	bad_pipe_rw,
	pipe_readdir,
	fifo_select,
	pipe_ioctl,
	NULL,		/* no mmap on pipes.. surprise */
	NULL,		/* no special open code */
	pipe_read_release,
	NULL
};

struct file_operations write_fifo_fops = {
	pipe_lseek,
	bad_pipe_rw,
	pipe_write,
	pipe_readdir,
	fifo_select,
	pipe_ioctl,
	NULL,		/* mmap */
	NULL,		/* no special open code */
	pipe_write_release,
	NULL
};

struct file_operations rdwr_fifo_fops = {
	pipe_lseek,
	pipe_read,
	pipe_write,
	pipe_readdir,
	fifo_select,
	pipe_ioctl,
	NULL,		/* mmap */
	NULL,		/* no special open code */
	pipe_rdwr_release,
	NULL
};

struct file_operations read_pipe_fops = {
	pipe_lseek,
	pipe_read,
	bad_pipe_rw,
	pipe_readdir,
	pipe_select,
	pipe_ioctl,
	NULL,		/* no mmap on pipes.. surprise */
	NULL,		/* no special open code */
	pipe_read_release,
	NULL
};

struct file_operations write_pipe_fops = {
	pipe_lseek,
	bad_pipe_rw,
	pipe_write,
	pipe_readdir,
	pipe_select,
	pipe_ioctl,
	NULL,		/* mmap */
	NULL,		/* no special open code */
	pipe_write_release,
	NULL
};

struct file_operations rdwr_pipe_fops = {
	pipe_lseek,
	pipe_read,
	pipe_write,
	pipe_readdir,
	pipe_select,
	pipe_ioctl,
	NULL,		/* mmap */
	NULL,		/* no special open code */
	pipe_rdwr_release,
	NULL
};

struct inode_operations pipe_inode_operations = {
	&rdwr_pipe_fops,
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

asmlinkage int sys_pipe(unsigned long * fildes)
{
	struct inode * inode;
	struct file * f[2];
	int fd[2];
	int i,j;

	j = verify_area(VERIFY_WRITE,fildes,8);
	if (j)
		return j;
	for(j=0 ; j<2 ; j++)
		if (!(f[j] = get_empty_filp()))
			break;
	if (j==1)
		f[0]->f_count--;
	if (j<2)
		return -ENFILE;
	j=0;
	for(i=0;j<2 && i<NR_OPEN && i<current->rlim[RLIMIT_NOFILE].rlim_cur;i++)
		if (!current->files->fd[i]) {
			current->files->fd[ fd[j]=i ] = f[j];
			j++;
		}
	if (j==1)
		current->files->fd[fd[0]]=NULL;
	if (j<2) {
		f[0]->f_count--;
		f[1]->f_count--;
		return -EMFILE;
	}
	if (!(inode=get_pipe_inode())) {
		current->files->fd[fd[0]] = NULL;
		current->files->fd[fd[1]] = NULL;
		f[0]->f_count--;
		f[1]->f_count--;
		return -ENFILE;
	}
	f[0]->f_inode = f[1]->f_inode = inode;
	f[0]->f_pos = f[1]->f_pos = 0;
	f[0]->f_flags = O_RDONLY;
	f[0]->f_op = &read_pipe_fops;
	f[0]->f_mode = 1;		/* read */
	f[1]->f_flags = O_WRONLY;
	f[1]->f_op = &write_pipe_fops;
	f[1]->f_mode = 2;		/* write */
	put_fs_long(fd[0],0+fildes);
	put_fs_long(fd[1],1+fildes);
	return 0;
}
