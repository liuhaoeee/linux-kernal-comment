/*
 *  linux/fs/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Some corrections by tytso.
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
						有关Linux0.11 namei.c的说明
	namei.c:提供文件路径名到i结点的操作。大部函数参数都直接给出文件路径名，
	所以它实现了通过文件名来管理文件系统的接口。如打开创建删除文件、目录，创建删除文件硬连接等。
	大部分函数的原理都差不多：调用get_dir取得文件最后一层目录的i结点dir。如果是查找就调用find_entry
	从dir的数据块中查找匹配文件名字符串的目录项。这样通过目录项就取得了文件的i结点。
	如果是创建(sys_mknod)就申请一个空的inode,在dir的数据块中找一个空闲的目录项，把文件名和inode号填入目录项。
	创建目录的时候要特殊一些，要为目录申请一个数据块，至少填两个目录项，.和..  (sys_mkdir)。
	删除文件和目录的时候把要释放i结点并删除所在目录的数据块中占用的目录项。
	打开函数open_namei()基本上实现了open()的绝大部分功能。它先按照上述过程通过文件路径名查找 
	最后一层目录i结点，在目录数据块中查找目录顶。如果没找到且有创建标志，则创建新文件，
	申请一个空闲的inode和目录项进行设置。 对于得到的文件inode,根据打开选项进行相应处理。
	成功则通过调用参数返回inode指针。
	这个文件用得最多的功能函数莫过于namei();根据文件名返回i节点。
	这里任何对inode的操作都是通过iget,iput这类更底层的函数去实现，
	iget和iput所在的层次基于buffer管理和内存inode表的操作之上。
*/
#include <asm/segment.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>

#define ACC_MODE(x) ("\000\004\002\006"[(x)&O_ACCMODE])

/*
 * How long a filename can we get from user space?
 *  -EFAULT if invalid area
 *  0 if ok (ENAMETOOLONG before EFAULT)
 *  >0 EFAULT after xx bytes
 */
 //返回-EFAULT表示无效的用户地址空间
 //返回表示0表示很长的文件名
 //大于0表示可以获取的文件名字节数
static inline int get_max_filename(unsigned long address)
{
	struct vm_area_struct * vma;

	//取当前fs寄存器的值，如果其指向内核数据段
	//返回0
	if (get_fs() == KERNEL_DS)
		return 0;
	//找到文件名所在进程的虚拟地址空间
	vma = find_vma(current, address);
	//如果没有找到或者虚拟地址空间没有读标志，返回EFAULT，表示无效的用户地址空间
	if (!vma || vma->vm_start > address || !(vma->vm_flags & VM_READ))
		return -EFAULT;
	//将address转换为一个长度值
	address = vma->vm_end - address;
	//如果address大于一页物理内存字节数，返回0
	if (address > PAGE_SIZE)
		return 0;
	//此虚拟地址空间的下一个相邻的地址空间是否可以与之无缝结合
	if (vma->vm_next && vma->vm_next->vm_start == vma->vm_end &&
	   (vma->vm_next->vm_flags & VM_READ))
		return 0;
	return address;
}

/*
 * In order to reduce some races, while at the same time doing additional
 * checking and hopefully speeding things up, we copy filenames to the
 * kernel data space before using them..
 *
 * POSIX.1 2.4: an empty pathname is invalid (ENOENT).
 */
 
 //作用就是把用户态的字符串拷贝到内核空间中	参见Super.c中的fs_index函数
