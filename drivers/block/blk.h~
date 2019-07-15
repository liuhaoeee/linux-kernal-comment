#ifndef _BLK_H
#define _BLK_H

#include <linux/blkdev.h>
#include <linux/locks.h>
#include <linux/config.h>
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
 * NR_REQUEST is the number of entries in the request-queue.
 * NOTE that writes may use only the low 2/3 of these: reads
 * take precedence.
 *
 * 32 seems to be a reasonable number: enough to get some benefit
 * from the elevator-mechanism, but not so much as to lock a lot of
 * buffers when they are in the queue. 64 seems to be too many (easily
 * long pauses in reading when heavy writing/syncing is going on)
 */

 /*
			头文件 blk.h
     由于块设备驱动程序的绝大部分是设备无关的,核心的开发者通过把大部分相同
	 的代码放在一个头文件<linux/blk.h>中，来试图简化驱动程序的代码。因此，
	 每个块设备驱动程序都必须包含这个头文件，在<linux/blk.h>中定义的最重要
	 的函数是end_request，它被声明为static（静态）的。让它成为静态的，使得
	 不同驱动程序可有一个正确定义的end_request，而不需要每个都写自己的实现。
     在Linux1.2中，这个头文件应该用<linux/../../drivers/block/blk.h>来包含。
	 原因在于当时还不支持自定义的块设备驱动程序，而这个头文件最初位于drivers/block源码目录下
 */
#define NR_REQUEST	64

/*
 * This is used in the elevator algorithm: Note that
 * reads always go before writes. This is natural: reads
 * are much more time-critical than writes.
 */
/*
	IN_ORDER(tmp，req)的作用类似于在比较tmp和req这两个请求。
	如果tmp请求“小于”req请求，则IN_ORDER为真。
	“小于”的意思：
	（1）读请求小于写请求。
	（2）若请求类型相同，低设备号小于高设备号。
	（3）若请求同一设备，低扇区号小于高扇区号。
	*/
#define IN_ORDER(s1,s2) \
((s1)->cmd < (s2)->cmd || ((s1)->cmd == (s2)->cmd && \
((s1)->dev < (s2)->dev || (((s1)->dev == (s2)->dev && \
(s1)->sector < (s2)->sector)))))

/*
 * These will have to be changed to be aware of different buffer
 * sizes etc.. It actually needs a major cleanup.
 */
#ifdef IDE_DRIVER
#define SECTOR_MASK ((BLOCK_SIZE >> 9) - 1)
#else
#define SECTOR_MASK (blksize_size[MAJOR_NR] &&     \
	blksize_size[MAJOR_NR][MINOR(CURRENT->dev)] ? \
	((blksize_size[MAJOR_NR][MINOR(CURRENT->dev)] >> 9) - 1) :  \
	((BLOCK_SIZE >> 9)  -  1))
#endif /* IDE_DRIVER */

#define SUBSECTOR(block) (CURRENT->current_nr_sectors > 0)

extern unsigned long cdu31a_init(unsigned long mem_start, unsigned long mem_end);
extern unsigned long mcd_init(unsigned long mem_start, unsigned long mem_end);
#ifdef CONFIG_AZTCD
extern unsigned long aztcd_init(unsigned long mem_start, unsigned long mem_end);
#endif
#ifdef CONFIG_CDU535
extern unsigned long sony535_init(unsigned long mem_start, unsigned long mem_end);
#endif
#ifdef CONFIG_BLK_DEV_HD
extern unsigned long hd_init(unsigned long mem_start, unsigned long mem_end);
#endif
#ifdef CONFIG_BLK_DEV_IDE
extern unsigned long ide_init(unsigned long mem_start, unsigned long mem_end);
#endif
#ifdef CONFIG_SBPCD
extern unsigned long sbpcd_init(unsigned long, unsigned long);
#endif CONFIG_SBPCD
extern void set_device_ro(int dev,int flag);

extern void floppy_init(void);
#ifdef FD_MODULE
static
#else
extern
#endif
int new_floppy_init(void);
extern void rd_load(void);
extern long rd_init(long mem_start, int length);
extern int ramdisk_size;

extern unsigned long xd_init(unsigned long mem_start, unsigned long mem_end);

#define RO_IOCTLS(dev,where) \
  case BLKROSET: if (!suser()) return -EACCES; \
		 set_device_ro((dev),get_fs_long((long *) (where))); return 0; \
  case BLKROGET: { int __err = verify_area(VERIFY_WRITE, (void *) (where), sizeof(long)); \
		   if (!__err) put_fs_long(0!=is_read_only(dev),(long *) (where)); return __err; }
		 
