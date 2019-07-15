/*
 *  linux/fs/devices.c
 *
 * (C) 1993 Matthias Urlichs -- collected common code and tables.
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
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/ext_fs.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/errno.h>

//设备抽象结构
struct device_struct {
	const char * name;
	struct file_operations * fops;
};

//具体设备的表结构是保存在数组chrdevs或blkdevs中的，并由主设备号作为索引

static struct device_struct chrdevs[MAX_CHRDEV] = {
	{ NULL, NULL },
};

static struct device_struct blkdevs[MAX_BLKDEV] = {
	{ NULL, NULL },
};

//获取设备链表，将现有的所有设备的名字复制到page缓冲中
int get_device_list(char * page)
{
	int i;
	int len;
	
	//将所有的字符设备名称格式化到page中
	len = sprintf(page, "Character devices:\n");
	for (i = 0; i < MAX_CHRDEV ; i++) {
		if (chrdevs[i].fops) {
			len += sprintf(page+len, "%2d %s\n", i, chrdevs[i].name);
		}
	}
	
	//将所有的块设备名称格式化到page中
	len += sprintf(page+len, "\nBlock devices:\n");
	for (i = 0; i < MAX_BLKDEV ; i++) {
		if (blkdevs[i].fops) {
			len += sprintf(page+len, "%2d %s\n", i, blkdevs[i].name);
		}
	}
	return len;
}

//获取主编号为major的块设备的操作方法
struct file_operations * get_blkfops(unsigned int major)
{
	if (major >= MAX_BLKDEV)
		return NULL;
	return blkdevs[major].fops;
}

//获取主编号为major的字符设备的操作方法
struct file_operations * get_chrfops(unsigned int major)
{
	if (major >= MAX_CHRDEV)
		return NULL;
	return chrdevs[major].fops;
}

int register_chrdev(unsigned int major, const char * name, struct file_operations *fops)
{
	if (major == 0) {
		for (major = MAX_CHRDEV-1; major > 0; major--) {
			if (chrdevs[major].fops == NULL) {
				chrdevs[major].name = name;
				chrdevs[major].fops = fops;
				return major;
			}
		}
		return -EBUSY;
	}
	if (major >= MAX_CHRDEV)
		return -EINVAL;
	if (chrdevs[major].fops && chrdevs[major].fops != fops)
		return -EBUSY;
	chrdevs[major].name = name;
	chrdevs[major].fops = fops;
	return 0;
}

//块设备的注册与注销 块驱动, 象字符驱动, 必须使用一套注册接口来使内核可使用它们的设备

/*
				块设备驱动程序的注册 
	对于块设备来说，驱动程序的注册不仅在其初始化的时候进行而且在编译的时候也要进行注册。
	在初始化时通过 register_blkdev( )函数将相应的块设备添加到数组 blkdevs 中
*/

//大部分块驱动采取的第一步是注册它们自己到内核. 这个任务的函数是 register_blkdev
//参数是你的设备要使用的主编号和关联的name(内核将显示它在 /proc/devices). 如果 major 传递为0, 
//内核分配一个新的主编号并且返回它给调用者. 如常, 自 register_blkdev 的一个负的返回值指示已发生了一个错误
/*
	这个函数的第一个参数是主设备号，第二个参数是设备名称的字符串，第三项参数是指向具体设备操作的指针。
	如果一切顺利则返回0，否则返回负值。如果指定的主设备号为0，此函数将会搜索空闲的主设备号分配给该设
	备驱动程序并将其作为返回值。
*/
int register_blkdev(unsigned int major, const char * name, struct file_operations *fops)
{
	//若传递的参数major为0，则指示内核自动寻找一项空闲的major编号
	if (major == 0) {
		for (major = MAX_BLKDEV-1; major > 0; major--) {
			if (blkdevs[major].fops == NULL) {
				blkdevs[major].name = name;
				blkdevs[major].fops = fops;
				return major;
			}
		}
		return -EBUSY;
	}
	if (major >= MAX_BLKDEV)
		return -EINVAL;
	//此行保证了要注册的块设备不会覆盖已存在的块设备，最多也就是关联一个新的名字
	if (blkdevs[major].fops && blkdevs[major].fops != fops)
		return -EBUSY;
	blkdevs[major].name = name;
	blkdevs[major].fops = fops;
	return 0;
}

