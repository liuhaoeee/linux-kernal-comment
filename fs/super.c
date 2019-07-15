/*
 *  linux/fs/super.c
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
/*
 * super.c contains code to handle the super-block tables.
 */
 /*
							超级块对象
	存储一个已安装的文件系统的控制信息，代表一个已安装的文件系统；每次一个实际的文件系统被安装时， 
	内核会从磁盘的特定位置读取一些控制信息来填充内存中的超级块对象。一个安装实例和一个超级块对象
	一一对应。 超级块通过其结构中的一个域s_type记录它所属的文件系统类型。除此之外还有一个文件系统链表，
	对应着当前系统中挂载的所有文件系统。超级块对象数组和文件系统链表的区别是，对于一个已经挂载的文件系统，
	在文件系统链表中只存在一个唯一的实例，而对于超级块对象，则可有同一个文件系统的多个实例，因为一个系统中
	多个分区可以安装同一个文件系统，所以会存在同一个文件系统的多个实例的情况
*/

#include <stdarg.h>

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>
 
extern struct file_operations * get_blkfops(unsigned int);	//获取特定块设备的块操作方法？？？
extern struct file_operations * get_chrfops(unsigned int);	//获取特定字符设备的操作方法？？？

extern void wait_for_keypress(void);

extern int root_mountflags;

struct super_block super_blocks[NR_SUPER];	//NR_SUPER=32 系统中能同时安装的文件系统总数数组

static int do_remount_sb(struct super_block *sb, int flags, char * data);

/* this is initialized in init/main.c */
dev_t ROOT_DEV = 0;

//文件系统链表头
static struct file_system_type * file_systems = NULL;

//注册文件系统
int register_filesystem(struct file_system_type * fs)
{
	struct file_system_type ** tmp;	//之所以要用二级指针 是因为函数要修改链表

	//如果指定的药注册的文件系统fs为空 返回
	if (!fs)
		return -EINVAL;
	//如果fs->next不为空，说明fs可能是已经注册过的文件系统，则返回EBUSY
	//这样就可避免在链表中的循环查找操作
	if (fs->next)
		return -EBUSY;
	//tmp指向文件系统链表头
	tmp = &file_systems;
	//遍历当前系统中已经存在的文件系统
	while (*tmp) {
		//比较要注册的文件系统和系统中已经存在的文件系统的名字，若要注册的文件系统已经存在于链表中，则返回错误信息
		if (strcmp((*tmp)->name, fs->name) == 0)
			return -EBUSY;
		tmp = &(*tmp)->next;
	}
	//将fs链入文件系统链表
	*tmp = fs;
	return 0;
}

