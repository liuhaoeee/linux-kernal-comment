/*
 *  linux/fs/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
 //Linux操作系统内核就是这样 曾经风云变幻 到最后却被千万行的代码淹没的无影无踪  
//幸好 他有时又会留下一点蛛丝马迹 引领我们穿越操作系统内部所有的秘密
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
					索引节点对象
	索引节点对象存储了文件的相关信息，代表了存储设备上的一个实际的物理文件。当一个 
	文件首次被访问时，内核会在内存中组装相应的索引节点对象，以便向内核提供对一个文件进行操 
	作时所必需的全部信息；这些信息一部分存储在磁盘特定位置，另外一部分是在加载时动态填充的。
*/

#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>

#include <asm/system.h>

static struct inode_hash_entry {
	struct inode * inode;
	int updating;
} hash_table[NR_IHASH];

static struct inode * first_inode;
static struct wait_queue * inode_wait = NULL;
static int nr_inodes = 0, nr_free_inodes = 0;

static inline int const hashfn(dev_t dev, unsigned int i)
{
	return (dev ^ i) % NR_IHASH;
}

static inline struct inode_hash_entry * const hash(dev_t dev, int i)
{
	return hash_table + hashfn(dev, i);
}

//将一个inode插入到first_inode所指向的链表头部
static void insert_inode_free(struct inode *inode)
{
	inode->i_next = first_inode;
	inode->i_prev = first_inode->i_prev;
	inode->i_next->i_prev = inode;
	inode->i_prev->i_next = inode;
	first_inode = inode;
}

//将指定的inode节点从first_inode所指向的链表中删除
static void remove_inode_free(struct inode *inode)
{
	if (first_inode == inode)
		first_inode = first_inode->i_next;
	if (inode->i_next)
		inode->i_next->i_prev = inode->i_prev;
	if (inode->i_prev)
		inode->i_prev->i_next = inode->i_next;
	inode->i_next = inode->i_prev = NULL;
}

//将inode插入到hash表中
void insert_inode_hash(struct inode *inode)
{
	struct inode_hash_entry *h;
	h = hash(inode->i_dev, inode->i_ino);

	inode->i_hash_next = h->inode;
	inode->i_hash_prev = NULL;	//双向但不循环链表
	//如果此hash链表不为空
	if (inode->i_hash_next)
		inode->i_hash_next->i_hash_prev = inode;
	h->inode = inode;
}

//将inode从hash表中移除
static void remove_inode_hash(struct inode *inode)
{
	struct inode_hash_entry *h;
	//找到对应的hash表项头
	h = hash(inode->i_dev, inode->i_ino);

	//如果要移除的inode是此hash表项链表头，则直接将此hash结构指向链表中下一项
	if (h->inode == inode)
		h->inode = inode->i_hash_next;
	if (inode->i_hash_next)
		inode->i_hash_next->i_hash_prev = inode->i_hash_prev;
	if (inode->i_hash_prev)
		inode->i_hash_prev->i_hash_next = inode->i_hash_next;
	inode->i_hash_prev = inode->i_hash_next = NULL;
}

//将指定的inode移到first_inode所指向的链表尾部，即first_inode之前
static void put_last_free(struct inode *inode)
{
	remove_inode_free(inode);	//现将此inode从链表中移除
	inode->i_prev = first_inode->i_prev;
	inode->i_prev->i_next = inode;
	inode->i_next = first_inode;
	inode->i_next->i_prev = inode;
}

//增加内核中的inode结构数
void grow_inodes(void)
{
	struct inode * inode;
	int i;

	//先申请一页空闲物理内存
	if (!(inode = (struct inode*) get_free_page(GFP_KERNEL)))
		return;

	//一页内存所能容纳的inode数
	i=PAGE_SIZE / sizeof(struct inode);
	nr_inodes += i;	//增加当前内核中的inode总数
	nr_free_inodes += i;	//增加当前内核中的空闲inode总数

	//如果first_inode链表为空，则将此也内存中的第一个inode链入first_inode链表（只用于首次初始化）
	if (!first_inode)
		inode->i_next = inode->i_prev = first_inode = inode++, i--;
	
	//将其余的inode插入链表
	for ( ; i ; i-- )
		insert_inode_free(inode++);
}

unsigned long inode_init(unsigned long start, unsigned long end)
{
	memset(hash_table, 0, sizeof(hash_table));
	first_inode = NULL;
	return start;
}