#if defined(MAJOR_NR) || defined(IDE_DRIVER)
/*
	DEVICE_NR(kdev_t device)
	这个符号用来从kdev_t设备号中抽取物理设备的序号。这个宏的值可以是MINOR(device)或别的表达式。
	这要依据给设备或分区分配次设备号的常规方式而定。对同一个物理设备上的所有分区，这个宏应返回
	同一个设备号——也就是说，DEVICE_NR表达的是磁盘号，而不是分区号。这个符号被用来声明CURRENT_DEV，
	它在request_fn中用来确定被一个传送请求访问的硬件设备的次设备号

	DEVICE_INTR
	这个符号用来声明一个指向当前下半部处理程序的指针变量。宏SET_INTR(intr)和CLEAR_INTR用来给这
	个变量赋值。当设备可以发出具有不同含义的中断时，使用多个处理程序是很方便的

	DEVICE_OFF(kdev_t device)
	end_request函数在结束时调用这个宏。例如在软盘驱动程序中，它调用一个函数，这个函数负责更新用
	来控制马达停转的一个计时器。如果设备没有被关掉，那么串DEVICE_OFF可以被定义为空。
*/
/*
 * Add entries as needed.
 */

#ifdef IDE_DRIVER
//由于硬盘存在分区和逻辑分区，所以以这种形式求得其次设备号。参见fs.h中对IDE硬盘MINOR的注释
#define DEVICE_NR(device)	(MINOR(device) >> PARTN_BITS)
#define DEVICE_ON(device)	/* nothing */
#define DEVICE_OFF(device)	/* nothing */

#elif (MAJOR_NR == MEM_MAJOR)

/* ram disk */
#define DEVICE_NAME "ramdisk"
#define DEVICE_REQUEST do_rd_request
#define DEVICE_NR(device) ((device) & 7)
#define DEVICE_ON(device) 
#define DEVICE_OFF(device)

#elif (MAJOR_NR == FLOPPY_MAJOR)

static void floppy_off(unsigned int nr);

#define DEVICE_NAME "floppy"
#define DEVICE_INTR do_floppy
#define DEVICE_REQUEST do_fd_request
#define DEVICE_NR(device) ( ((device) & 3) | (((device) & 0x80 ) >> 5 ))
#define DEVICE_ON(device)
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device))

#elif (MAJOR_NR == HD_MAJOR)

/* harddisk: timeout is 6 seconds.. */
#define DEVICE_NAME "harddisk"
#define DEVICE_INTR do_hd	//不固定的do_hd函数，内核是动态调用SET_INTR来改变此函数指针的实际例程的，因为硬盘的读写比较复杂
#define DEVICE_TIMEOUT HD_TIMER
#define TIMEOUT_VALUE 600
#define DEVICE_REQUEST do_hd_request
#define DEVICE_NR(device) (MINOR(device)>>6)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == SCSI_DISK_MAJOR)

#define DEVICE_NAME "scsidisk"
#define DEVICE_INTR do_sd  
#define TIMEOUT_VALUE 200
#define DEVICE_REQUEST do_sd_request
#define DEVICE_NR(device) (MINOR(device) >> 4)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == SCSI_TAPE_MAJOR)

#define DEVICE_NAME "scsitape"
#define DEVICE_INTR do_st  
#define DEVICE_NR(device) (MINOR(device) & 0x7f)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == SCSI_CDROM_MAJOR)

#define DEVICE_NAME "CD-ROM"
#define DEVICE_INTR do_sr
#define DEVICE_REQUEST do_sr_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == XT_DISK_MAJOR)

#define DEVICE_NAME "xt disk"
#define DEVICE_REQUEST do_xd_request
#define DEVICE_NR(device) (MINOR(device) >> 6)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == CDU31A_CDROM_MAJOR)

#define DEVICE_NAME "CDU31A"
#define DEVICE_REQUEST do_cdu31a_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MITSUMI_CDROM_MAJOR)

#define DEVICE_NAME "Mitsumi CD-ROM"
/* #define DEVICE_INTR do_mcd */
#define DEVICE_REQUEST do_mcd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == AZTECH_CDROM_MAJOR)