//unregister_filesystem用于注销指定的文件系统
int unregister_filesystem(struct file_system_type * fs)
{
	struct file_system_type ** tmp;

	//tmp指向文件系统链表头
	tmp = &file_systems;
	//遍历当前系统中已经存在的文件系统 找到要注销的文件系统
	while (*tmp) {
		if (fs == *tmp) {
			//将fs从文件系统链表中摘除
			*tmp = fs->next;
			fs->next = NULL;
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	//执行到这里，说明没有找到要注销的文件系统 则返回出错信息
	return -EINVAL;
}

//遍历文件系统链表，并且让index自加，找到指定的文件系统所在的节点后，跳出循环，然后返回index值
static int fs_index(const char * __name)
{
	struct file_system_type * tmp;
	char * name;
	int err, index;
	//将__name所指示得字符串拷入内核空间，并且如果内核定制了audit属性，增加该项进入查找access访问控制表
	err = getname(__name, &name);	//作用就是把__name（用户态的字符串）拷贝到内核空间中
	if (err)
		return err;
	index = 0;
	for (tmp = file_systems ; tmp ; tmp = tmp->next) {
		if (strcmp(tmp->name, name) == 0) {
			putname(name);
			return index;
		}
		index++;
	}
	putname(name);	//函数的作用是把内核态的name字符串写回到用户态下。和之前的getname的作用刚好相反
	return -EINVAL;
}

//该函数的作用就是找到文件系统链表中第index个文件系统的名称，然后将值返回到用户空间buf中。
static int fs_name(unsigned int index, char * buf)
{
	struct file_system_type * tmp;
	int err, len;

	tmp = file_systems;
	while (tmp && index > 0) {
		tmp = tmp->next;
		index--;
	}
	if (!tmp)
		return -EINVAL;
	len = strlen(tmp->name) + 1;
	err = verify_area(VERIFY_WRITE, buf, len);
	if (err)
		return err;
	memcpy_tofs(buf, tmp->name, len);
	return 0;
}

//该函数的作用计算文件系统链表中节点的总数，应该是操作前加锁，操作后解锁，其中的遍历过程使用index自加来记录，
//最终的index值就是节点的总数。
static int fs_maxindex(void)
{
	struct file_system_type * tmp;
	int index;

	index = 0;
	for (tmp = file_systems ; tmp ; tmp = tmp->next)
		index++;
	return index;
}

/*
 * Whee.. Weird sysv syscall. 
 */
 //这是一种系统调用的定义方式，通过option的不同，可以完成3种不同的功能：
 //(1)查找指定文件系统所在文件系统链表的位置
 //(2)查找指定文件系统的名称
 //(3)查看当前文件系统链表的总长度。
asmlinkage int sys_sysfs(int option, ...)
{
	va_list args;
	int retval = -EINVAL;
	unsigned int index;

	va_start(args, option);
	switch (option) {
		case 1:
			retval = fs_index(va_arg(args, const char *));
			break;

		case 2:
			index = va_arg(args, unsigned int);
			retval = fs_name(index, va_arg(args, char *));
			break;

		case 3:
			retval = fs_maxindex();
			break;
	}
	va_end(args);
	return retval;
}

//将当前存在于文件系统链表中的文件系统的名称保存到buf中，并且记录下长度len。而且区分需要dev和不需要dev（即nodev）的两种文件系统
int get_filesystem_list(char * buf)
{
	int len = 0;
	struct file_system_type * tmp;

	tmp = file_systems;
	//buf+len代表每次循环中格式化后的字符串从buf+len处开始存放
	while (tmp && len < PAGE_SIZE - 80) {
		len += sprintf(buf+len, "%s\t%s\n",
			tmp->requires_dev ? "" : "nodev",
			tmp->name);
		tmp = tmp->next;
	}
	return len;
}

//该函数的作用就是根据指定的文件系统的名称，找到该文件系统所在文件系统链表的节点位置，返回其指针指向。
//如果没有找到就返回一个NULL指针。当然，在对文件系统链表进行操作的时候要进行加锁保护
struct file_system_type *get_fs_type(char *name)
{
	struct file_system_type * fs = file_systems;
	
	if (!name)
		return fs;
	while (fs) {
		if (!strcmp(name,fs->name))
			break;
		fs = fs->next;
	}
	return fs;
}

void __wait_on_super(struct super_block * sb)
{
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&sb->s_wait, &wait);
repeat:
	current->state = TASK_UNINTERRUPTIBLE;
	if (sb->s_lock) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&sb->s_wait, &wait);
	current->state = TASK_RUNNING;
}

//内核线程功能专一，用来同步操作系统当前挂载的各个文件系统的超级块数据，由于超级块对于文件系统的特殊性，
//所以这对保证文件系统的完整性至关重要
void sync_supers(dev_t dev)
{
	struct super_block * sb;

	for (sb = super_blocks + 0 ; sb < super_blocks + NR_SUPER ; sb++) {
		//如果超级块对象没有对应任何设备,即此超级块对象空闲
		if (!sb->s_dev)
			continue;
		//如果dev不为0（为0代表同步所有超级块），但是本超级块对应对应的设备不是要求同步的设备
		if (dev && sb->s_dev != dev)
			continue;
		//等待超级块对象解锁
		wait_on_super(sb);
		//在等待过程中，可能睡眠...
		if (!sb->s_dev || !sb->s_dirt)
			continue;
		if (dev && (dev != sb->s_dev))
			continue;
		if (sb->s_op && sb->s_op->write_super)
			sb->s_op->write_super(sb);	//调用具体的文件系统例程
	}
}

