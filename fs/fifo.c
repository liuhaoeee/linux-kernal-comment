/*
 *  linux/fs/fifo.c
 *
 *  written by Paul H. Hargrove
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
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
/*
			FIFO（命名管道）的打开规则:
	如果当前打开操作时为读而打开FIFO时，若已经有相应进程为写而打开该FIFO，
	则当前打开操作将成功返回；否则，可能阻塞到有相应进程为写而打开该FIFO
	(当前打开操作设置了阻塞标志)；或者，成功返回(当前打开操作没有设置阻塞标志)。
	如果当前打开操作时为写而打开FIFO时，如果已经有相应进程为读而打开该FIFO,则
	当前打开操作将成功返回；否则，可能阻塞直到有相应进程为读而打开该FIFO
	(当前打开操作设置了阻塞标志)；或者，返回ENIO错误(当期打开操作没有设置阻塞标志)。
*/
/*
	试图打开管道文件，注意是打开，如果在此之前已经被其他进程打开
	则一定存在读写进程，所以本函数一定不会被阻塞
	如果以写的方式打开管道阻塞了，则一定不存在读进程，但可能会存在若干个正在以写方式打开管道
	（但还没成功打开）的进程，反之亦然
	如果本函数成功返回了，即打开了管道，则成功的原因有两个：
	A.之前已经存在读写进程
	B.之前没有读写进程，但在阻塞过程中，有其他进程对该管道进行相反方式的打开操作
*/
static int fifo_open(struct inode * inode,struct file * filp)
{
	int retval = 0;
	unsigned long page;

	switch( filp->f_mode ) {

	//如果是读管道
	case 1:
	/*
	 *  O_RDONLY
	 *  POSIX.1 says that O_NONBLOCK means return with the FIFO
	 *  opened, even when there is no process writing the FIFO.
	 */
	/*
		如果设置了O_NONBLOCK标志，则要求读打开操作不要阻塞
		则将文件的操作函数设置为connecting_fifo_fops，并成功打开管道
		当读取管道时，connect_read()函数会等待管道可读时将filp->f_op
		重新设置为read_fifo_fops
	*/
		filp->f_op = &connecting_fifo_fops;
		//如果之前没有读进程，则有可能有写进程睡眠阻塞在此管道上
		//但一定不存在已经打开管道并进行读写操作的进程
		if (!PIPE_READERS(*inode)++)
			//唤醒所有因等待读管道而睡眠的写进程
			wake_up_interruptible(&PIPE_WAIT(*inode));
		//如果管道没有以O_NONBLOCK的方式打开，而且当前没有写进程
		//则需要阻塞此读进程，直到有写进程产生
		if (!(filp->f_flags & O_NONBLOCK) && !PIPE_WRITERS(*inode)) {
			//增加以读方式打开此管道而被阻塞（因为没有写进程）的进程总数
			PIPE_RD_OPENERS(*inode)++;
			//如果当前没有写进程，则在循环中将本进程设置为可中断的睡眠状态，直到产生一个写进程
			//并同时检测进程是否收到非阻塞信号，如果没有收到，则进程将一直睡眠
			//如果收到了非阻塞信号，则循环也可终止
			while (!PIPE_WRITERS(*inode)) {
				//如果进程收到非阻塞信号
				if (current->signal & ~current->blocked) {
					retval = -ERESTARTSYS;
					break;
				}
				//将本进程设置为可中断的睡眠状态，直到有写进程产生时将会被唤醒
				//其实，当有新的读进程产生时，也会被唤醒，因为不管是读进程还是写进程
				//都睡眠在同一个管道上，但由于while循环的条件，进程将又会被睡眠
				//上面一句话分析错了，读进程是不会唤醒此进程的，因为再产生读进程时，已经存在读进程了
				interruptible_sleep_on(&PIPE_WAIT(*inode));
			}
			//执行到这里，说明有写进程产生或者进程接收到非阻塞信号
			
			//当所有因等待写进程而睡眠的读进程都不再阻塞（跳出其while阻塞语句并执行--PIPE_WR_OPENERS(*inode)操作）后
			//便可唤醒因等待所有被唤醒(见case 2中开始处的几行代码)但还没有跳出while阻塞循环语句的读进程跳出其阻塞循环而睡眠的写进程
			//一旦当所有被写进程唤醒的读进程都跳出阻塞循环语句，便可唤醒将这些读进程唤醒的写进程了
			if (!--PIPE_RD_OPENERS(*inode))
				wake_up_interruptible(&PIPE_WAIT(*inode));
		}
		//如果存在以写方式打开此管道而被阻塞的进程，则将本进程设置为可中断的睡眠状态
		//这样做是为了让本读进程去等待被其唤醒（见case 1开始处的几行代码）的但还其
		//PIPE_RD_OPENERS(*inode)还未自减(见case 2中while阻塞循环语句中的if判断语句，
		//当其自减到0时，这里便被唤醒并且跳出这个while循环)的所有写进程
		//也可以说是等待被其唤醒的所有的写进程都成功打开管道
		//因为当一个管道能被读或写的方式打开时，必定不会再存在任何被阻塞的读或写进程
		while (PIPE_WR_OPENERS(*inode))
			interruptible_sleep_on(&PIPE_WAIT(*inode));
		//如果有进程写此管道
		if (PIPE_WRITERS(*inode))
			filp->f_op = &read_fifo_fops;
		if (retval && !--PIPE_READERS(*inode))	//参见case 2中的注释
			wake_up_interruptible(&PIPE_WAIT(*inode));
		break;
	
	//如果是写管道
	case 2:
	/*
	 *  O_WRONLY
	 *  POSIX.1 says that O_NONBLOCK means return -1 with
	 *  errno=ENXIO when there is no process reading the FIFO.
	 */
		//如果管道有O_NONBLOCK标志，并且没有任何进程读管道，则break
		if ((filp->f_flags & O_NONBLOCK) && !PIPE_READERS(*inode)) {
			retval = -ENXIO;
			break;
		}
		//赋予管道文件i节点写操作的能力
		filp->f_op = &write_fifo_fops;
		//增加写进程数，如果之前写进程数为0，则试图唤醒一个等待此管道的读进程
		if (!PIPE_WRITERS(*inode)++)
			wake_up_interruptible(&PIPE_WAIT(*inode));
		//如果读进程为0
		if (!PIPE_READERS(*inode)) {
			//增加以写方式打开此管道而被阻塞（因为没有读进程）的进程总数 因为执行到这里，说明没有设置O_NONBLOCK标志
			PIPE_WR_OPENERS(*inode)++;
			//如果当前没有读进程
			while (!PIPE_READERS(*inode)) {
				//若进程收到了非屏蔽信号 则返回处理，之后还可能重新启动此系统调用
				if (current->signal & ~current->blocked) {
					retval = -ERESTARTSYS;	//系统调用的重新执行 参见arch/i386/kernal/singal.c
					break;
				}
				//将进程设置为可中断睡眠状态
				interruptible_sleep_on(&PIPE_WAIT(*inode));
			}
			//执行到这里，已跳出阻塞的循环
			//如果本进程是因为收到信号才break执行到这里的 则PIPE_READERS依然为0
			//减少一个以写方式打开此管道而被阻塞（因为没有读进程）的进程总数
			if (!--PIPE_WR_OPENERS(*inode))
				wake_up_interruptible(&PIPE_WAIT(*inode));
		}
		//如果本进程是因为收到信号才break执行到这里的 则PIPE_READERS和PIPE_RD_OPENERS都为0
		
		//如果还存在以读方式打开此管道而被阻塞（因为没有写进程）的进程
		//说明当前没有产生写进程，则再次将进程设置为可中断睡眠状态
		while (PIPE_RD_OPENERS(*inode))
			//将进程设置为可中断睡眠状态
			interruptible_sleep_on(&PIPE_WAIT(*inode));
		//执行到这里，说明睡眠队列中已不包含任何读进程了
		
		//如果retval不为0（说明进程是在阻塞过程中因为收到了信号而执行到这里的）
		//retval为0，则减少写进程数
		//如果减少写进程数后为0，则需要唤醒所有因收到信号而不再阻塞的进程
		//因为如果本进程是因为收到信号而执行到这里的，则其运行状态是可运行的，也从睡眠队列中移除了
		
		//否则，PIPE_WRITERS不为0，则说明还有其他的写进程存在，但由于无读进程（所以写进程都睡眠），所以将其唤醒也是无意义的
		if (retval && !--PIPE_WRITERS(*inode))	//retval为真，则PIPE_READERS和PIPE_RD_OPENERS都为0
												//*!--PIPE_WRITERS(*inode)为真，则PIPE_WRITERS和PIPE_WR_OPENERS都为0
			//唤醒因等待写进程而睡眠的读进程 百思不得其解 我想可能是just in case
			//因为此时睡眠队列应该是空的，但若不为空...just in case???
			//或者理解为，既然没有了任何读写管道，那么为该管道睡眠的进程都应该被唤醒
			
			/*
				OK OK 我明白了 事情的真相是这样的：
				假设之前没有任何进程打开过或正在进行打开此管道文件的操作
				现在进程A要以写的方式打开此管道 但由于不存在读进程
				所以进程A阻塞、睡眠
				某一时刻，进程B要以写的方式打开此管道
				进程B发现之前没有写进程，所以会唤醒所有可能存在的读进程，包括A进程
				但是A进程在睡眠过程中收到了信号，所以进程A之前就已经变成可运行状态，但调度器还没调度到它
				但是当调度器调度到他的时候，retval就不会为真了
				......不对，想错了，感觉linux内核真是无懈可击啊 还是不得其解 just in case???
			*/
			/*
				Not just in case！
				可能和select()系统调用有关，进程调用select()系统调用等待管道可读或可写
				进程就可能睡眠在此管道上 但和打开管道无关 参见select() pipe_select()等内涵函数分析
			*/
			wake_up_interruptible(&PIPE_WAIT(*inode));
		break;
	
	//以读写方式打开
	case 3:
	/*
	 *  O_RDWR
	 *  POSIX.1 leaves this case "undefined" when O_NONBLOCK is set.
	 *  This implementation will NEVER block on a O_RDWR open, since
	 *  the process can at least talk to itself.
	 */
		filp->f_op = &rdwr_fifo_fops;
		//如果之前的读进程数为0，则唤醒所有的写进程
		if (!PIPE_READERS(*inode)++)
			wake_up_interruptible(&PIPE_WAIT(*inode));
		//等待被此进程唤醒的所有写进程都跳出阻塞
		while (PIPE_WR_OPENERS(*inode))
			interruptible_sleep_on(&PIPE_WAIT(*inode));
		//如果之前的写进程数为0，则唤醒所有的读进程
		if (!PIPE_WRITERS(*inode)++)
			wake_up_interruptible(&PIPE_WAIT(*inode));
		//等待被此进程唤醒的所有读进程都跳出阻塞
		while (PIPE_RD_OPENERS(*inode))
			interruptible_sleep_on(&PIPE_WAIT(*inode));
		break;

	default:
		retval = -EINVAL;
	}
	if (retval || PIPE_BASE(*inode))
		return retval;
	page = __get_free_page(GFP_KERNEL);
	if (PIPE_BASE(*inode)) {
		free_page(page);
		return 0;
	}
	if (!page)
		return -ENOMEM;
	PIPE_LOCK(*inode) = 0;
	PIPE_START(*inode) = PIPE_LEN(*inode) = 0;
	PIPE_BASE(*inode) = (char *) page;
	return 0;
}

/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the access mode of the file...
 */
static struct file_operations def_fifo_fops = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	fifo_open,		/* will set read or write pipe_fops */
	NULL,
	NULL
};

static struct inode_operations fifo_inode_operations = {
	&def_fifo_fops,		/* default file operations */
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

void init_fifo(struct inode * inode)
{
	inode->i_op = &fifo_inode_operations;
	inode->i_pipe = 1;
	PIPE_LOCK(*inode) = 0;
	PIPE_BASE(*inode) = NULL;
	PIPE_START(*inode) = PIPE_LEN(*inode) = 0;
	PIPE_RD_OPENERS(*inode) = PIPE_WR_OPENERS(*inode) = 0;
	PIPE_WAIT(*inode) = NULL;
	PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 0;
}