int getname(const char * filename, char **result)
{
	int i, error;
	unsigned long page;
	char * tmp, c;

	i = get_max_filename((unsigned long) filename);
	if (i < 0)
		return i;
	error = -EFAULT;
	if (!i) {
		error = -ENAMETOOLONG;
		i = PAGE_SIZE;
	}
	c = get_fs_byte(filename++);
	if (!c)
		return -ENOENT;
	if(!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	*result = tmp = (char *) page;
	while (--i) {
		*(tmp++) = c;
		c = get_fs_byte(filename++);
		if (!c) {
			*tmp = '\0';
			return 0;
		}
	}
	free_page(page);
	return error;
}

void putname(char * name)
{
	free_page((unsigned long) name);
}

/*
 *	permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * We use "fsuid" for this, letting us set arbitrary permissions
 * for filesystem access without changing the "normal" uids which
 * are used for other things..
 */
int permission(struct inode * inode,int mask)
{
	int mode = inode->i_mode;

	//如果文件i节点表明有自己的permission函数，则调用之
	if (inode->i_op && inode->i_op->permission)
		return inode->i_op->permission(inode, mask);
	else if ((mask & S_IWOTH) && IS_IMMUTABLE(inode))	//S_IWOTH:其他用户仅有写权限
		return -EACCES; /* Nobody gets write access to an immutable（不可改变的） file */
	else if (current->fsuid == inode->i_uid)
		mode >>= 6;
	else if (in_group_p(inode->i_gid))
		mode >>= 3;
	if (((mode & mask & 0007) == mask) || fsuser())
		return 0;
	return -EACCES;
}

/*
 * get_write_access() gets write permission for a file.
 * put_write_access() releases this write permission.
 * This is used for regular files.
 * We cannot support write (and maybe mmap read-write shared) accesses and
 * MAP_DENYWRITE mmappings simultaneously（同时）.
 */
 //获取对指定文件i节点的写访问权
int get_write_access(struct inode * inode)
{
	struct task_struct ** p;

	//如果文件被共享，并且是普通文件，那么遍历所有进程的虚拟地址空间
	//若存在某个进程的虚拟地址空间对应该文件的i节点并且其有VM_DENYWRITE
	//标志，则返回ETXTBSY
	if ((inode->i_count > 1) && S_ISREG(inode->i_mode)) /* shortcut（捷径） */
		for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		        struct vm_area_struct * mpnt;
			if (!*p)
				continue;
			for(mpnt = (*p)->mm->mmap; mpnt; mpnt = mpnt->vm_next) {
				if (inode != mpnt->vm_inode)
					continue;
				if (mpnt->vm_flags & VM_DENYWRITE)
					return -ETXTBSY;
			}
		}
	//执行到这里，说明文件没有被共享或者被共享了但没有进程将其映射设为VM_DENYWRITE
	//则增加文件的写进程数，然后返回0
	inode->i_wcount++;
	return 0;
}

void put_write_access(struct inode * inode)
{
	inode->i_wcount--;
}

/*
 * lookup() looks up one part of a pathname, using the fs-dependent
 * routines (currently minix_lookup) for it. It also checks for
 * fathers (pseudo-roots, mount-points)
 */
 //查找指定的目录dir下是否存在名称为name的目录（或文件）节点
 //len指明了name的长度，查找到的name节点是否存在于result中
int lookup(struct inode * dir,const char * name, int len,
	struct inode ** result)
{
	struct super_block * sb;
	int perm;

	*result = NULL;
	if (!dir)
		return -ENOENT;
/* check permissions before traversing mount-points */
	//检查对dir是否有执行权（因为对目录的访问操作视为一种执行操作）
	perm = permission(dir,MAY_EXEC);
	//如果name==".."(代表本目录的上级目录)
	//一下if之内的语句应该视为系统内部自动的转换，不需要检查权限
	if (len==2 && name[0] == '.' && name[1] == '.') {
		//如果本节点是根文件系统根目录
		if (dir == current->fs->root) {
			//将本目录节点返回，即根目录的父目录还是它本身
			*result = dir;
			return 0;
		  //sb->s_mounted指向被加载文件系统的根目录i节点
		  //dir_i->i_mount保存有sb->s_mounted
		  //sb->s_covered保存有dir_i
		  //如果文件i节点的超级块不为空，并且dir等于超级块的根目录i节点
		  //说明要lookup的dir是一个挂载点 因为非根文件系统的超级块必定挂载在某个目录节点下
		} else if ((sb = dir->i_sb) && (dir == sb->s_mounted)) {
			//取dir的超级块
			sb = dir->i_sb;
			iput(dir);	//释放dir
			dir = sb->s_covered;	//将dir赋值为该超级块根目录i节点的挂载点i节点
									//因为在lookup一个路径的时候，遍历到一个非根文件系统的文件系统时，需要替换为其挂载点才能
									//继续向上遍历 或者说其父目录是其挂载点
			if (!dir)	//如果dir为空，即超级块没有挂载到任何目录节点下 则返回ENOENT
				return -ENOENT;
			dir->i_count++;	//增加dir的引用计数
		}
	}
	//如果此目录节点没有对应的i节点操作集或者没有lookup方法
	if (!dir->i_op || !dir->i_op->lookup) {
		iput(dir);	//释放此dir
		return -ENOTDIR;
	}
	//如果没有相应的权限
 	if (perm != 0) {
		iput(dir);
		return perm;
	}
	//如果len为0，则代表调用本函数的函数传入的name是为空，则返回dir目录本身
	if (!len) {
		*result = dir;
		return 0;
	}
	return dir->i_op->lookup(dir,name,len,result);
}