// 取指定设备dev的超级块
static struct super_block * get_super(dev_t dev)
{
	struct super_block * s;
	
	if (!dev)
		return NULL;
	s = 0+super_blocks;
	//遍历超级块数组
	while (s < NR_SUPER+super_blocks)
		if (s->s_dev == dev) {	//如果本超级块对象的设备号等于要获取的设备号
			wait_on_super(s);	//等待超级块解锁
			//等待过程中可能睡眠
			if (s->s_dev == dev)
				return s;
			//执行到这里，说明在等待解锁的过程中，s已经被其他设备所占用，
			//则重新从头开始扫描超级块数组，重新寻找新的空闲超级块对象
			s = 0+super_blocks;
		} else
			s++;
	//执行到这，说明没有找到
	return NULL;
}

//释放一个文件系统超级块对象
void put_super(dev_t dev)
{
	struct super_block * sb;

	if (dev == ROOT_DEV) {	//如果释放的是根设备的超级块对象，说明系统出现了问题，或者...
		printk("VFS: Root device %d/%d: prepare for armageddon\n",
							MAJOR(dev), MINOR(dev));
		return;
	}
	if (!(sb = get_super(dev)))
		return;
	if (sb->s_covered) {	 //此超级块被挂载了一个目录节点下
		printk("VFS: Mounted device %d/%d - tssk, tssk\n",
						MAJOR(dev), MINOR(dev));
		return;
	}
	if (sb->s_op && sb->s_op->put_super)
		sb->s_op->put_super(sb);
}

//读取指定设备的超级块，如果该设备还没有超级块，就创建一个并返回
static struct super_block * read_super(dev_t dev,char *name,int flags,
				       void *data, int silent)
{
	struct super_block * s;
	struct file_system_type *type;

	if (!dev)
		return NULL;
	check_disk_change(dev);
	s = get_super(dev);
	if (s)
		return s;
	//该设备还没有超级块，则尝试创建一个并返回
	if (!(type = get_fs_type(name))) {
		printk("VFS: on device %d/%d: get_fs_type(%s) failed\n",
						MAJOR(dev), MINOR(dev), name);
		return NULL;
	}
	//在超级块数组中寻找一份空闲超级块对象
	for (s = 0+super_blocks ;; s++) {
		if (s >= NR_SUPER+super_blocks)
			return NULL;
		if (!s->s_dev)
			break;
	}
	//初始化此超级块对象
	s->s_dev = dev;
	s->s_flags = flags;
	if (!type->read_super(s,data, silent)) {
		s->s_dev = 0;	//读取失败，则释放此超级块对象
		return NULL;
	}
	s->s_dev = dev;
	s->s_covered = NULL;
	s->s_rd_only = 0;
	s->s_dirt = 0;
	s->s_type = type;
	return s;
}

/*
 * Unnamed block devices are dummy devices used by virtual
 * filesystems which don't use real block-devices.  -- jrs
 */

 //无名块设备，不是用实际的块设备
static char unnamed_dev_in_use[256/8] = { 0, };

static dev_t get_unnamed_dev(void)
{
	int i;

	for (i = 1; i < 256; i++) {
		if (!set_bit(i,unnamed_dev_in_use))
			return (UNNAMED_MAJOR << 8) | i;	//UNNAMED_MAJOR=0	Major.h
	}
	return 0;
}

