#ifndef _LINUX_FS_H
#define _LINUX_FS_H

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
 * This file has definitions for some important file table
 * structures etc.
 */

#include <linux/linkage.h>
#include <linux/limits.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/vfs.h>
#include <linux/net.h>

/*
 * It's silly to have NR_OPEN bigger than NR_FILE, but I'll fix
 * that later. Anyway, now the file code is no longer dependent
 * on bitmaps in unsigned longs, but uses the new fd_set structure..
 *
 * Some programs (notably those using select()) may have to be 
 * recompiled to take full advantage of the new limits..
 */
#undef NR_OPEN
#define NR_OPEN 256

#define NR_INODE 2048	/* this should be bigger than NR_FILE */
#define NR_FILE 1024	/* this can well be larger on a larger system */
#define NR_SUPER 32
#define NR_IHASH 131
#define BLOCK_SIZE 1024
#define BLOCK_SIZE_BITS 10

#define MAY_EXEC 1
#define MAY_WRITE 2
#define MAY_READ 4

#define READ 0
#define WRITE 1
#define READA 2		/* read-ahead - don't pause */
#define WRITEA 3	/* "write-ahead" - silly, but somewhat useful */

extern void buffer_init(void);
extern unsigned long inode_init(unsigned long start, unsigned long end);
extern unsigned long file_table_init(unsigned long start, unsigned long end);
extern unsigned long name_cache_init(unsigned long start, unsigned long end);

/*
	由于对设备号的总共就16个字节，也就是一个短整型，那么一个系统中所能拥有的设备号就是及其有限的了。
	从中我们可以看出，他是以8为为分界线，高8位为主设备号，低8位为次设备号，那么一个8位所能表示的最
	多也即是255个数值，那么当我们系统中如果拥有的设备大于这个数值的时候，在老版本的内核中就没有办法处理了。
	参见ide.c中对PARTN_BITS的注释
*/
#define MAJOR(a) (int)((unsigned short)(a) >> 8)
#define MINOR(a) (int)((unsigned short)(a) & 0xFF)
#define MKDEV(a,b) ((int)((((a) & 0xff) << 8) | ((b) & 0xff)))
#define NODEV MKDEV(0,0)

#ifndef NULL
#define NULL ((void *) 0)
#endif

#define NIL_FILP	((struct file *)0)
#define SEL_IN		1
#define SEL_OUT		2
#define SEL_EX		4

/*
 * These are the fs-independent mount-flags: up to 16 flags are supported
 */
#define MS_RDONLY	 1 /* mount read-only */
#define MS_NOSUID	 2 /* ignore suid and sgid bits */
#define MS_NODEV	 4 /* disallow access to device special files */
#define MS_NOEXEC	 8 /* disallow program execution */
#define MS_SYNCHRONOUS	16 /* writes are synced at once */
#define MS_REMOUNT	32 /* alter flags of a mounted FS */

#define S_APPEND    256 /* append-only file */
#define S_IMMUTABLE 512 /* immutable file */

/*
 * Flags that can be altered by MS_REMOUNT
 */
#define MS_RMT_MASK (MS_RDONLY)

/*
 * Magic mount flag number. Has to be or-ed to the flag values.
 */
#define MS_MGC_VAL 0xC0ED0000 /* magic flag number to indicate "new" flags */
#define MS_MGC_MSK 0xffff0000 /* magic flag number mask */

/*
 * Note that read-only etc flags are inode-specific: setting some file-system
 * flags just means all the inodes inherit those flags by default. It might be
 * possible to override it selectively if you really wanted to with some
 * ioctl() that is not currently implemented.
 *
 * Exception: MS_RDONLY is always applied to the entire file system.
 */