int follow_link(struct inode * dir, struct inode * inode,
	int flag, int mode, struct inode ** res_inode)
{
	if (!dir || !inode) {
		iput(dir);
		iput(inode);
		*res_inode = NULL;
		return -ENOENT;
	}
	//如果具体的文件系统i节点没有对应的follow_link方法
	if (!inode->i_op || !inode->i_op->follow_link) {
		iput(dir);	//释放dir
		*res_inode = inode;	//将返回值设为inode
		return 0;
	}
	return inode->i_op->follow_link(dir,inode,flag,mode,res_inode);
}

/*
 *	dir_namei()
 *
 * dir_namei() returns the inode of the directory of the
 * specified name, and the name within that directory.
 */
 //本函数取得文件最后一层目录的i结点
 //对于"/a/b/c"，返回目录b的i节点 此时res_inode指向b，name是c的name
 //对于"/a/b/"，返回目录b的i节点	此时res_inode指向b，name为空
static int dir_namei(const char * pathname, int * namelen, const char ** name,
	struct inode * base, struct inode ** res_inode)
{
	char c;
	const char * thisname;
	int len,error;
	struct inode * inode;

	*res_inode = NULL;
	//如果base为空
	if (!base) {
		//则将base设置为当前进程的工作目录 代表相对路径
		base = current->fs->pwd;
		base->i_count++;	//增加当前进程的工作目录的i节点引用次数
	}
	//如果pathname以'/'开头，则说明是绝对路径
	if ((c = *pathname) == '/') {
		iput(base);
		base = current->fs->root;	//将base设置为根目录节点
		pathname++;
		base->i_count++;	//增加根目录节点的引用计数
	}
	//在while循环中解析文件路径字符串
	while (1) {
		thisname = pathname;	//将thisname指向当前要处理的目录名
		for(len=0;(c = *(pathname++))&&(c != '/');len++)
			/* nothing */ ;
		//若c为0，则代表处理到了路径中的最后一项 比如/a/b/ax
		//则此时thisname指向字符串ax len是ax的长度
		if (!c)
			break;
		base->i_count++;	/* lookup uses up base */
		//在目录base下查找名为thisname的目录节点，结果放在inode中
		//若原pathname的值形如"/a/b/c/"，则最后一次循环调用lookup()函数时
		//base=='c',thisname=='/'，len为0，并且函数lookup()将返回（通过inode）c的i节点本身
		//第一次循环时，base=='/'（代表根目录）,thisname=='a'，并且函数lookup()将返回（通过inode）a的i节点本身
		//其意义是在根目录下寻找名字为a的目录节点，并通过inode将a的i节点返回
		error = lookup(base,thisname,len,&inode);
		if (error) {
			iput(base);
			return error;
		}
		//在follow_link()函数中改变了base 正因为如此 函数可以沿着路径一直遍历下去
		//对于具体的文件系统，比如minix文件系统，由于其没有对应的follow_link方法
		//所以函数内部将base指向inode
		error = follow_link(base,inode,0,0,&base);
		if (error)
			return error;
	}
	if (!base->i_op || !base->i_op->lookup) {
		iput(base);
		return -ENOTDIR;
	}
	*name = thisname;
	*namelen = len;
	*res_inode = base;
	return 0;
}