static void __wait_on_inode(struct inode *);

static inline void wait_on_inode(struct inode * inode)
{
	if (inode->i_lock)
		__wait_on_inode(inode);
}

static inline void lock_inode(struct inode * inode)
{
	wait_on_inode(inode);
	inode->i_lock = 1;
}

static inline void unlock_inode(struct inode * inode)
{
	inode->i_lock = 0;
	wake_up(&inode->i_wait);
}

/*
 * Note that we don't want to disturb any wait-queues when we discard
 * an inode.
 *
 * Argghh. Got bitten by a gcc problem with inlining: no way to tell
 * the compiler that the inline asm function 'memset' changes 'inode'.
 * I've been searching for the bug for days, and was getting desperate.
 * Finally looked at the assembler output... Grrr.
 *
 * The solution is the weird use of 'volatile'. Ho humm. Have to report
 * it to the gcc lists, and hope we can do this more cleanly some day..
 */
 /*
	 volatile提醒编译器它后面所定义的变量随时都有可能改变，因此编译后的程序每次需要存储或读取这个变量的时候，
	 都会直接从变量地址中读取数据。如果没有volatile关键字，则编译器可能优化读取和存储，可能暂时使用寄存器中
	 的值，如果这个变量由别的程序更新了的话，将出现不一致的现象。下面举例说明。在DSP开发中，经常需要等待某个
	 事件的触发，所以经常会写出这样的程序：
	short flag;
	void test()
	{
		do1();
		while(flag==0);
		do2();
	}
	这段程序等待内存变量flag的值变为1(怀疑此处是0,有点疑问,)之后才运行do2()。变量flag的值由别的程序更改，
	这个程序可能是某个硬件中断服务程序。例如：如果某个按钮按下的话，就会对DSP产生中断，在按键中断程序中修改flag为1，
	这样上面的程序就能够得以继续运行。但是，编译器并不知道flag的值会被别的程序修改，因此在它进行优化的时候，
	可能会把flag的值先读入某个寄存器，然后等待那个寄存器变为1。如果不幸进行了这样的优化，那么while循环就变成
	了死循环，因为寄存器的内容不可能被中断服务程序修改。为了让程序每次都读取真正flag变量的值，就需要定义为如下形式：
	volatile short flag;需要注意的是，没有volatile也可能能正常运行，但是可能修改了编译器的优化级别之后就又不能正常运行了。
	因此经常会出现debug版本正常，但是release版本却不能正常的问题。所以为了安全起见，只要是等待别的程序修改某个变量的话，
	就加上volatile关键字。
*/
 /* VFS调用该函数释放索引节点，并清空包含相关数据的所有页面 */
void clear_inode(struct inode * inode)
{
	struct wait_queue * wait;

	wait_on_inode(inode);
	remove_inode_hash(inode);
	remove_inode_free(inode);
	wait = ((volatile struct inode *) inode)->i_wait;
	//如果i_count非零，则说明释放节点之前，此节点被占用，则释放后，空闲节点加一
	if (inode->i_count)
		nr_free_inodes++;
	//清空包含相关数据的所有页面
	memset(inode,0,sizeof(*inode));
	((volatile struct inode *) inode)->i_wait = wait;
	insert_inode_free(inode);
}

int fs_may_mount(dev_t dev)
{
	struct inode * inode, * next;
	int i;

	next = first_inode;
	for (i = nr_inodes ; i > 0 ; i--) {
		inode = next;
		next = inode->i_next;	/* clear_inode() changes the queues.. */
		if (inode->i_dev != dev)
			continue;
		if (inode->i_count || inode->i_dirt || inode->i_lock)
			return 0;
		clear_inode(inode);
	}
	return 1;
}

int fs_may_umount(dev_t dev, struct inode * mount_root)
{
	struct inode * inode;
	int i;

	inode = first_inode;
	for (i=0 ; i < nr_inodes ; i++, inode = inode->i_next) {
		if (inode->i_dev != dev || !inode->i_count)
			continue;
		if (inode == mount_root && inode->i_count == 1)
			continue;
		return 0;
	}
	return 1;
}

int fs_may_remount_ro(dev_t dev)
{
	struct file * file;
	int i;

	/* Check that no files are currently opened for writing. */
	for (file = first_file, i=0; i<nr_files; i++, file=file->f_next) {
		if (!file->f_count || !file->f_inode ||
		    file->f_inode->i_dev != dev)
			continue;
		if (S_ISREG(file->f_inode->i_mode) && (file->f_mode & 2))
			return 0;
	}
	return 1;
}