static void put_unnamed_dev(dev_t dev)
{
	if (!dev)
		return;
	if (MAJOR(dev) == UNNAMED_MAJOR &&
	    clear_bit(MINOR(dev), unnamed_dev_in_use))
		return;
	printk("VFS: put_unnamed_dev: freeing unused device %d/%d\n",
			MAJOR(dev), MINOR(dev));
}

static int do_umount(dev_t dev)
{
	struct super_block * sb;
	int retval;
	
	//对于根文件系统需要特殊的处理：同步设备，然后使用只读的方法来重新挂载根文件系统
	if (dev==ROOT_DEV) {
		/* Special case for "unmounting" root.  We just try to remount
		   it readonly, and sync() the device. */
		if (!(sb=get_super(dev)))
			return -ENOENT;
		if (!(sb->s_flags & MS_RDONLY)) {
			fsync_dev(dev);
			retval = do_remount_sb(sb, MS_RDONLY, 0);
			if (retval)
				return retval;
		}
		return 0;
	}
	//*!(sb->s_covered)不能被卸载，没有挂载在任何目录下
	if (!(sb=get_super(dev)) || !(sb->s_covered))
		return -ENOENT;
	if (!sb->s_covered->i_mount)	 //是否被挂载了
		printk("VFS: umount(%d/%d): mounted inode has i_mount=NULL\n",
	//这里进行尝试卸载操作，成功了就进行下面的善后操作
							MAJOR(dev), MINOR(dev));
	if (!fs_may_umount(dev, sb->s_mounted))
		return -EBUSY;
	//设置超级块的参数，放回相应的挂载的节点
	sb->s_covered->i_mount = NULL;
	iput(sb->s_covered);
	sb->s_covered = NULL;	//保存的是被挂载目录的节点 同时目录节点的i_mount也保存了
	iput(sb->s_mounted);
	sb->s_mounted = NULL;
	//如果超级块脏了，就执行写入操作
	if (sb->s_op && sb->s_op->write_super && sb->s_dirt)
		sb->s_op->write_super(sb);
	//放回设备的超级块共其他的设备使用 
	put_super(dev);
	return 0;
}

/*
 * Now umount can handle mount points as well as block devices.
 * This is important for filesystems which use unnamed block devices.
 *
 * There is a little kludge here with the dummy_inode.  The current
 * vfs release functions only use the r_dev field in the inode so
 * we give them the info they need without using a real inode.
 * If any other fields are ever needed by any block device release
 * functions, they should be faked here.  -- jrs
 */

asmlinkage int sys_umount(char * name)
{
	struct inode * inode;
	dev_t dev;
	int retval;
	struct inode dummy_inode;
	struct file_operations * fops;

	if (!suser())
		return -EPERM;
	//找到要卸载的文件的i节点
	retval = namei(name,&inode);
	if (retval) {
		//这里说明namei返回错误不一定是真的错误，而可能是由于符号连接的问题，使用lnamei检查
		retval = lnamei(name,&inode);
		if (retval)
			return retval;
	}
	/*如果是块设备*/
	if (S_ISBLK(inode->i_mode)) {
		dev = inode->i_rdev;	//只有块设备才有rdev字段表示主设备和此设备号
		 //检查是否具有真的设备，因为这里的内核有多类文件系统了
		if (IS_NODEV(inode)) {
			iput(inode);
			return -EACCES;
		}
	} else {	/*非块设备的操作*/
		if (!inode->i_sb || inode != inode->i_sb->s_mounted) {
			iput(inode);
			return -EINVAL;
		}
		dev = inode->i_sb->s_dev;
		iput(inode);
		memset(&dummy_inode, 0, sizeof(dummy_inode));
		dummy_inode.i_rdev = dev;
		inode = &dummy_inode;	 //节点复位操作了
		//呃，看看上面的操作，实际就是保留了dev，然后所有的东西
		//清零
	}
	if (MAJOR(dev) >= MAX_BLKDEV) {
		iput(inode);
		return -ENXIO;
	}
	if (!(retval = do_umount(dev)) && dev != ROOT_DEV) {
		fops = get_blkfops(MAJOR(dev));
		if (fops && fops->release)
			fops->release(inode,NULL);
		/*这是对于未命名的设备进行的操作*/
		if (MAJOR(dev) == UNNAMED_MAJOR)
			put_unnamed_dev(dev);	//直接复位
	}
	if (inode != &dummy_inode)
		iput(inode);
	if (retval)
		return retval;
	fsync_dev(dev);
	return 0;
}