//对于pathname="/a/b/c",res_inode指向c
//对于pathname="/a/b/",res_inode指向b
static int _namei(const char * pathname, struct inode * base,
	int follow_links, struct inode ** res_inode)
{
	const char * basename;
	int namelen,error;
	struct inode * inode;

	*res_inode = NULL;
	//取得文件最后一层目录的i结点
	//比如pathname="/a/b/c",则执行dir_namei()后，basename="c",namelen是basename的长度，base指向b
	//pathname="/a/b/",则执行dir_namei()后，basename为空,namelen是basename的长度(0)，base指向b
	error = dir_namei(pathname,&namelen,&basename,base,&base);
	if (error)
		return error;
	base->i_count++;	/* lookup uses up base */
	//在目录base下查找名为basename的目录节点，结果放在inode中
	//pathname="/a/b/c" 则在目录b下lookup文件c，将文件c的i节点存放在inode中
	//pathname="/a/b/" 则在b目录下lookup搜寻，由于搜寻的目标名称为空，所以inode返回目录b
	error = lookup(base,basename,namelen,&inode);
	if (error) {
		iput(base);
		return error;
	}
	if (follow_links) {
		error = follow_link(base,inode,0,0,&inode);
		if (error)
			return error;
	} else
		iput(base);
	*res_inode = inode;
	return 0;
}

int lnamei(const char * pathname, struct inode ** res_inode)
{
	int error;
	char * tmp;

	error = getname(pathname,&tmp);
	if (!error) {
		error = _namei(tmp,NULL,0,res_inode);
		putname(tmp);
	}
	return error;
}

/*
 *	namei()
 *
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 */
 //找出一个路径所指出的文件的i节点
 //对于pathname="/a/b/c",res_inode指向c
 //对于pathname="/a/b/",res_inode指向b
int namei(const char * pathname, struct inode ** res_inode)
{
	int error;
	char * tmp;

	error = getname(pathname,&tmp);
	if (!error) {
		error = _namei(tmp,NULL,1,res_inode);
		putname(tmp);
	}
	return error;
}
/*
				 linux同步机制之信号量（semaphore）down和up 
　　Linux内核的信号量在概念和原理上和用户态的System V的IPC机制信号量是相同的，不过他绝不可能在内核之外使用，
	因此他和System V的IPC机制信号量毫不相干。信号量在创建时需要设置一个初始值，表示同时能有几个任务能访问
	该信号量保护的共享资源，初始值为1就变成互斥锁（Mutex），即同时只能有一个任务能访问信号量保护的共享资源。 
　  一个任务要想访问共享资源，首先必须得到信号量，获取信号量的操作将把信号量的值减1，若当前信号量的值为负数，
	表明无法获得信号量，该任务必须挂起在 该信号量的等待队列等待该信号量可用；若当前信号量的值为非负数，
	表示能获得信号量，因而能即时访问被该信号量保护的共享资源。当任务访问完被信号量保护的共享资源后，必须释放
	信号量，释放信号量通过把信号量的值加1实现，如果信号量的值为非正数，表明有任务等待当前信号量，因此他也唤醒
	所有等待该信号量的任务。
	参见《现代操作系统》中对信号量的分析
*/

/*
 *	open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.
 *
 * Note that the low bits of "flag" aren't the same as in the open
 * system call - they are 00 - no permissions needed
 *			  01 - read permission needed
 *			  10 - write permission needed
 *			  11 - read/write permissions needed
 * which is a lot more logical, and also allows the "no perm" needed
 * for symlinks (where the permissions are checked later).
 */
int open_namei(const char * pathname, int flag, int mode,
	struct inode ** res_inode, struct inode * base)
{
	const char * basename;
	int namelen,error;
	struct inode * dir, *inode;