//将inode节点信息写回磁盘
static void write_inode(struct inode * inode)
{
	//如果i节点不是脏的，表示其内容和磁盘内容一致，不必写回磁盘
	if (!inode->i_dirt)
		return;
	//等待i节点解锁
	wait_on_inode(inode);
	//因为存在竞争条件，即wait_on_inode(inode)期间可能睡眠，在睡眠期间...	
	if (!inode->i_dirt)
		return;
	if (!inode->i_sb || !inode->i_sb->s_op || !inode->i_sb->s_op->write_inode) {
		inode->i_dirt = 0;
		return;
	}
	inode->i_lock = 1;		//加锁
	inode->i_sb->s_op->write_inode(inode);	//VFS调用具体的文件系统例程
	unlock_inode(inode);	//解锁
}

//从磁盘读入指定的inode结构信息至内存
static void read_inode(struct inode * inode)
{
	lock_inode(inode);	//加锁
	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->read_inode)
		inode->i_sb->s_op->read_inode(inode);
	unlock_inode(inode);	//解锁
}

/* POSIX UID/GID verification for setting inode attributes */
//验证是否有相应的权限来改变属性
int inode_change_ok(struct inode *inode, struct iattr *attr)
{
	/* Make sure a caller can chown */
	if ((attr->ia_valid & ATTR_UID) &&
	    (current->fsuid != inode->i_uid ||
	     attr->ia_uid != inode->i_uid) && !fsuser())
		return -EPERM;

	/* Make sure caller can chgrp */
	if ((attr->ia_valid & ATTR_GID) &&
	    (!in_group_p(attr->ia_gid) && attr->ia_gid != inode->i_gid) &&
	    !fsuser())
		return -EPERM;

	/* Make sure a caller can chmod */
	if (attr->ia_valid & ATTR_MODE) {
		if ((current->fsuid != inode->i_uid) && !fsuser())
			return -EPERM;
		/* Also check the setgid bit! */
		if (!fsuser() && !in_group_p((attr->ia_valid & ATTR_GID) ? attr->ia_gid :
					     inode->i_gid))
			attr->ia_mode &= ~S_ISGID;
	}

	/* Check for setting the inode time */
	if ((attr->ia_valid & ATTR_ATIME_SET) &&
	    ((current->fsuid != inode->i_uid) && !fsuser()))
		return -EPERM;
	if ((attr->ia_valid & ATTR_MTIME_SET) &&
	    ((current->fsuid != inode->i_uid) && !fsuser()))
		return -EPERM;


	return 0;
}

/*
 * Set the appropriate attributes from an attribute structure into
 * the inode structure.
 */
void inode_setattr(struct inode *inode, struct iattr *attr)
{
	if (attr->ia_valid & ATTR_UID)
		inode->i_uid = attr->ia_uid;
	if (attr->ia_valid & ATTR_GID)
		inode->i_gid = attr->ia_gid;
	if (attr->ia_valid & ATTR_SIZE)
		inode->i_size = attr->ia_size;
	if (attr->ia_valid & ATTR_ATIME)
		inode->i_atime = attr->ia_atime;
	if (attr->ia_valid & ATTR_MTIME)
		inode->i_mtime = attr->ia_mtime;
	if (attr->ia_valid & ATTR_CTIME)
		inode->i_ctime = attr->ia_ctime;
	if (attr->ia_valid & ATTR_MODE) {
		inode->i_mode = attr->ia_mode;
		if (!fsuser() && !in_group_p(inode->i_gid))
			inode->i_mode &= ~S_ISGID;
	}
	inode->i_dirt = 1;
}

/*
 * notify_change is called for inode-changing operations such as
 * chown, chmod, utime, and truncate.  It is guaranteed (unlike
 * write_inode) to be called from the context of the user requesting
 * the change.  It is not called for ordinary access-time updates.
 * NFS uses this to get the authentication correct.  -- jrs
 */
 /*
	notify_change：当一个索引节点的属性被改变时，会调用该函数。
	它的参数struct iattr *指向一个新的属性组。如果一个文件系统没有定义该方法(即NULL)，
	则VFS会调用例程fs/iattr.c:inode_change_ok，该方 法实现了一个符合POSIX标准的属性检验，
	然后VFS会将该索引节点标记为“脏”。如果一个文件系统实现了自己的notify_change方法，则应 
	该在改变属性后显式地调用mark_inode_dirty(inode)方法。
 */