#define DEVICE_NAME "Aztech CD-ROM"
#define DEVICE_REQUEST do_aztcd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == CDU535_CDROM_MAJOR)

#define DEVICE_NAME "SONY-CDU535"
#define DEVICE_INTR do_cdu535
#define DEVICE_REQUEST do_cdu535_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MATSUSHITA_CDROM_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #1"
#define DEVICE_REQUEST do_sbpcd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MATSUSHITA_CDROM2_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #2"
#define DEVICE_REQUEST do_sbpcd2_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MATSUSHITA_CDROM3_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #3"
#define DEVICE_REQUEST do_sbpcd3_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MATSUSHITA_CDROM4_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #4"
#define DEVICE_REQUEST do_sbpcd4_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#endif /* MAJOR_NR == whatever */

#if (MAJOR_NR != SCSI_TAPE_MAJOR) && !defined(IDE_DRIVER)

#ifndef CURRENT
#define CURRENT (blk_dev[MAJOR_NR].current_request)
#endif

#define CURRENT_DEV DEVICE_NR(CURRENT->dev)

#ifdef DEVICE_INTR
void (*DEVICE_INTR)(void) = NULL;
#endif
#ifdef DEVICE_TIMEOUT

#define SET_TIMER \
((timer_table[DEVICE_TIMEOUT].expires = jiffies + TIMEOUT_VALUE), \
(timer_active |= 1<<DEVICE_TIMEOUT))

#define CLEAR_TIMER \
timer_active &= ~(1<<DEVICE_TIMEOUT)

#define SET_INTR(x) \
if ((DEVICE_INTR = (x)) != NULL) \
	SET_TIMER; \
else \
	CLEAR_TIMER;

#else

#define SET_INTR(x) (DEVICE_INTR = (x))

#endif /* DEVICE_TIMEOUT */

static void (DEVICE_REQUEST)(void);

#ifdef DEVICE_INTR
#define CLEAR_INTR SET_INTR(NULL)
#else
#define CLEAR_INTR
#endif

#define INIT_REQUEST \
	if (!CURRENT) {\
		CLEAR_INTR; \
		return; \
	} \
	if (MAJOR(CURRENT->dev) != MAJOR_NR) \
		panic(DEVICE_NAME ": request list destroyed"); \
	if (CURRENT->bh) { \
		if (!CURRENT->bh->b_lock) \
			panic(DEVICE_NAME ": block not locked"); \
	}

#endif /* (MAJOR_NR != SCSI_TAPE_MAJOR) && !defined(IDE_DRIVER) */

/* end_request() - SCSI devices have their own version */

#if ! SCSI_MAJOR(MAJOR_NR)

#ifdef IDE_DRIVER
static void end_request(byte uptodate, byte hwif) {
	struct request *req = ide_cur_rq[HWIF];
#else
static void end_request(int uptodate) {
	struct request *req = CURRENT;
#endif /* IDE_DRIVER */
	struct buffer_head * bh;

	req->errors = 0;
	if (!uptodate) {
		printk("end_request: I/O error, dev %04lX, sector %lu\n",
		       (unsigned long)req->dev, req->sector);
		req->nr_sectors--;
		req->nr_sectors &= ~SECTOR_MASK;
		req->sector += (BLOCK_SIZE / 512);
		req->sector &= ~SECTOR_MASK;		
	}

	if ((bh = req->bh) != NULL) {
		req->bh = bh->b_reqnext;
		bh->b_reqnext = NULL;
		bh->b_uptodate = uptodate;		
		if (!uptodate) bh->b_req = 0; /* So no "Weird" errors */
		unlock_buffer(bh);
		if ((bh = req->bh) != NULL) {
			req->current_nr_sectors = bh->b_size >> 9;
			if (req->nr_sectors < req->current_nr_sectors) {
				req->nr_sectors = req->current_nr_sectors;
				printk("end_request: buffer-list destroyed\n");
			}
			req->buffer = bh->b_data;
			return;
		}
	}
#ifdef IDE_DRIVER
	ide_cur_rq[HWIF] = NULL;
#else
	DEVICE_OFF(req->dev);
	CURRENT = req->next;
#endif /* IDE_DRIVER */
	if (req->sem != NULL)
		up(req->sem);
	req->dev = -1;
	wake_up(&wait_for_request);
}
#endif /* ! SCSI_MAJOR(MAJOR_NR) */

#endif /* defined(MAJOR_NR) || defined(IDE_DRIVER) */

#endif /* _BLK_H */