#define IS_RDONLY(inode) (((inode)->i_sb) && ((inode)->i_sb->s_flags & MS_RDONLY))
#define IS_NOSUID(inode) ((inode)->i_flags & MS_NOSUID)	//执行程序时，不遵照set-user-ID和set-group-ID位
#define IS_NODEV(inode) ((inode)->i_flags & MS_NODEV)
#define IS_NOEXEC(inode) ((inode)->i_flags & MS_NOEXEC)	//不允许在挂上的文件系统上执行程序
#define IS_SYNC(inode) ((inode)->i_flags & MS_SYNCHRONOUS)

#define IS_APPEND(inode) ((inode)->i_flags & S_APPEND)
#define IS_IMMUTABLE(inode) ((inode)->i_flags & S_IMMUTABLE)

/* the read-only stuff doesn't really belong here, but any other place is
   probably as bad and I don't want to create yet another include file. */

#define BLKROSET 4701 /* set device read-only (0 = read-write) */
#define BLKROGET 4702 /* get read-only status (0 = read_write) */
#define BLKRRPART 4703 /* re-read partition table */
#define BLKGETSIZE 4704 /* return device size */
#define BLKFLSBUF 4705 /* flush buffer cache */
#define BLKRASET 4706 /* Set read ahead for block device */
#define BLKRAGET 4707 /* get current read ahead setting */

/* These are a few other constants  only used by scsi  devices */

#define SCSI_IOCTL_GET_IDLUN 0x5382

/* Used to turn on and off tagged queuing for scsi devices */

#define SCSI_IOCTL_TAGGED_ENABLE 0x5383
#define SCSI_IOCTL_TAGGED_DISABLE 0x5384


#define BMAP_IOCTL 1	/* obsolete - kept for compatibility */
#define FIBMAP	   1	/* bmap access */
#define FIGETBSZ   2	/* get the block size used for bmap */

typedef char buffer_block[BLOCK_SIZE];

struct buffer_head {
	char * b_data;			/* pointer to data block (1024 bytes) */ /* 指向数据块 */ 
	unsigned long b_size;		/* block size *//* 块大小 */
	unsigned long b_blocknr;	/* block number */	/* 逻辑块号 */ 
	dev_t b_dev;			/* device (0 = free) */	 /* 虚拟设备标示符(B_FREE = free) */ 
	unsigned short b_count;		/* users using this block */	/* 块引用计数器 */
	unsigned char b_uptodate;
	unsigned char b_dirt;		/* 0-clean,1-dirty */
	unsigned char b_lock;		/* 0 - ok, 1 -locked */
	unsigned char b_req;		/* 0 if the buffer has been invalidated */	  
	unsigned char b_list;		/* List that this buffer appears *//* 本缓冲区所出现的LRU链表 [其实是数组lru_list的下标 参见buffer.c insert_into_queues函数]*/ 
	unsigned char b_retain;         /* Expected number of times this will
					   be used.  Put on freelist when 0 */
	unsigned long b_flushtime;      /* Time when this (dirty) buffer should be written */	/* 对脏缓冲区进行刷新的时间*/ 
	unsigned long b_lru_time;       /* Time when this buffer was last used. */
	struct wait_queue * b_wait;		/* 缓冲区等待队列 */ 
	struct buffer_head * b_prev;		/* doubly linked list of hash-queue */
	struct buffer_head * b_next;
	struct buffer_head * b_prev_free;	/* doubly linked list of buffers */
	struct buffer_head * b_next_free;
	struct buffer_head * b_this_page;	/* circular list of buffers in one page */
	struct buffer_head * b_reqnext;		/* request queue */
};

#include <linux/pipe_fs_i.h>
#include <linux/minix_fs_i.h>
#include <linux/ext_fs_i.h>
#include <linux/ext2_fs_i.h>
#include <linux/hpfs_fs_i.h>
#include <linux/msdos_fs_i.h>
#include <linux/umsdos_fs_i.h>
#include <linux/iso_fs_i.h>
#include <linux/nfs_fs_i.h>
#include <linux/xia_fs_i.h>
#include <linux/sysv_fs_i.h>

#ifdef __KERNEL__