int notify_change(struct inode * inode, struct iattr *attr)
{
	int retval;

	if (inode->i_sb && inode->i_sb->s_op  &&
	    inode->i_sb->s_op->notify_change) 
		return inode->i_sb->s_op->notify_change(inode, attr);
	
	//权限不够，则返回
	if ((retval = inode_change_ok(inode, attr)) != 0)
		return retval;

	inode_setattr(inode, attr);
	return 0;
}

/*
 * bmap is needed for demand-loading and paging: if this function
 * doesn't exist for a filesystem, then those things are impossible:
 * executables cannot be run from the filesystem etc...
 *
 * This isn't as bad as it sounds: the read-routines might still work,
 * so the filesystem would be otherwise ok (for example, you might have
 * a DOS filesystem, which doesn't lend itself to bmap very well, but
 * you could still transfer files to/from the filesystem)
 */
int bmap(struct inode * inode, int block)
{
	if (inode->i_op && inode->i_op->bmap)
		return inode->i_op->bmap(inode,block);
	return 0;
}

//使指定设备的inode无效
void invalidate_inodes(dev_t dev)
{
	struct inode * inode, * next;
	int i;

	next = first_inode;
	for(i = nr_inodes ; i > 0 ; i--) {
		inode = next;
		next = inode->i_next;		/* clear_inode() changes the queues.. */
		if (inode->i_dev != dev)
			continue;
		if (inode->i_count || inode->i_dirt || inode->i_lock) {
			printk("VFS: inode busy on removed device %d/%d\n", MAJOR(dev), MINOR(dev));
			continue;
		}
		clear_inode(inode);
	}
}

//同步指定设备的inode
void sync_inodes(dev_t dev)
{
	int i;
	struct inode * inode;

	inode = first_inode;
	//遍历两次 why？
	for(i = 0; i < nr_inodes*2; i++, inode = inode->i_next) {
		if (dev && inode->i_dev != dev)
			continue;
		wait_on_inode(inode);
		//如果inode脏了，写盘、、同步
		if (inode->i_dirt)
			write_inode(inode);
	}
}

//释放inode结构
void iput(struct inode * inode)
{
	if (!inode)
		return;
	wait_on_inode(inode);
	if (!inode->i_count) {
		printk("VFS: iput: trying to free free inode\n");
		printk("VFS: device %d/%d, inode %lu, mode=0%07o\n",
			MAJOR(inode->i_rdev), MINOR(inode->i_rdev),
					inode->i_ino, inode->i_mode);
		return;
	}
	if (inode->i_pipe)
		wake_up_interruptible(&PIPE_WAIT(*inode));
repeat:
	//如果inode被引用多次，则减少一次引用，返回
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}
	wake_up(&inode_wait);
	if (inode->i_pipe) {
		unsigned long page = (unsigned long) PIPE_BASE(*inode);
		PIPE_BASE(*inode) = NULL;
		free_page(page);
	}
	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->put_inode) {
		inode->i_sb->s_op->put_inode(inode);
		if (!inode->i_nlink)
			return;
	}
	if (inode->i_dirt) {
		write_inode(inode);	/* we can sleep - so do again */
		wait_on_inode(inode);
		goto repeat;
	}
	inode->i_count--;
	if (inode->i_mmap) {
		printk("iput: inode %lu on device %d/%d still has mappings.\n",
			inode->i_ino, MAJOR(inode->i_dev), MINOR(inode->i_dev));
		inode->i_mmap = NULL;
	}
	nr_free_inodes++;
	return;
}