/*
 * do_mount() does the actual mounting after sys_mount has done the ugly
 * parameter parsing. When enough time has gone by, and everything uses the
 * new mount() parameters, sys_mount() can then be cleaned up.
 *
 * We cannot mount a filesystem if it has active, used, or dirty inodes.
 * We also have to flush all inode-data for this device, as the new mount
 * might need new info.
 */
static int do_mount(dev_t dev, const char * dir, char * type, int flags, void * data)
{
	struct inode * dir_i;
	struct super_block * sb;
	int error;

	error = namei(dir,&dir_i);
	if (error)
		return error;
	//引用计数只能是1，表示在此处引用（因为在sys_mount中
	//设备被打开了一次了）
	//并且没有被挂载
	if (dir_i->i_count != 1 || dir_i->i_mount) {
		iput(dir_i);
		return -EBUSY;
	}
	if (!S_ISDIR(dir_i->i_mode)) {	//不是目录类型的文件
		iput(dir_i);
		return -ENOTDIR;
	}
	if (!fs_may_mount(dev)) {
		iput(dir_i);
		return -EBUSY;
	}
	//注意，这里的read_super函数，如果原先的设备没有
    //超级块，会自动创建一块的（这不正是我们想要的吗？）
	sb = read_super(dev,type,flags,data,0);
	if (!sb) {
		iput(dir_i);
		return -EINVAL;
	}
	if (sb->s_covered) {	//超级块已经被挂载在了某目录节点下
		iput(dir_i);
		return -EBUSY;
	}
	//sb->s_mounted指向被加载文件系统的根目录i节点
    //dir_i->i_mount保存有sb->s_mounted
    //sb->s_covered保存有dir_i
	sb->s_covered = dir_i;	//将sb安装在dir_i目录节点下
	dir_i->i_mount = sb->s_mounted;
	return 0;		/* we don't iput(dir_i) - see umount */
}


/*
 * Alters the mount flags of a mounted file system. Only the mount point
 * is used as a reference - file system type and the device are ignored.
 * FS-specific mount options can't be altered by remounting.
 */

//主要功能是改变已经被挂载的文件系统的挂载选项的
static int do_remount_sb(struct super_block *sb, int flags, char *data)
{
	int retval;
	
	//如果文件系统是只读的，但是我们挂载文件系统的时候没有指定RDONLY标志，则返回错误信息
	if (!(flags & MS_RDONLY ) && sb->s_dev && is_read_only(sb->s_dev))
		return -EACCES;
		/*flags |= MS_RDONLY;*/
	/* If we are remounting RDONLY, make sure there are no rw files open */
	if ((flags & MS_RDONLY) && !(sb->s_flags & MS_RDONLY))
		if (!fs_may_remount_ro(sb->s_dev))
			return -EBUSY;
	if (sb->s_op && sb->s_op->remount_fs) {
		retval = sb->s_op->remount_fs(sb, &flags, data);
		if (retval)
			return retval;
	}
	//记录改变后的挂载选项
	sb->s_flags = (sb->s_flags & ~MS_RMT_MASK) |
		(flags & MS_RMT_MASK);
	return 0;
}