	mode &= S_IALLUGO & ~current->fs->umask;	//S_IALLUGO 具有读写属性
	mode |= S_IFREG;
	//取得文件最后一层目录的i结点
	error = dir_namei(pathname,&namelen,&basename,base,&dir);
	if (error)
		return error;
	if (!namelen) {			/* special case: '/usr/' etc */
		if (flag & 2) {	//10 - write permission needed
			iput(dir);
			return -EISDIR;
		}
		/* thanks to Paul Pluzhnikov for noticing this was missing.. */
		if ((error = permission(dir,ACC_MODE(flag))) != 0) {
			iput(dir);
			return error;
		}
		*res_inode=dir;
		return 0;
	}
	dir->i_count++;		/* lookup eats the dir */
	//如果要求内核创建文件
	if (flag & O_CREAT) {
		down(&dir->i_sem);
		//在dir目录下查找名字为basename的文件
		error = lookup(dir,basename,namelen,&inode);
		//如果文件已经存在
		if (!error) {
			if (flag & O_EXCL) {	//O_EXCL：以独占模式打开文件；若同时设置 O_EXCL 和 O_CREATE, 那么若文件已经存在，则打开操作会失败
				iput(inode);
				error = -EEXIST;
			}
		//往下的判断说明要创建的文件还不存在
		//检查对dir目录的执行权限，要求要有可写（代表可修改该目录节点以新建文件）可执行（代表可打开此目录节点）权限
		} else if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0)
			;	/* error is already set! */
		//往下的判断说明已经通过权限检查
		else if (!dir->i_op || !dir->i_op->create)
			error = -EACCES;
		//如果目录节点是只读的
		else if (IS_RDONLY(dir))
			error = -EROFS;
		//通过了所有检查，即以上的if...else if...语句判断都为假
		//则开始创建文件节点
		else {
			dir->i_count++;		/* create eats the dir */
			error = dir->i_op->create(dir,basename,namelen,mode,res_inode);
			up(&dir->i_sem);
			iput(dir);
			return error;
		}
		up(&dir->i_sem);
	//否则，不是创建文件，即是要打开已经存在的文件
	} else
		//在目录dir下查找名字为basename的文件节点，将查找结果存在inode中
		error = lookup(dir,basename,namelen,&inode);
	//如果没有找到
	if (error) {
		iput(dir);	//释放目录节点
		return error;
	}
	error = follow_link(dir,inode,flag,mode,&inode);
	if (error)
		return error;
	//如果打开的文件是目录并且要求写权限
	if (S_ISDIR(inode->i_mode) && (flag & 2)) {
		iput(inode);	//释放inode节点
		return -EISDIR;
	}
	if ((error = permission(inode,ACC_MODE(flag))) != 0) {
		iput(inode);
		return error;
	}
	//如果要打开的文件是块设备文件或者字符设备文件
	if (S_ISBLK(inode->i_mode) || S_ISCHR(inode->i_mode)) {
		//如果文件节点没有对应的设备存在
		if (IS_NODEV(inode)) {
			iput(inode);	//释放节点
			return -EACCES;
		}
		flag &= ~O_TRUNC;	//O_TRUNC:截断文件，若文件存在，则删除该文件
		//否则要打开的文件不是块设备文件也不是字符设备文件
	} else {
		//如果文件是只读的但是要求写权限
		if (IS_RDONLY(inode) && (flag & 2)) {
			iput(inode);
			return -EROFS;
		}
	}
	/*
	 * An append-only file must be opened in append mode for writing
	 */
	if (IS_APPEND(inode) && ((flag & 2) && !(flag & O_APPEND))) {
		iput(inode);
		return -EPERM;
	}
	//若要求清空文件内容
	if (flag & O_TRUNC) {
		struct iattr newattrs;

		if ((error = get_write_access(inode))) {
			iput(inode);
			return error;
		}
		newattrs.ia_size = 0;	//将i节点代表的文件大小设为0
		newattrs.ia_valid = ATTR_SIZE;	//这个标志代表newattrs要改变的是i节点的大小属性
		//通知内核改变i节点的属性，函数内部会做一些权限检查
		if ((error = notify_change(inode, &newattrs))) {
			put_write_access(inode);
			iput(inode);
			return error;
		}
		inode->i_size = 0;	//notify_change()函数内已经修改了i_size，但这里还要再做一次，可见内核开发者的严谨
		if (inode->i_op && inode->i_op->truncate)
			inode->i_op->truncate(inode);
		inode->i_dirt = 1;
		put_write_access(inode);
	}
	*res_inode = inode;
	return 0;
}
/*
	1.   mknod命令用于创建一个设备文件，即特殊文件
	2.   首先要明白什么是设备文件，简单的我们说 操作系统与外部设备（入磁盘驱动器，打印机，modern，终端 等等）
	都是通过设备文件来进行通信 的，在Unix/Linux系统与外部设备通讯之前，这个设备必须首先要有一个设备文件，
	设备文件均放在/dev目录下。一般情况下在安装系统的时候系统自动创建了很多已检测到的设备的设备文件，但有时候
	我们也需要自己手动创建，命令行生成设备文件的方式有 insf，mksf，mknod等等
*/
int do_mknod(const char * filename, int mode, dev_t dev)
{
	const char * basename;
	int namelen, error;
	struct inode * dir;

	mode &= ~current->fs->umask;
	error = dir_namei(filename,&namelen,&basename, NULL, &dir);
	if (error)
		return error;
	//之所以要验证namelen==0的情况是因为，mknod要创建一个设备文件
	//需要指出其所要创建的文件的名字，名字可以包含在路径中
	//比如存在一个路径/a/b/c,我们要在目录b下创建文件d
	//则要传参数/a/b/d 但不能传/a/b/(此时namelen为0，代表没有给出要创建的设备文件的名字)
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (IS_RDONLY(dir)) {
		iput(dir);
		return -EROFS;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(dir);
		return error;
	}
	if (!dir->i_op || !dir->i_op->mknod) {
		iput(dir);
		return -EPERM;
	}
	dir->i_count++;
	down(&dir->i_sem);
	error = dir->i_op->mknod(dir,basename,namelen,mode,dev);
	up(&dir->i_sem);
	iput(dir);
	return error;
}