struct inode * get_empty_inode(void)
{
	struct inode * inode, * best;
	int i;
	//如果到了这个界限，需要增加inode节点
	if (nr_inodes < NR_INODE && nr_free_inodes < (nr_inodes >> 2))
		grow_inodes();
repeat:
	inode = first_inode;
	best = NULL;
	for (i = 0; i<nr_inodes; inode = inode->i_next, i++) {
		if (!inode->i_count) {
			if (!best)
				best = inode;
			if (!inode->i_dirt && !inode->i_lock) {
				best = inode;
				break;
			}
		}
	}
	if (!best || best->i_dirt || best->i_lock)
		if (nr_inodes < NR_INODE) {
			grow_inodes();
			goto repeat;
		}
	inode = best;
	if (!inode) {
		printk("VFS: No free inodes - contact Linus\n");
		sleep_on(&inode_wait);
		goto repeat;
	}
	if (inode->i_lock) {
		wait_on_inode(inode);
		goto repeat;
	}
	if (inode->i_dirt) {
		write_inode(inode);
		goto repeat;
	}
	if (inode->i_count)
		goto repeat;
	clear_inode(inode);
	inode->i_count = 1;
	inode->i_nlink = 1;
	inode->i_version = ++event;
	inode->i_sem.count = 1;
	nr_free_inodes--;
	if (nr_free_inodes < 0) {
		printk ("VFS: get_empty_inode: bad free inode count.\n");
		nr_free_inodes = 0;
	}
	return inode;
}

struct inode * get_pipe_inode(void)
{
	struct inode * inode;
	extern struct inode_operations pipe_inode_operations;

	if (!(inode = get_empty_inode()))
		return NULL;
	if (!(PIPE_BASE(*inode) = (char*) __get_free_page(GFP_USER))) {
		iput(inode);
		return NULL;
	}
	inode->i_op = &pipe_inode_operations;
	inode->i_count = 2;	/* sum of readers/writers */
	PIPE_WAIT(*inode) = NULL;
	PIPE_START(*inode) = PIPE_LEN(*inode) = 0;
	PIPE_RD_OPENERS(*inode) = PIPE_WR_OPENERS(*inode) = 0;
	PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 1;
	PIPE_LOCK(*inode) = 0;
	inode->i_pipe = 1;
	inode->i_mode |= S_IFIFO | S_IRUSR | S_IWUSR;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_blksize = PAGE_SIZE;
	return inode;
}

struct inode * __iget(struct super_block * sb, int nr, int crossmntp)
{
	static struct wait_queue * update_wait = NULL;
	struct inode_hash_entry * h;
	struct inode * inode;
	struct inode * empty = NULL;

	if (!sb)
		panic("VFS: iget with sb==NULL");
	//由此可见，hash表中存储的是和磁盘存储信息相关的有效的inode节点
	h = hash(sb->s_dev, nr);
repeat:
	for (inode = h->inode; inode ; inode = inode->i_hash_next)
		if (inode->i_dev == sb->s_dev && inode->i_ino == nr)
			goto found_it;
	if (!empty) {
		h->updating++;
		empty = get_empty_inode();
		if (!--h->updating)
			wake_up(&update_wait);
		if (empty)
			goto repeat;
		return (NULL);
	}
	//执行到这里说明没有在hash表中找到相关的inode，而是重新得到了一项空的inode
	//则填充其相关信息
	inode = empty;
	inode->i_sb = sb;
	inode->i_dev = sb->s_dev;
	inode->i_ino = nr;
	inode->i_flags = sb->s_flags;
	put_last_free(inode);		//将inode节点移到first_inode尾部
	insert_inode_hash(inode);	//将新的inode插入相应的hash表项
	read_inode(inode);	//从磁盘读取相关信息，由此可见，hash表中的inode有高速缓冲之意
	goto return_it;

found_it:
	if (!inode->i_count)
		nr_free_inodes--;
	inode->i_count++;
	wait_on_inode(inode);
	if (inode->i_dev != sb->s_dev || inode->i_ino != nr) {
		printk("Whee.. inode changed from under us. Tell Linus\n");
		iput(inode);
		goto repeat;
	}
	if (crossmntp && inode->i_mount) {
		struct inode * tmp = inode->i_mount;
		tmp->i_count++;
		iput(inode);
		inode = tmp;
		wait_on_inode(inode);
	}
	if (empty)
		iput(empty);

return_it:
	while (h->updating)
		sleep_on(&update_wait);
	return inode;
}

/*
 * The "new" scheduling primitives (new as of 0.97 or so) allow this to
 * be done without disabling interrupts (other than in the actual queue
 * updating things: only a couple of 386 instructions). This should be
 * much better for interrupt latency.
 */
static void __wait_on_inode(struct inode * inode)
{
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&inode->i_wait, &wait);
repeat:
	current->state = TASK_UNINTERRUPTIBLE;
	if (inode->i_lock) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&inode->i_wait, &wait);
	current->state = TASK_RUNNING;
}