//参数必须匹配传递给 register_blkdev 的那些, 否则这个函数返回 -EINVAL 并且什么都不注销.
int unregister_chrdev(unsigned int major, const char * name)
{
	if (major >= MAX_CHRDEV)
		return -EINVAL;
	if (!chrdevs[major].fops)
		return -EINVAL;
	if (strcmp(chrdevs[major].name, name))
		return -EINVAL;
	chrdevs[major].name = NULL;
	chrdevs[major].fops = NULL;
	return 0;
}

int unregister_blkdev(unsigned int major, const char * name)
{
	if (major >= MAX_BLKDEV)
		return -EINVAL;
	if (!blkdevs[major].fops)
		return -EINVAL;
	if (strcmp(blkdevs[major].name, name))
		return -EINVAL;
	blkdevs[major].name = NULL;
	blkdevs[major].fops = NULL;
	return 0;
}

/*
 * This routine checks whether a removable media has been changed,
 * and invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 */
int check_disk_change(dev_t dev)
{
	int i;
	struct file_operations * fops;

	i = MAJOR(dev);
	if (i >= MAX_BLKDEV || (fops = blkdevs[i].fops) == NULL)
		return 0;
	if (fops->check_media_change == NULL)
		return 0;
	if (!fops->check_media_change(dev))
		return 0;

	printk("VFS: Disk change detected on device %d/%d\n",
					MAJOR(dev), MINOR(dev));
	for (i=0 ; i<NR_SUPER ; i++)
		if (super_blocks[i].s_dev == dev)
			put_super(super_blocks[i].s_dev);
	invalidate_inodes(dev);
	invalidate_buffers(dev);

	if (fops->revalidate)	//revalidate 使重新生效
		fops->revalidate(dev);
	return 1;
}

/*
	当打开一个设备i节点时，chrdev_open()函数（或者是blkdev_open()，但我只专
	注于字符设备）将被执行。这个函数是通过数据
	结构def_chr_fops取得的，而它又是被chrdev_inode_operations引用的，是被所
	有文件系统类型使用的（见前面有关文件系统的部分）。 

	chrdev_open通过在当前操作中替换具体设备的file_operations表并且调用
	特定的open()函数来管理指定的设备操作的。具体设备的表结构是保存在数组
	chrdevs[]中的，并由主设备号作为索引
*/

/*
 * Called every time a block special file is opened
 */
 //每次打开块设备文件都会调用此函数 此函数根据打开的文件i节点，
 //找到实际对应的块设备号，从而调用相应块设备的驱动程序例程
int blkdev_open(struct inode * inode, struct file * filp)
{
	int i;

	i = MAJOR(inode->i_rdev);
	if (i >= MAX_BLKDEV || !blkdevs[i].fops)
		return -ENODEV;
	filp->f_op = blkdevs[i].fops;
	if (filp->f_op->open)
		return filp->f_op->open(inode,filp);
	return 0;
}	

/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the special file...
 */
 
 /*
	块设备注册到系统以后，怎样与文件系统联系起来呢，也就是说，文件系统怎么调用已注册的块设备，这还得从file_operations结构说起
	下面我们以open()系统调用为例，说明用户进程中的一个系统调用如何最终与物理块设备的操作联系起来。在此，我们仅仅给出几个open（）
	函数的调用关系.当调用open（）系统调用时，其最终会调用到def_blk_fops 的blkdev_open（）函数。blkdev_open（）函数的任务就是
	根据主设备号找到对应的block_device_operations结构，然后再调用block_device_operations结构中的函数指针open所指向的函数，
	如果open所指向的函数非空，就调用该函数打开最终的物理块设备。这就简单地说明了块设备注册以后，从最上层的系统调用到具体的
	打开一个设备的过程。 
 */
struct file_operations def_blk_fops = {
	NULL,		/* lseek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* select */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	blkdev_open,	/* open */
	NULL,		/* release */
};

struct inode_operations blkdev_inode_operations = {
	&def_blk_fops,		/* default file operations */
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

/*
 * Called every time a character special file is opened
 */
int chrdev_open(struct inode * inode, struct file * filp)
{
	int i;

	i = MAJOR(inode->i_rdev);
	if (i >= MAX_CHRDEV || !chrdevs[i].fops)
		return -ENODEV;
	filp->f_op = chrdevs[i].fops;
	if (filp->f_op->open)
		return filp->f_op->open(inode,filp);
	return 0;
}

/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the special file...
 */
struct file_operations def_chr_fops = {
	NULL,		/* lseek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* select */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	chrdev_open,	/* open */
	NULL,		/* release */
};

struct inode_operations chrdev_inode_operations = {
	&def_chr_fops,		/* default file operations */
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
