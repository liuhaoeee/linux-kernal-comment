/*
 *  linux/fs/filesystems.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  table of configured filesystems
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

#include <linux/config.h>
#include <linux/fs.h>
 
//包含了linux/fs目录下列出的内核支持的所有文件系统类型
#include <linux/minix_fs.h>
#include <linux/ext_fs.h>
#include <linux/ext2_fs.h>
#include <linux/xia_fs.h>
#include <linux/msdos_fs.h>
#include <linux/umsdos_fs.h>
#include <linux/proc_fs.h>
#include <linux/nfs_fs.h>
#include <linux/iso_fs.h>
#include <linux/sysv_fs.h>
#include <linux/hpfs_fs.h>

extern void device_setup(void);

/* This may be used only once, enforced by 'static int callable' */
//根据系统配置文件，注册相应的文件系统，注册过程参见supper.c
asmlinkage int sys_setup(void)
{
	static int callable = 1;
	//通过判断这个static变量，迫使系统只能调用此函数一次
	if (!callable)
		return -1;
	callable = 0;

	device_setup();

#ifdef CONFIG_MINIX_FS
	register_filesystem(&(struct file_system_type)
		{minix_read_super, "minix", 1, NULL});
#endif

#ifdef CONFIG_EXT_FS
	register_filesystem(&(struct file_system_type)
		{ext_read_super, "ext", 1, NULL});
#endif

#ifdef CONFIG_EXT2_FS
	register_filesystem(&(struct file_system_type)
		{ext2_read_super, "ext2", 1, NULL});
#endif

#ifdef CONFIG_XIA_FS
	register_filesystem(&(struct file_system_type)
		{xiafs_read_super, "xiafs", 1, NULL});
#endif
#ifdef CONFIG_UMSDOS_FS
	register_filesystem(&(struct file_system_type)
	{UMSDOS_read_super,	"umsdos",	1, NULL});
#endif

#ifdef CONFIG_MSDOS_FS
	register_filesystem(&(struct file_system_type)
		{msdos_read_super, "msdos", 1, NULL});
#endif

#ifdef CONFIG_PROC_FS
	register_filesystem(&(struct file_system_type)
		{proc_read_super, "proc", 0, NULL});
#endif

#ifdef CONFIG_NFS_FS
	register_filesystem(&(struct file_system_type)
		{nfs_read_super, "nfs", 0, NULL});
#endif

#ifdef CONFIG_ISO9660_FS
	register_filesystem(&(struct file_system_type)
		{isofs_read_super, "iso9660", 1, NULL});
#endif

#ifdef CONFIG_SYSV_FS
	register_filesystem(&(struct file_system_type)
		{sysv_read_super, "xenix", 1, NULL});

	register_filesystem(&(struct file_system_type)
		{sysv_read_super, "sysv", 1, NULL});

	register_filesystem(&(struct file_system_type)
		{sysv_read_super, "coherent", 1, NULL});
#endif

#ifdef CONFIG_HPFS_FS
	register_filesystem(&(struct file_system_type)
		{hpfs_read_super, "hpfs", 1, NULL});
#endif

	mount_root();
	return 0;
}