//用来创建命名管道，实现linux的mkfifo命令
asmlinkage int sys_mknod(const char * filename, int mode, dev_t dev)
{
	int error;
	char * tmp;

	//如果要创建文件的类型是目录，或者不是fifo文件也不是超级用户
	if (S_ISDIR(mode) || (!S_ISFIFO(mode) && !fsuser()))
		return -EPERM;	//没有权限
	switch (mode & S_IFMT) {
	case 0:
		mode |= S_IFREG;
		break;
	case S_IFREG: case S_IFCHR: case S_IFBLK: case S_IFIFO:
		break;
	default:
		return -EINVAL;
	}
	error = getname(filename,&tmp);
	if (!error) {
		error = do_mknod(tmp,mode,dev);
		putname(tmp);
	}
	return error;
}

static int do_mkdir(const char * pathname, int mode)
{
	const char * basename;
	int namelen, error;
	struct inode * dir;

	error = dir_namei(pathname,&namelen,&basename,NULL,&dir);
	if (error)
		return error;
	//之所以要验证namelen==0的情况是因为，mkdir要创建一个目录
	//需要指出其所要创建的文件的名字，名字可以包含在路径中
	//比如存在一个路径/a/b/c,我们要在目录b下创建目录d
	//则要传参数/a/b/d 但不能传/a/b/(此时namelen为0，即没有指出要创建的文件)
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (IS_RDONLY(dir)) {
		iput(dir);
		return -EROFS;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(dir);
		return error;
	}
	if (!dir->i_op || !dir->i_op->mkdir) {
		iput(dir);
		return -EPERM;
	}
	dir->i_count++;
	down(&dir->i_sem);
	error = dir->i_op->mkdir(dir, basename, namelen, mode & 0777 & ~current->fs->umask);
	up(&dir->i_sem);
	iput(dir);
	return error;
}