/*
 * Attribute flags.  These should be or-ed together to figure out what
 * has been changed!
 */
#define ATTR_MODE	1
#define ATTR_UID	2
#define ATTR_GID	4
#define ATTR_SIZE	8
#define ATTR_ATIME	16
#define ATTR_MTIME	32
#define ATTR_CTIME	64
#define ATTR_ATIME_SET	128
#define ATTR_MTIME_SET	256

/*
 * This is the Inode Attributes structure, used for notify_change().  It
 * uses the above definitions as flags, to know which values have changed.
 * Also, in this manner, a Filesystem can look at only the values it cares
 * about.  Basically, these are the attributes that the VFS layer can
 * request to change from the FS layer.
 *
 * Derek Atkins <warlord@MIT.EDU> 94-10-20
 */
struct iattr {
	unsigned int	ia_valid;	//可以改变的有效属性 比如只设置了ATTR_UID，则只能改变inode的uid属性，将其设置为本结构体中的ia_uid
	umode_t		ia_mode;
	uid_t		ia_uid;
	gid_t		ia_gid;
	off_t		ia_size;
	time_t		ia_atime;
	time_t		ia_mtime;
	time_t		ia_ctime;
};

/*
	inode代表的是物理意义上的文件，通过inode可以得到一个数组，这个数组记录了文件内容的位置，
	如该文件位于硬盘的第3，8，10块，那么这个数组的内容就是3,8,10。其索引节点号inode->i_ino，
	在同一个文件系统中是唯一的，内核只要根据i_ino，就可以计算出它对应的inode在介质上的位置。
	就硬盘来说，根据i_ino就可以计算出它对应的inode属于哪个块(block)，从而找到相应的inode结构。
	但仅仅用inode还是无法描述出所有的文件系统，对于某一种特定的文件系统而言，比如ext3，
	在内存中用ext3_inode_info描述。他是一个包含inode的"容器"。inode代表一个文件。inode保存了文件的大小、
	创建时间、文件的块大小等参数，以及对文件的读写函数、文件的读写缓存等信息。一个真实的文件可以有多个
	dentry，因为指向文件的路径可以有多个（考虑文件的链接），而inode只有一个。
*/

/*
	i_mapping是一个重要的成员。这个结构目的是缓存文件的内容，对文件的读写操作首先要在i_mapping
	包含的缓存里寻找文件的内容。如果有缓存，对文件的读就可以直接从缓存中获得，而不用再去物理硬盘读取，
	从而大大加速了文件的读操作。写操作也要首先访问缓存，写入到文件的缓存。然后等待合适的机会，再从缓存写入硬盘。
*/

/*
	注意，在遥远的2.4的古代，不同文件系统索引节点的内存映像(ext3_inode_info，reiserfs_inode_info，msdos_inode_info ...)
	都是用一个union内嵌在inode数据结构中的. 但inode作为一种非常基本的数据结构而言，这样搞太大了，不利于快速的分配和回收
	。但是后来发明了container_of(...)这种方法后，就把union移到了外部，我们可以用类似
	container of(inode, struct ext3_inode_info, vfs_inode)，从inode出发，得到其的"容器"。
*/