static int do_remount(const char *dir,int flags,char *data)
{
	struct inode *dir_i;
	int retval;

	retval = namei(dir,&dir_i);
	if (retval)
		return retval;
	if (dir_i != dir_i->i_sb->s_mounted) {
		iput(dir_i);
		return -EINVAL;
	}
	retval = do_remount_sb(dir_i->i_sb, flags, data);
	iput(dir_i);
	return retval;
}

//就是将data数据拷贝到内核空间中，然后用where返回数据的地址
static int copy_mount_options (const void * data, unsigned long *where)
{
	int i;
	unsigned long page;
	struct vm_area_struct * vma;

	*where = 0;
	if (!data)
		return 0;

	vma = find_vma(current, (unsigned long) data);
	if (!vma || (unsigned long) data < vma->vm_start)
		return -EFAULT;
	i = vma->vm_end - (unsigned long) data;
	if (PAGE_SIZE <= (unsigned long) i)
		i = PAGE_SIZE-1;
	if (!(page = __get_free_page(GFP_KERNEL))) {
		return -ENOMEM;
	}
	memcpy_fromfs((void *) page,data,i);
	*where = page;
	return 0;
}

/*
 * Flags is a 16-bit value that allows up to 16 non-fs dependent flags to
 * be given to the mount() call (ie: read-only, no-dev, no-suid etc).
 *
 * data is a (void *) that can point to any structure up to
 * PAGE_SIZE-1 bytes, which can contain arbitrary fs-dependent
 * information (or be NULL).
 *
 * NOTE! As old versions of mount() didn't use this setup, the flags
 * has to have a special 16-bit magic number in the hight word:
 * 0xC0ED. If this magic word isn't present, the flags and data info
 * isn't used, as the syscall assumes we are talking to an older
 * version that didn't understand them.
 */
asmlinkage int sys_mount(char * dev_name, char * dir_name, char * type,
	unsigned long new_flags, void * data)
{
	struct file_system_type * fstype;
	struct inode * inode;
	struct file_operations * fops;
	dev_t dev;
	int retval;
	char * t;
	unsigned long flags = 0;
	unsigned long page = 0;

	if (!suser())
		return -EPERM;
	if ((new_flags &
	     (MS_MGC_MSK | MS_REMOUNT)) == (MS_MGC_VAL | MS_REMOUNT)) {
		retval = copy_mount_options (data, &page);
		if (retval < 0)
			return retval;
		retval = do_remount(dir_name,
				    new_flags & ~MS_MGC_MSK & ~MS_REMOUNT,
				    (char *) page);
		free_page(page);
		return retval;
	}
	retval = copy_mount_options (type, &page);
	if (retval < 0)
		return retval;
	fstype = get_fs_type((char *) page);
	free_page(page);
	if (!fstype)		
		return -ENODEV;
	t = fstype->name;
	fops = NULL;
	if (fstype->requires_dev) {
		retval = namei(dev_name,&inode);
		if (retval)
			return retval;
		if (!S_ISBLK(inode->i_mode)) {
			iput(inode);
			return -ENOTBLK;
		}
		if (IS_NODEV(inode)) {
			iput(inode);
			return -EACCES;
		}
		dev = inode->i_rdev;
		if (MAJOR(dev) >= MAX_BLKDEV) {
			iput(inode);
			return -ENXIO;
		}
		fops = get_blkfops(MAJOR(dev));
		if (!fops) {
			iput(inode);
			return -ENOTBLK;
		}
		if (fops->open) {
			struct file dummy;	/* allows read-write or read-only flag */
			memset(&dummy, 0, sizeof(dummy));
			dummy.f_inode = inode;
			dummy.f_mode = (new_flags & MS_RDONLY) ? 1 : 3;
			retval = fops->open(inode, &dummy);
			if (retval) {
				iput(inode);
				return retval;
			}
		}

	} else {
		if (!(dev = get_unnamed_dev()))
			return -EMFILE;
		inode = NULL;
	}
	page = 0;
	if ((new_flags & MS_MGC_MSK) == MS_MGC_VAL) {
		flags = new_flags & ~MS_MGC_MSK;
		retval = copy_mount_options(data, &page);
		if (retval < 0) {
			iput(inode);
			return retval;
		}
	}
	retval = do_mount(dev,dir_name,t,flags,(void *) page);
	free_page(page);
	if (retval && fops && fops->release)
		fops->release(inode, NULL);
	iput(inode);
	return retval;
}