asmlinkage int sys_mkdir(const char * pathname, int mode)
{
	int error;
	char * tmp;

	error = getname(pathname,&tmp);
	if (!error) {
		error = do_mkdir(tmp,mode);
		putname(tmp);
	}
	return error;
}

static int do_rmdir(const char * name)
{
	const char * basename;
	int namelen, error;
	struct inode * dir;

	error = dir_namei(name,&namelen,&basename,NULL,&dir);
	if (error)
		return error;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (IS_RDONLY(dir)) {
		iput(dir);
		return -EROFS;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(dir);
		return error;
	}
	/*
	 * A subdirectory cannot be removed from an append-only directory
	 */
	if (IS_APPEND(dir)) {
		iput(dir);
		return -EPERM;
	}
	if (!dir->i_op || !dir->i_op->rmdir) {
		iput(dir);
		return -EPERM;
	}
	return dir->i_op->rmdir(dir,basename,namelen);
}

asmlinkage int sys_rmdir(const char * pathname)
{
	int error;
	char * tmp;

	error = getname(pathname,&tmp);
	if (!error) {
		error = do_rmdir(tmp);
		putname(tmp);
	}
	return error;
}

static int do_unlink(const char * name)
{
	const char * basename;
	int namelen, error;
	struct inode * dir;

	error = dir_namei(name,&namelen,&basename,NULL,&dir);
	if (error)
		return error;
	if (!namelen) {
		iput(dir);
		return -EPERM;
	}
	if (IS_RDONLY(dir)) {
		iput(dir);
		return -EROFS;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(dir);
		return error;
	}
	/*
	 * A file cannot be removed from an append-only directory
	 */
	if (IS_APPEND(dir)) {
		iput(dir);
		return -EPERM;
	}
	if (!dir->i_op || !dir->i_op->unlink) {
		iput(dir);
		return -EPERM;
	}
	return dir->i_op->unlink(dir,basename,namelen);
}

asmlinkage int sys_unlink(const char * pathname)
{
	int error;
	char * tmp;

	error = getname(pathname,&tmp);
	if (!error) {
		error = do_unlink(tmp);
		putname(tmp);
	}
	return error;
}

static int do_symlink(const char * oldname, const char * newname)
{
	struct inode * dir;
	const char * basename;
	int namelen, error;

	error = dir_namei(newname,&namelen,&basename,NULL,&dir);
	if (error)
		return error;
	if (!namelen) {
		iput(dir);
		return -ENOENT;
	}
	if (IS_RDONLY(dir)) {
		iput(dir);
		return -EROFS;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(dir);
		return error;
	}
	if (!dir->i_op || !dir->i_op->symlink) {
		iput(dir);
		return -EPERM;
	}
	dir->i_count++;
	down(&dir->i_sem);
	error = dir->i_op->symlink(dir,basename,namelen,oldname);
	up(&dir->i_sem);
	iput(dir);
	return error;
}

asmlinkage int sys_symlink(const char * oldname, const char * newname)
{
	int error;
	char * from, * to;

	error = getname(oldname,&from);
	if (!error) {
		error = getname(newname,&to);
		if (!error) {
			error = do_symlink(from,to);
			putname(to);
		}
		putname(from);
	}
	return error;
}