struct inode {
	dev_t		i_dev;
	unsigned long	i_ino;	/* 节点号 */
	umode_t		i_mode;	/* 访问权限控制 */
	nlink_t		i_nlink;	/* 硬链接数 */
	uid_t		i_uid;	/* 使用者id */
	gid_t		i_gid;	 /* 使用者id组 */
	dev_t		i_rdev;	 /* 实设备标识符 */
	off_t		i_size;	/* 以字节为单位的文件大小 */
	time_t		i_atime;	 /* 最后访问时间 */
	time_t		i_mtime;	/* 最后修改(modify)时间 */
	time_t		i_ctime;	 /* 最后改变(change)时间 */
	unsigned long	i_blksize;	/* 以字节为单位的块大小 */
	unsigned long	i_blocks;	/* 文件的块数 */
	unsigned long	i_version;	/* 版本号 */
	struct semaphore i_sem;	/* 索引节点信号量 */
	struct inode_operations * i_op;	/* 索引节点操作表 */
	struct super_block * i_sb;	/* 相关的超级块 */
	struct wait_queue * i_wait;
	struct file_lock * i_flock;	 /* 文件锁链表 */
	struct vm_area_struct * i_mmap;	 /* 相关的地址映射 */
	struct inode * i_next, * i_prev;	/* 索引节点链表 */
	struct inode * i_hash_next, * i_hash_prev;	/* 哈希表 */
	struct inode * i_bound_to, * i_bound_by;
	struct inode * i_mount;
	unsigned short i_count;
	unsigned short i_wcount;	//write count？？？
	unsigned short i_flags;
	unsigned char i_lock;
	unsigned char i_dirt;
	unsigned char i_pipe;
	unsigned char i_sock;
	unsigned char i_seek;
	unsigned char i_update;
	union {
		struct pipe_inode_info pipe_i;	//将管道也视为了一种独立的文件系统
		struct minix_inode_info minix_i;
		struct ext_inode_info ext_i;
		struct ext2_inode_info ext2_i;
		struct hpfs_inode_info hpfs_i;
		struct msdos_inode_info msdos_i;
		struct umsdos_inode_info umsdos_i;
		struct iso_inode_info isofs_i;
		struct nfs_inode_info nfs_i;
		struct xiafs_inode_info xiafs_i;
		struct sysv_inode_info sysv_i;
		struct socket socket_i;
		void * generic_ip;
	} u;
};

/*
	文件对象表示进程已打开的文件，从用户角度来看，我们在代码中操作的就是一个文件对象。
	文件对象反过来指向一个目录项对象（目录项反过来指向一个索引节点）其实只有目录项对象
	才表示一个已打开的实际文件，虽然一个文件对应的文件对象不是唯一的，但其对应的索引节点和目录项对象却是唯一的。
*/

struct file {
	mode_t f_mode;
	loff_t f_pos;
	unsigned short f_flags;
	unsigned short f_count;
	off_t f_reada;	//预读标志
	struct file *f_next, *f_prev;
	int f_owner;		/* pid or -pgrp where SIGIO should be sent */
	struct inode * f_inode;	//file多对应的文件i节点
	struct file_operations * f_op;	//file对象的操作函数集 通过具体的文件系统的i节点来初始化
	unsigned long f_version;
	void *private_data;	/* needed for tty driver, and maybe others */
};

//我觉得struct flock结构和struct file_lock结构不同之处在于
//前者是专用于fcntl函数的简化参数，而后者是内核专用，记录了比前者更多的、
//用户不必关心的、内核自动维护的信息，比如文件锁链表等
//也可以叫做“请求锁”结构
struct file_lock {
	struct file_lock *fl_next;	/* singly linked list for this inode  *///将属于该文件节点的锁链接成单链表的锁指针
	struct file_lock *fl_nextlink;	/* doubly linked list of all locks *///将所有的内核文件锁链接成一双链表的指针
	struct file_lock *fl_prevlink;	/* used to simplify lock removal */
	struct task_struct *fl_owner;
	struct wait_queue *fl_wait;
	char fl_type;
	char fl_whence;
	off_t fl_start;
	off_t fl_end;
};

//Linux异步通知fasync，参见fcntl.c文件最下方的相关注释
//注意：应用程序通常只假设套接字和终端具备异步通知功能
//所以，内核中对fasync_struct结构体的引用主要只出现在Socket.c和tty_io.c中
struct fasync_struct {
	int    magic;	//由此可见，内核将这样的设备抽象为一种文件系统
	struct fasync_struct	*fa_next; /* singly linked list */
	struct file 		*fa_file;
};

#define FASYNC_MAGIC 0x4601