/*
	根文件系统至少包括以下目录：
	/etc/：存储重要的配置文件。
	/bin/：存储常用且开机时必须用到的执行文件。
	/sbin/：存储着开机过程中所需的系统执行文件。
	/lib/：存储/bin/及/sbin/的执行文件所需的链接库，以及Linux的内核模块。
	/dev/：存储设备文件。
	注：五大目录必须存储在根文件系统上，缺一不可。
	
	以只读的方式挂载根文件系统，之所以采用只读的方式挂载根文件系统是因为：此时Linux内核仍在启动阶段，
	还不是很稳定，如果采用可读可写的方式挂载根文件系统，万一Linux不小心宕机了，一来可能破坏根文件系统
	上的数据，再者Linux下次开机时得花上很长的时间来检查并修复根文件系统。

	挂载根文件系统的而目的有两个：一是安装适当的内核模块，以便驱动某些硬件设备或启用某些功能；二是启动
	存储于文件系统中的init服务，以便让init服务接手后续的启动工作。
*/
void mount_root(void)
{
	struct file_system_type * fs_type;
	struct super_block * sb;
	struct inode * inode, d_inode;
	struct file filp;
	int retval;

	memset(super_blocks, 0, sizeof(super_blocks));
#ifdef CONFIG_BLK_DEV_FD
	if (MAJOR(ROOT_DEV) == FLOPPY_MAJOR) {
		printk(KERN_NOTICE "VFS: Insert root floppy and press ENTER\n");
		wait_for_keypress();
	}
#endif

	memset(&filp, 0, sizeof(filp));
	memset(&d_inode, 0, sizeof(d_inode));
	d_inode.i_rdev = ROOT_DEV;
	filp.f_inode = &d_inode;
	if ( root_mountflags & MS_RDONLY)
		filp.f_mode = 1; /* read only */
	else
		filp.f_mode = 3; /* read write */
	retval = blkdev_open(&d_inode, &filp);
	if(retval == -EROFS){
		root_mountflags |= MS_RDONLY;
		filp.f_mode = 1;
		retval = blkdev_open(&d_inode, &filp);
	}

	for (fs_type = file_systems ; fs_type ; fs_type = fs_type->next) {
		if(retval)
			break;
		if (!fs_type->requires_dev)
			continue;
		sb = read_super(ROOT_DEV,fs_type->name,root_mountflags,NULL,1);
		if (sb) {
			inode = sb->s_mounted;
			inode->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
			sb->s_covered = inode;
			sb->s_flags = root_mountflags;
			//比较核心的关键所在
/*
			//进程1调用的setup()最终调用mount_root()函数挂载根文件系统
			//其核心的关键操作即将根文件系统的超级块读入，并建立起与之相关的
			//inode结构，并将此inode结构设置为进程1的当前目录和根目录
			//current->fs->pwd = inode;
			//current->fs->root = inode; 重中之重！
			//正是因为系统中的所有进程（除进程0）都是进程1的子进程，所以所有的进程
			//都继承了此根文件系统 也正是因为如此，所以进程1绝不能被杀死
*/
			current->fs->pwd = inode;
			current->fs->root = inode;
			printk ("VFS: Mounted root (%s filesystem)%s.\n",
				fs_type->name,
				(sb->s_flags & MS_RDONLY) ? " readonly" : "");
			return;
		}
	}
	panic("VFS: Unable to mount root fs on %02x:%02x",
		MAJOR(ROOT_DEV), MINOR(ROOT_DEV));
}
