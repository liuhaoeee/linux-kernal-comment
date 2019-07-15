/*
 *  linux/fs/ioctl.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
//Linux操作系统内核就是这样 曾经风云变幻 到最后却被千万行的代码淹没的无影无踪  
//幸好 他有时又会留下一点蛛丝马迹 引领我们穿越操作系统内部所有的秘密
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
			使用ioctl()操作设备 
	ioctl()系统调用是用户进程控制设备文件行为的入口点。Ioctl管理是从
	../../fs/ioctl.c中产生的，实际上sys_ioctl()就是在这个ioctl.c中的。标准
	的ioctl请求就是在那里执行的，其它与文件相关的请求是由file_ioctl()处理的
	（在同一个源文件中），而其它任何请求都分派给特定设备的ioctl()函数
*/
#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/termios.h>
#include <linux/fcntl.h> /* for f_flags values */

static int file_ioctl(struct file *filp,unsigned int cmd,unsigned long arg)
{
	int error;
	int block;

	switch (cmd) {
		case FIBMAP:
			if (filp->f_inode->i_op == NULL)
				return -EBADF;
		    	if (filp->f_inode->i_op->bmap == NULL)
				return -EINVAL;
			error = verify_area(VERIFY_WRITE,(void *) arg,4);
			if (error)
				return error;
			block = get_fs_long((long *) arg);
			block = filp->f_inode->i_op->bmap(filp->f_inode,block);
			put_fs_long(block,(long *) arg);
			return 0;
		case FIGETBSZ:
			if (filp->f_inode->i_sb == NULL)
				return -EBADF;
			error = verify_area(VERIFY_WRITE,(void *) arg,4);
			if (error)
				return error;
			put_fs_long(filp->f_inode->i_sb->s_blocksize,
			    (long *) arg);
			return 0;
		case FIONREAD:
			error = verify_area(VERIFY_WRITE,(void *) arg,4);
			if (error)
				return error;
			put_fs_long(filp->f_inode->i_size - filp->f_pos,
			    (long *) arg);
			return 0;
	}
	if (filp->f_op && filp->f_op->ioctl)
		return filp->f_op->ioctl(filp->f_inode, filp, cmd,arg);
	return -EINVAL;
}


asmlinkage int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;
	int on;

	if (fd >= NR_OPEN || !(filp = current->files->fd[fd]))
		return -EBADF;
	switch (cmd) {
		//设置 close-on-exec 标志(File IOctl Close on EXec) 
		//设置这个标志使文件描述符被关闭
		case FIOCLEX:
			FD_SET(fd, &current->files->close_on_exec);
			return 0;

		//清除 close-no-exec 标志(File IOctl Not CLose on EXec)
		case FIONCLEX:
			FD_CLR(fd, &current->files->close_on_exec);
			return 0;

		case FIONBIO:
			on = get_fs_long((unsigned long *) arg);
			if (on)
				filp->f_flags |= O_NONBLOCK;
			else
				filp->f_flags &= ~O_NONBLOCK;
			return 0;

		case FIOASYNC: /* O_SYNC is not yet implemented,
				  but it's here for completeness. */
			on = get_fs_long ((unsigned long *) arg);
			if (on)
				filp->f_flags |= O_SYNC;
			else
				filp->f_flags &= ~O_SYNC;
			return 0;

		default:
			if (filp->f_inode && S_ISREG(filp->f_inode->i_mode))
				return file_ioctl(filp,cmd,arg);

			if (filp->f_op && filp->f_op->ioctl)
				return filp->f_op->ioctl(filp->f_inode, filp, cmd,arg);

			return -EINVAL;
	}
}