#include <linux/minix_fs_sb.h>
#include <linux/ext_fs_sb.h>
#include <linux/ext2_fs_sb.h>
#include <linux/hpfs_fs_sb.h>
#include <linux/msdos_fs_sb.h>
#include <linux/iso_fs_sb.h>
#include <linux/nfs_fs_sb.h>
#include <linux/xia_fs_sb.h>
#include <linux/sysv_fs_sb.h>

/*
							超级块对象
	存储一个已安装的文件系统的控制信息，代表一个已安装的文件系统；每次一个实际的文件系统被安装时， 
	内核会从磁盘的特定位置读取一些控制信息来填充内存中的超级块对象。一个安装实例和一个超级块对象
	一一对应。 超级块通过其结构中的一个域s_type记录它所属的文件系统类型。
*/

struct super_block {
	dev_t s_dev;
	unsigned long s_blocksize;
	unsigned char s_blocksize_bits;
	unsigned char s_lock;
	unsigned char s_rd_only;
	unsigned char s_dirt;
	struct file_system_type *s_type;
	struct super_operations *s_op;	/* 超级块方法 */
	unsigned long s_flags;
	unsigned long s_magic;
	unsigned long s_time;
	struct inode * s_covered;	 //挂载超级块的目录节点 超级块被挂载在此目录节点下 covered：覆盖的
	struct inode * s_mounted;	//超级块所在的i节点，我想，是因为文件超级块是struct super_block结构，不同于i节点结构
								//但超级块也需要一个i节点结构来表示，那就用s_mounted来表示吧
								//sb->s_mounted指向被加载文件系统的根目录i节点 也就是指向超级块中的根目录i节点
      							  //dir_i->i_mount保存有sb->s_mounted
								  //sb->s_covered保存有dir_i
	struct wait_queue * s_wait;
	union {
		struct minix_sb_info minix_sb;
		struct ext_sb_info ext_sb;
		struct ext2_sb_info ext2_sb;
		struct hpfs_sb_info hpfs_sb;
		struct msdos_sb_info msdos_sb;
		struct isofs_sb_info isofs_sb;
		struct nfs_sb_info nfs_sb;
		struct xiafs_sb_info xiafs_sb;
		struct sysv_sb_info sysv_sb;
		void *generic_sbp;
	} u;
};

//file对象的一个统一的抽象操作函数集
struct file_operations {
	int (*lseek) (struct inode *, struct file *, off_t, int);
	int (*read) (struct inode *, struct file *, char *, int);
	int (*write) (struct inode *, struct file *, char *, int);
	int (*readdir) (struct inode *, struct file *, struct dirent *, int);
	int (*select) (struct inode *, struct file *, int, select_table *);
	int (*ioctl) (struct inode *, struct file *, unsigned int, unsigned long);
	int (*mmap) (struct inode *, struct file *, struct vm_area_struct *);
	int (*open) (struct inode *, struct file *);
	void (*release) (struct inode *, struct file *);
	int (*fsync) (struct inode *, struct file *);
	int (*fasync) (struct inode *, struct file *, int);
	int (*check_media_change) (dev_t dev);
	int (*revalidate) (dev_t dev);
};

struct inode_operations {
	struct file_operations * default_file_ops;
	int (*create) (struct inode *,const char *,int,int,struct inode **);
	int (*lookup) (struct inode *,const char *,int,struct inode **);
	int (*link) (struct inode *,struct inode *,const char *,int);
	int (*unlink) (struct inode *,const char *,int);
	int (*symlink) (struct inode *,const char *,int,const char *);
	int (*mkdir) (struct inode *,const char *,int,int);
	int (*rmdir) (struct inode *,const char *,int);
	int (*mknod) (struct inode *,const char *,int,int,int);
	int (*rename) (struct inode *,const char *,int,struct inode *,const char *,int);
	int (*readlink) (struct inode *,char *,int);
	int (*follow_link) (struct inode *,struct inode *,int,int,struct inode **);
	int (*bmap) (struct inode *,int);
	void (*truncate) (struct inode *);
	int (*permission) (struct inode *, int);
	int (*smap) (struct inode *,int);	//类似于bamp smap->sector map
};

