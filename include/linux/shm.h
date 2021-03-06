#ifndef _LINUX_SHM_H_
#define _LINUX_SHM_H_
#include <linux/ipc.h>
/*
	shmid_ds数据结构表示每个新建的共享内存。当shmget()创建了一块新的共享内存后，
	返回一个可以用于引用该共享内存的shmid_ds数据结构的标识符。
*/
struct shmid_ds {
	struct ipc_perm shm_perm;	/* operation perms *//* 操作许可权数据结构指针 */
	int	shm_segsz;		/* size of segment (bytes) *//* 共享存储段大小 (bytes) */
	time_t	shm_atime;		/* last attach time *//* 最后调用shmat时间 */
	time_t	shm_dtime;		/* last detach time *//* 最后调用shmdt的时间 */
	time_t	shm_ctime;		/* last change time *//* 最后调用shmctl的改变的时间 */
	unsigned short	shm_cpid;	/* pid of creator *//*创建者的进程ID */
	unsigned short	shm_lpid;	/* pid of last operator *//* 最后对共享存储段进行操作的进程ID */
	short	shm_nattch;		/* no. of current attaches *//* 当前连接数 */
	/* the following are private */
	//用页面数表示的共享内存区域的容量。它恰为shm_segsz成员除以PAGE_SIZE的结果
	unsigned short   shm_npages;	/* size of segment (pages) */
	//用于跟踪这块共享内存区域页面分配的一个“页表”——“页表
	//”在这里加了引号，是因为它不是一个同前一章里一样真正的、硬件支持的页表。
	//不过它完成同样的工作
	unsigned long   *shm_pages;	/* array of ptrs to frames -> SHMMAX */ 
	//代表各自进程对这块共享内存区域进行映射的VMA的一个链表
	struct vm_area_struct *attaches; /* descriptors for attaches */
};

/* permission flag for shmget */
#define SHM_R		0400	/* or S_IRUGO from <linux/stat.h> */
#define SHM_W		0200	/* or S_IWUGO from <linux/stat.h> */

/* mode for attach */
#define	SHM_RDONLY	010000	/* read-only access */
#define	SHM_RND		020000	/* round attach address to SHMLBA boundary */
#define	SHM_REMAP	040000	/* take-over region on attach */

/* super user shmctl commands */
#define SHM_LOCK 	11
#define SHM_UNLOCK 	12

struct	shminfo {
    int shmmax;	
    int shmmin;	
    int shmmni;	
    int shmseg;	
    int shmall;	
};

/* address range for shared memory attaches if no address passed to shmat() */
#define SHM_RANGE_START	0x50000000
#define SHM_RANGE_END	0x60000000

/* format of page table entries that correspond to shared memory pages
   currently out in swap space (see also mm/swap.c):
   bit 0 (PAGE_PRESENT) is  = 0
   bits 7..1 (SWP_TYPE) are = SHM_SWP_TYPE
   bits 31..8 are used like this:
   bits 14..8 (SHM_ID) the id of the shared memory segment
   bits 29..15 (SHM_IDX) the index of the page within the shared memory segment
                    (actually only bits 24..15 get used since SHMMAX is so low)
*/

#define SHM_ID_SHIFT	8
/* Keep _SHM_ID_BITS as low as possible since SHMMNI depends on it and
   there is a static array of size SHMMNI. */
#define _SHM_ID_BITS	7
#define SHM_ID_MASK	((1<<_SHM_ID_BITS)-1)

#define SHM_IDX_SHIFT	(SHM_ID_SHIFT+_SHM_ID_BITS)
#define _SHM_IDX_BITS	15
#define SHM_IDX_MASK	((1<<_SHM_IDX_BITS)-1)

/* We must have SHM_ID_SHIFT + _SHM_ID_BITS + _SHM_IDX_BITS <= 32
   and SHMMAX <= (PAGE_SIZE << _SHM_IDX_BITS). */

#define SHMMAX 0x3fa000				/* max shared seg size (bytes) */
#define SHMMIN 1	 /* really PAGE_SIZE */	/* min shared seg size (bytes) */
#define SHMMNI (1<<_SHM_ID_BITS)		/* max num of segs system wide */
#define SHMALL (1<<(_SHM_IDX_BITS+_SHM_ID_BITS))/* max shm system wide (pages) */
#define	SHMLBA 0x1000				/* attach addr a multiple of this */
#define SHMSEG SHMMNI				/* max shared segs per process */

#ifdef __KERNEL__

/* shm_mode upper byte flags */
#define	SHM_DEST	01000	/* segment will be destroyed on last detach */
#define SHM_LOCKED      02000   /* segment will not be swapped */

/* ipcs ctl commands */
#define SHM_STAT 	13
#define SHM_INFO 	14
struct shm_info {
	int   used_ids;
	ulong shm_tot; /* total allocated shm */
	ulong shm_rss; /* total resident shm */
	ulong shm_swp; /* total swapped shm */
	ulong swap_attempts;
	ulong swap_successes;
};

#endif /* __KERNEL__ */

#endif /* _LINUX_SHM_H_ */