static int do_link(struct inode * oldinode, const char * newname)
{
	struct inode * dir;
	const char * basename;
	int namelen, error;

	error = dir_namei(newname,&namelen,&basename,NULL,&dir);
	if (error) {
		iput(oldinode);
		return error;
	}
	if (!namelen) {
		iput(oldinode);
		iput(dir);
		return -EPERM;
	}
	if (IS_RDONLY(dir)) {
		iput(oldinode);
		iput(dir);
		return -EROFS;
	}
	if (dir->i_dev != oldinode->i_dev) {
		iput(dir);
		iput(oldinode);
		return -EXDEV;
	}
	if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(dir);
		iput(oldinode);
		return error;
	}
	/*
	 * A link to an append-only or immutable file cannot be created
	 */
	if (IS_APPEND(oldinode) || IS_IMMUTABLE(oldinode)) {
		iput(dir);
		iput(oldinode);
		return -EPERM;
	}
	if (!dir->i_op || !dir->i_op->link) {
		iput(dir);
		iput(oldinode);
		return -EPERM;
	}
	dir->i_count++;
	down(&dir->i_sem);
	error = dir->i_op->link(oldinode, dir, basename, namelen);
	up(&dir->i_sem);
	iput(dir);
	return error;
}

asmlinkage int sys_link(const char * oldname, const char * newname)
{
	int error;
	char * to;
	struct inode * oldinode;

	error = namei(oldname, &oldinode);
	if (error)
		return error;
	error = getname(newname,&to);
	if (error) {
		iput(oldinode);
		return error;
	}
	error = do_link(oldinode,to);
	putname(to);
	return error;
}

//不仅可以重命名文件，还可以移动文件
static int do_rename(const char * oldname, const char * newname)
{
	struct inode * old_dir, * new_dir;
	const char * old_base, * new_base;
	int old_len, new_len, error;

	//找到文件的最后一层目录 函数执行后，old_base指向文件name，old_len是文件name长度，old_dir是最后一层目录i节点
	error = dir_namei(oldname,&old_len,&old_base,NULL,&old_dir);
	if (error)
		return error;
	//要修改文件名，就必须对文件的父目录具有可写可执行权限
	//因为文件i节点是其父目录"文件"中的内容
	//比如"/a/b/v" 要修改v的名字，则必须对目录b具有可写可执行权限
	//因为文件v的i节点是保存在"文件"b中的
	if ((error = permission(old_dir,MAY_WRITE | MAY_EXEC)) != 0) {
		iput(old_dir);
		return error;
	}
	//若old_len为0，即没有指定要修改名字的文件名字;
	//或者要修改的文件名为"."或".."
	if (!old_len || (old_base[0] == '.' &&
	    (old_len == 1 || (old_base[1] == '.' &&
	     old_len == 2)))) {
		iput(old_dir);
		return -EPERM;
	}
	error = dir_namei(newname,&new_len,&new_base,NULL,&new_dir);
	if (error) {
		iput(old_dir);
		return error;
	}
	if ((error = permission(new_dir,MAY_WRITE | MAY_EXEC)) != 0){
		iput(old_dir);
		iput(new_dir);
		return error;
	}
	if (!new_len || (new_base[0] == '.' &&
	    (new_len == 1 || (new_base[1] == '.' &&
	     new_len == 2)))) {
		iput(old_dir);
		iput(new_dir);
		return -EPERM;
	}
	if (new_dir->i_dev != old_dir->i_dev) {
		iput(old_dir);
		iput(new_dir);
		return -EXDEV;
	}
	if (IS_RDONLY(new_dir) || IS_RDONLY(old_dir)) {
		iput(old_dir);
		iput(new_dir);
		return -EROFS;
	}
	/*
	 * A file cannot be removed from an append-only directory
	 */
	if (IS_APPEND(old_dir)) {
		iput(old_dir);
		iput(new_dir);
		return -EPERM;
	}
	if (!old_dir->i_op || !old_dir->i_op->rename) {
		iput(old_dir);
		iput(new_dir);
		return -EPERM;
	}
	new_dir->i_count++;
	down(&new_dir->i_sem);
	error = old_dir->i_op->rename(old_dir, old_base, old_len, 
		new_dir, new_base, new_len);
	up(&new_dir->i_sem);
	iput(new_dir);
	return error;
}

asmlinkage int sys_rename(const char * oldname, const char * newname)
{
	int error;
	char * from, * to;

	error = getname(oldname,&from);
	if (!error) {
		error = getname(newname,&to);
		if (!error) {
			error = do_rename(from,to);
			putname(to);
		}
		putname(from);
	}
	return error;
}