struct super_operations {
	void (*read_inode) (struct inode *);
	int (*notify_change) (struct inode *, struct iattr *);
	void (*write_inode) (struct inode *);
	void (*put_inode) (struct inode *);
	void (*put_super) (struct super_block *);
	void (*write_super) (struct super_block *);
	void (*statfs) (struct super_block *, struct statfs *);	/* VFS调用该函数获取文件系统状态 */
	int (*remount_fs) (struct super_block *, int *, char *);	/* 指定新的安装选项重新安装文件系统时，VFS会调用该函数 */
};

//用来描述文件系统的类型（比如ext3,ntfs等等），每种文件系统,不管由多少个实例安装到系统中,还是根本没有安装到系统中,都只有一个 file_system_type 结构。
/*
						和文件系统相关
	根据文件系统所在的物理介质和数据在物理介质上的组织方式来区分不同的文件系统类型的。 file_system_type结构
	用于描述具体的文件系统的类型信息。被Linux支持的文件系统，都有且仅有一 个file_system_type结构而不管它有零
	个或多个实例被安装到系统中。
	而与此对应的是每当一个文件系统被实际安装，就有一个vfsmount结构体被创建，这个结构体对应一个安装点。
*/
struct file_system_type {
	struct super_block *(*read_super) (struct super_block *, void *, int);
	char *name;		/* 文件系统名称 */
	int requires_dev;
	struct file_system_type * next;
};

extern int register_filesystem(struct file_system_type *);
extern int unregister_filesystem(struct file_system_type *);

asmlinkage int sys_open(const char *, int, int);
asmlinkage int sys_close(unsigned int);		/* yes, it's really unsigned */

extern void kill_fasync(struct fasync_struct *fa, int sig);

extern int getname(const char * filename, char **result);
extern void putname(char * name);

extern int register_blkdev(unsigned int, const char *, struct file_operations *);
extern int unregister_blkdev(unsigned int major, const char * name);
extern int blkdev_open(struct inode * inode, struct file * filp);
extern struct file_operations def_blk_fops;
extern struct inode_operations blkdev_inode_operations;

extern int register_chrdev(unsigned int, const char *, struct file_operations *);
extern int unregister_chrdev(unsigned int major, const char * name);
extern int chrdev_open(struct inode * inode, struct file * filp);
extern struct file_operations def_chr_fops;
extern struct inode_operations chrdev_inode_operations;

extern void init_fifo(struct inode * inode);

extern struct file_operations connecting_fifo_fops;
extern struct file_operations read_fifo_fops;
extern struct file_operations write_fifo_fops;
extern struct file_operations rdwr_fifo_fops;
extern struct file_operations read_pipe_fops;
extern struct file_operations write_pipe_fops;
extern struct file_operations rdwr_pipe_fops;

extern struct file_system_type *get_fs_type(char *name);

extern int fs_may_mount(dev_t dev);
extern int fs_may_umount(dev_t dev, struct inode * mount_root);
extern int fs_may_remount_ro(dev_t dev);

extern struct file *first_file;
extern int nr_files;
extern struct super_block super_blocks[NR_SUPER];

extern int shrink_buffers(unsigned int priority);
extern void refile_buffer(struct buffer_head * buf);
extern void set_writetime(struct buffer_head * buf, int flag);
extern void refill_freelist(int size);

extern struct buffer_head ** buffer_pages;
extern int nr_buffers;
extern int buffermem;
extern int nr_buffer_heads;

#define BUF_CLEAN 0
#define BUF_UNSHARED 1 /* Buffers that were shared but are not any more */
#define BUF_LOCKED 2   /* Buffers scheduled for write */
#define BUF_LOCKED1 3  /* Supers, inodes */
#define BUF_DIRTY 4    /* Dirty buffers, not yet scheduled for write */
#define BUF_SHARED 5   /* Buffers shared */
#define NR_LIST 6

//将缓冲区bh转移到干净页面的LRU队列中
extern inline void mark_buffer_clean(struct buffer_head * bh)
{
  if(bh->b_dirt) {
    bh->b_dirt = 0;
    if(bh->b_list == BUF_DIRTY)
	 refile_buffer(bh);	//重新确定一个bh对象所属的lru_list链表
  }
}

extern inline void mark_buffer_dirty(struct buffer_head * bh, int flag)
{
  if(!bh->b_dirt) {
    bh->b_dirt = 1;
    set_writetime(bh, flag);
    if(bh->b_list != BUF_DIRTY) refile_buffer(bh);
  }
}


extern int check_disk_change(dev_t dev);
extern void invalidate_inodes(dev_t dev);
extern void invalidate_buffers(dev_t dev);
extern int floppy_is_wp(int minor);
extern void sync_inodes(dev_t dev);
extern void sync_dev(dev_t dev);
extern int fsync_dev(dev_t dev);
extern void sync_supers(dev_t dev);
extern int bmap(struct inode * inode,int block);
extern int notify_change(struct inode *, struct iattr *);
extern int namei(const char * pathname, struct inode ** res_inode);
extern int lnamei(const char * pathname, struct inode ** res_inode);
extern int permission(struct inode * inode,int mask);
extern int get_write_access(struct inode * inode);
extern void put_write_access(struct inode * inode);
extern int open_namei(const char * pathname, int flag, int mode,
	struct inode ** res_inode, struct inode * base);
extern int do_mknod(const char * filename, int mode, dev_t dev);
extern void iput(struct inode * inode);
extern struct inode * __iget(struct super_block * sb,int nr,int crsmnt);
extern struct inode * get_empty_inode(void);
extern void insert_inode_hash(struct inode *);
extern void clear_inode(struct inode *);
extern struct inode * get_pipe_inode(void);
extern struct file * get_empty_filp(void);
extern struct buffer_head * get_hash_table(dev_t dev, int block, int size);
extern struct buffer_head * getblk(dev_t dev, int block, int size);
extern void ll_rw_block(int rw, int nr, struct buffer_head * bh[]);
extern void ll_rw_page(int rw, int dev, int nr, char * buffer);
extern void ll_rw_swap_file(int rw, int dev, unsigned int *b, int nb, char *buffer);
extern int is_read_only(int dev);
extern void brelse(struct buffer_head * buf);
extern void set_blocksize(dev_t dev, int size);
extern struct buffer_head * bread(dev_t dev, int block, int size);
extern unsigned long bread_page(unsigned long addr,dev_t dev,int b[],int size,int no_share);
extern struct buffer_head * breada(dev_t dev,int block, int size, 
				   unsigned int pos, unsigned int filesize);
extern void put_super(dev_t dev);
unsigned long generate_cluster(dev_t dev, int b[], int size);
extern dev_t ROOT_DEV;

extern void show_buffers(void);
extern void mount_root(void);

extern int char_read(struct inode *, struct file *, char *, int);
extern int block_read(struct inode *, struct file *, char *, int);
extern int read_ahead[];

extern int char_write(struct inode *, struct file *, char *, int);
extern int block_write(struct inode *, struct file *, char *, int);

extern int generic_mmap(struct inode *, struct file *, struct vm_area_struct *);

extern int block_fsync(struct inode *, struct file *);
extern int file_fsync(struct inode *, struct file *);

extern void dcache_add(struct inode *, const char *, int, unsigned long);
extern int dcache_lookup(struct inode *, const char *, int, unsigned long *);

extern int inode_change_ok(struct inode *, struct iattr *);
extern void inode_setattr(struct inode *, struct iattr *);

extern inline struct inode * iget(struct super_block * sb,int nr)
{
	return __iget(sb,nr,1);
}

#endif /* __KERNEL__ */

#endif
