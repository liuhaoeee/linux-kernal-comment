/*
 *  linux/drivers/block/hd.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
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
/*关于本文件程序的理解，笔者觉得赵炯博士的《Linux内核完全注释》中解释的非常不错特别是对硬盘硬件的介绍*/
/*
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 * 
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 *
 *  Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  in the early extended-partition checks and added DM partitions
 *
 *  IRQ-unmask, drive-id, multiple-mode, support for ">16 heads",
 *  and general streamlining by mlord@bnr.ca (Mark Lord).
 */
//本程序中的所有函数都是在中断中调用的，所以这些函数不可以睡眠
//但是这并不意味着CPU需要等待数据的读取。实际上，CPU为硬盘控制器
//做好读写准备后，就从中断返回了而继续做其他的事情，而硬盘控制器
//负责做硬盘数据的操作。当操作完成后，中断CPU。


/*
	AT硬盘的工作方式是基于中断的。硬盘控制器接受到命令后，硬盘控制器会执行命令。
	当命令执行完毕，硬盘控制器就会向CPU发送中断，CPU响应中断并做相应的处理。
	比如，CPU向硬盘控制器发送写命令，CPU将要写的数据写入硬盘控制器内部缓冲区
	之后，就可以转而去做其他事情了，而硬盘控制器则负责将这些数据实际地写入
	硬盘，当这些数据都写入硬盘后或者写入的过程中出现了错误，硬盘控制器就会产
	生中断，以通知CPU命令执行完毕或者执行出错。CPU在发送写命令时就已经设置好了
	当硬盘控制器写完数据后中断所对应的处理程序（在这里就是write_intr()）。
	读方式也是类似的机制，即CPU向硬盘控制器发送读命令，并设置当读命令执行完毕
	或执行出错所引发的中断的处理程序（在这里就是read_intr()）,然后CPU就可以转
	而去做其他的事情了，而硬盘控制器则负责将硬盘中的指定数据读入其内部缓冲区。
	当读取完毕后，中断CPU，CPU随后将数据从硬盘控制器内部缓冲区中拷贝到指定内存
	缓冲区中。
	注意，硬盘中断的方式和类型都是一个，只是在执行命令之前CPU根据执行命令的不同
	设置了不同类型的中断处理程序。这些中断处理程序对应本文件中的xxx_intr()。	
*/
#define DEFAULT_MULT_COUNT  0	/* set to 0 to disable multiple mode at boot */
#define DEFAULT_UNMASK_INTR 0	/* set to 0 to *NOT* unmask irq's more often */

#include <asm/irq.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/mc146818rtc.h> /* CMOS defines */

#define REALLY_SLOW_IO
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR HD_MAJOR
#include "blk.h"

#define HD_IRQ 14

static int revalidate_hddisk(int, int);

#define	HD_DELAY	0

#define MAX_ERRORS     16	/* Max read/write errors/sector */
#define RESET_FREQ      8	/* Reset controller every 8th retry */
#define RECAL_FREQ      4	/* Recalibrate every 4th retry */
//系统所支持的做多的硬盘数 虽然ide接口最多支持四个ide设备，但一般hda和hdb用于硬盘，hdc用于光驱
#define MAX_HD		2

#define STAT_OK		(READY_STAT|SEEK_STAT)
#define OK_STATUS(s)	(((s)&(STAT_OK|(BUSY_STAT|WRERR_STAT|ERR_STAT)))==STAT_OK)

static void recal_intr(void);
static void bad_rw_intr(void);

static char recalibrate[MAX_HD] = { 0, };
static char special_op[MAX_HD] = { 0, };
static6nnt access_count[MAX_HD] = {0, };
static char busy[MAX_HD] = {0, };
static struct wait_queue * busy_wait = NULL;

//复位标志，当发生读写错误时会设置该标志位并调用相关复位函数，以复位硬盘和控制器
static int reset = 0;
static int hd_error = 0;

/*
 *  This struct defines the HD's and their types.
 */
//应该是抽象了硬盘，其中head表示硬盘的磁头数，也即盘面数
//sect表示每磁道扇区数，cyl表示柱面(磁道)数，ctl:控制字节
//wpcom:写前预补偿柱面号，lzone：磁头着陆区柱面号
/*这些信息可以在系统启动是从bios读取*/
struct hd_i_struct {
	unsigned int head,sect,cyl,wpcom,lzone,ctl;
	};
static struct hd_driveid *hd_ident_info[MAX_HD] = {0, };
/*
	Precomp（硬盘的预写补偿）
	在向硬盘写入时，由于记录密度很高，相邻的两个信息存储区域由于
	磁化后相互吸引或相互排斥，使它们之间有可能互相干涉，如连续写
	入两个1时有可能产生叠加，以至读出时，数据无法分离或丢失数据。
	盘片的内圈（高磁道）比外圈（低磁道）的位密度高，上述情况更容易发生。
	
	所谓预写补偿是指在写入时，偏离正常的位置（前移或后移），使得写入
	的磁化区域在完成相互排斥或吸引后，其实际位置恰好是正确的读出位置。
	预写补偿柱面是需预补偿写入的第一个柱面，其值由厂商的产品说明中给出。
	对于用户自定义硬盘，其预补偿值由用户在CMOS设置中指定。预写补偿是由
	硬盘控制器中的预写补偿电路完成的，它将对该柱面到中心柱面的所有柱面
	实行预写补偿。如果某驱动器有1024个柱面，其预补偿值也为1024，两者相
	同，则说明该驱动器不需要预写补偿。该项设置的最大值为65536。

	预补偿值之所以可以由用户在CMOS设置中指定，是因为操作系统是根据BOIS
	读取的CMOS中的硬盘信息，并保存在相关内核数据结构中
*/
#ifdef HD_TYPE
static struct hd_i_struct hd_info[] = { HD_TYPE };
struct hd_i_struct bios_info[] = { HD_TYPE };
//从这里可以看出，如果定义了HD_TYPE,则硬盘信息是静态编译的，否则是动态检测建立的
static int NR_HD = ((sizeof (hd_info))/(sizeof (struct hd_i_struct)));
#else
static struct hd_i_struct hd_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
struct hd_i_struct bios_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
static int NR_HD = 0;
#endif

//参见ide.c中对PARTN_BITS的注释，便可深刻理解下面三个数组的意义
//hd数组是硬盘各个分区的信息，每个数组项代表了一个硬盘分区
//数组项hd[0<<6]和hd[1<<6]（假设存在两个硬盘）代表了两个实际硬盘的总体系统
//而hd[0<<6]~hd[1<<6]之间的数组项代表了第一个硬盘的各个分区的信息
//hd[1<<6]~hd[2<<6]之间的数组项代表了第二个硬盘的各个分区的信息
static struct hd_struct hd[MAX_HD<<6]={{0,0},};
static int hd_sizes[MAX_HD<<6] = {0, };
static int hd_blocksizes[MAX_HD<<6] = {0, };

#if (HD_DELAY > 0)
unsigned long last_req;

unsigned long read_timer(void)
{
	unsigned long t, flags;
	int i;

	save_flags(flags);
	cli();
	//求得自系统启动以来 PIT产生的clock-cycle总数 
/*
	8254 PIT 的输入时钟信号的频率是 1193181HZ,也即一秒钟输
	入 1193181 个 clock-cycle。每输入一个 clock-cycle 其时间通道的计数器就向下减 1,一直减到 0 值。因此
	对于通道 0 而言,当他的计数器减到 0 时,PIT 就向系统产生一次时钟中断,表示一个时钟滴答已经过去
	了。当各通道的计数器减到 0 时,我们就说该通道处于“Terminal count”状态。
	通道计数器的最大值是 10000h,所对应的时钟中断频率是 1193181/(65536)=18.2HZ,也就是说,
	此时一秒钟之内将产生 18.2 次时钟中断。

	但这里为什么是*11932呢？因为linux1.2的时钟周期为一秒产生100次时钟中断
	11932*100==1193181 linux是将通道0的计数器设置为了11932
*/
	t = jiffies * 11932;
	//PIT control word(write only)
    	outb_p(0, 0x43);
	//读取PIT 0通道的当前值 通道 0 用来负责更新系统时钟。
	//每当一个时钟滴答过去时,它就会通过 IRQ0 向系统产生一次时钟中断。
/*
	由于通道 0、1、2 的计数器是一个 16 位寄存器,而相应的端口却都是 8 位的,因此读写通道
	计数器必须进行进行两次 I/O 端口读写操作,分别对应于计数器的高字节和低字节
*/
	i = inb_p(0x40);
	i |= inb(0x40) << 8;
	restore_flags(flags);
	//这里为什么是-i呢？因为从PIT 0通道中读取的值是一个不断自减的值，
	//当其自减到0时才会引发一个时钟中断，从而增加一个滴答值jiffies
	//所以t-i得到的并不是一个精确的时间值，而是一个比精确值少一个
	//滴答值的值，但这是无关紧要的，因为系统使用这个值也是作为相对值来使用的
	return(t - i);
}
#endif

/*
	对硬盘的操作离不开控制寄存器，为了控制磁盘要经常的去检测磁盘的运行状态，
	在本驱动程序中有一系列的函数是完成这项工作的，check_status()检测硬盘的
	运行状态，如果出现错误则进行处理。contorller_ready()检测控制器是否准备
	好。drive_busy()检测硬盘设备是否处于忙态。当出现错误的时候，由dump_status()
	函数去检测出错的原因。wait_DRQ()对数据请求位进行测试。

	当硬盘的操作出现错误的时候，硬盘驱动程序会把它尽量在接近硬件的地方解决掉，
	其方法是进行重复操作，这些在 bad_rw_intr()中进行，与其相关的函数有 
	reset_controller()和reset_hd() 。
*/

void hd_setup(char *str, int *ints)
{
	int hdind = 0;

	if (ints[0] != 3)
		return;
	if (bios_info[0].head != 0)
		hdind=1;
	bios_info[hdind].head  = hd_info[hdind].head = ints[2];
	bios_info[hdind].sect  = hd_info[hdind].sect = ints[3];
	bios_info[hdind].cyl   = hd_info[hdind].cyl = ints[1];
	bios_info[hdind].wpcom = hd_info[hdind].wpcom = 0;
	bios_info[hdind].lzone = hd_info[hdind].lzone = ints[1];
	//控制字节的第三位：若磁头数大于8则置1。参见《linux内核完全注释》关于硬盘基本参数信息表的说明
	bios_info[hdind].ctl   = hd_info[hdind].ctl = (ints[2] > 8 ? 8 : 0);
	NR_HD = hdind+1;
}

//当出现错误的时候，由 dump_status()函数去检测出错的原因
static void dump_status (char *msg, unsigned int stat)
{
	unsigned long flags;
	char devc;

	//为构造“hda hdb hdc hdd”做准备
	/*devc是char类型，而通过求得设备的次设备号(0-3)来与'a'相加求得a b c d*/
	devc = CURRENT ? 'a' + DEVICE_NR(CURRENT->dev) : '?';
	save_flags (flags);
	sti();
	//打印硬盘的出错信息
	printk("hd%c: %s: status=0x%02x { ", devc, msg, stat & 0xff);
	if (stat & BUSY_STAT)	printk("Busy ");
	if (stat & READY_STAT)	printk("DriveReady ");
	if (stat & WRERR_STAT)	printk("WriteFault ");
	if (stat & SEEK_STAT)	printk("SeekComplete ");
	if (stat & DRQ_STAT)	printk("DataRequest ");
	if (stat & ECC_STAT)	printk("CorrectedError ");
	if (stat & INDEX_STAT)	printk("Index ");
	if (stat & ERR_STAT)	printk("Error ");
	printk("}\n");
	if ((stat & ERR_STAT) == 0) {
		hd_error = 0;
	} else {
		//读取错误信息
		hd_error = inb(HD_ERROR);
		printk("hd%c: %s: error=0x%02x { ", devc, msg, hd_error & 0xff);
		if (hd_error & BBD_ERR)		printk("BadSector ");
		if (hd_error & ECC_ERR)		printk("UncorrectableError ");
		if (hd_error & ID_ERR)		printk("SectorIdNotFound ");
		if (hd_error & ABRT_ERR)	printk("DriveStatusError ");
		if (hd_error & TRK0_ERR)	printk("TrackZeroNotFound ");
		if (hd_error & MARK_ERR)	printk("AddrMarkNotFound ");
		printk("}");
		if (hd_error & (BBD_ERR|ECC_ERR|ID_ERR|MARK_ERR)) {
			printk(", CHS=%d/%d/%d", (inb(HD_HCYL)<<8) + inb(HD_LCYL),
				inb(HD_CURRENT) & 0xf, inb(HD_SECTOR));
			if (CURRENT)
				printk(", sector=%ld", CURRENT->sector);
		}
		printk("\n");
	}
	restore_flags (flags);
}

//check_status()检测硬盘的运行状态，如果出现错误则进行处理
void check_status(void)
{
	int i = inb_p(HD_STATUS);

	if (!OK_STATUS(i)) {
		dump_status("check_status", i);
		bad_rw_intr();
	}
}

//读取状态寄存器的值。为1时表示驱动器忙(BSY)，正在执行命令。在发送命令前先判断该位
//drive_busy()检测硬盘设备是否处于忙态
static int controller_busy(void)
{
	//循环尝试的次数
	int retries = 100000;
	unsigned char status;

	do {
		//读取状态寄存器的值
		status = inb_p(HD_STATUS);
	} while ((status & BUSY_STAT) && --retries);
	return status;
}

static int status_ok(void)
{
	//读取状态寄存器的值
	unsigned char status = inb_p(HD_STATUS);

	//BUSY_STAT：为1时表示驱动器忙(BSY)，正在执行命令
	if (status & BUSY_STAT)
		return 1;	/* Ancient(古代的), but does it make sense??? */
	//WRERR_STAT:为1时，表示驱动器发生写故障
	if (status & WRERR_STAT)
		return 0;
	//READY_STAT：为1时表示驱动器准备好，可以接受命令。
	if (!(status & READY_STAT))
		return 0;
	//SEEK_STAT：为1表示磁头完成寻道操作，已停留在该道上
	if (!(status & SEEK_STAT))
		return 0;
	return 1;
}

//试图将读写磁盘命令参数写入硬盘控制寄存器，并测试是否可以执行命令，如是，则返回1
//从函数参数来看，函数测试的是硬盘驱动器driver的head磁头
//contorller_ready()检测控制器是否准备好
static int controller_ready(unsigned int drive, unsigned int head)
{
	//循环尝试的次数
	int retry = 100;

	do {
		//读取状态寄存器的值。为1时表示驱动器忙(BSY)，正在执行命令。在发送命令前先判断该位
		if (controller_busy() & BUSY_STAT)
			return 0;
		//向命令寄存器组写如命令 /* 101dhhhh , d=drive, hhhh=head */
		outb_p(0xA0 | (drive<<4) | head, HD_CURRENT);
		//判断命令是否可以立即执行或者已被执行
		if (status_ok())
			return 1;
	} while (--retry);
	return 0;
}
/*
	对于块设备的读写操作是先对缓冲区操作，但是当需要真正同硬盘交换数据的时候，
	驱动程序又干了些什么？在 hd.c 中有一个函数 hd_out()，可以说它在实际的数
	据交换中起着主要的作用．
	
	其中参数drive是进行操作的硬盘号（0/1）； nsect 是每次读写的扇区数； 
	sect 是读写的开始扇区号； head 是读写的磁头号；cmd是操作命令控制命令字。
	intr_addr是硬盘中断处理程序中将要调用的函数
	
	通过这个函数向硬盘控制器的寄存器中写入数据，启动硬盘进行实际的操作。同时
	这个函数也配合完成 cmd 命令相应的中断服务子程序，通过SET_INIT(intr_addr) 
	宏定义将其地址赋给 DEVICE_INTR。
*/
//输出命令块，向对应端口输出参数和命令，hd_out函数实现
static void hd_out(unsigned int drive,unsigned int nsect,unsigned int sect,
		unsigned int head,unsigned int cyl,unsigned int cmd,
		void (*intr_addr)(void))
{
	unsigned short port;

#if (HD_DELAY > 0)
	while (read_timer() - last_req < HD_DELAY)
		/* nothing */;
#endif
	if (reset)
		return;
	//如果硬盘还未准备好执行命令
	if (!controller_ready(drive, head)) {
		//表示要复位硬盘控制器和硬盘，在就版本中如果硬盘没有做好执行命令的准备
		//在这里直接死机
		reset = 1;
		return;
	}
	SET_INTR(intr_addr);
	//向控制寄存器输出控制字节
	/*在向硬盘控制器发送参数和命令之前，规定要先向控制器命令端口HD_CMD(0x3f6)发送一指定硬盘的控制字节
	*以建立相应的硬盘控制方式，该控制字节即是硬盘信息结构数组中的ctl字段。然后向控制器端口0x1f1-0x1f7
	*发送7字节的参数命令块	
	*/
	outb_p(hd_info[drive].ctl,HD_CMD);
	port=HD_DATA;
	/* same io address, read=error, write=feature */
	outb_p(hd_info[drive].wpcom>>2,++port);
	/* nr of sectors to read/write */
	outb_p(nsect,++port);
	/* starting sector */
	outb_p(sect,++port);
	/* starting cylinder */
	outb_p(cyl,++port);
	/* high byte of starting cyl */
	outb_p(cyl>>8,++port);
	/* 101dhhhh , d=drive, hhhh=head */
	outb_p(0xA0|(drive<<4)|head,++port);
	/* same io address, read=status, write=cmd */
	outb_p(cmd,++port);
}

static void hd_request (void);
static unsigned int identified  [MAX_HD] = {0,}; /* 1 = drive ID already displayed   */
static unsigned int unmask_intr [MAX_HD] = {0,}; /* 1 = unmask IRQs during I/O       */
/*以下几个数据结构用于muti_mode硬盘读写模式*/
static unsigned int max_mult    [MAX_HD] = {0,}; /* max sectors for MultMode         */
static unsigned int mult_req    [MAX_HD] = {0,}; /* requested MultMode count         */
static unsigned int mult_count  [MAX_HD] = {0,}; /* currently enabled MultMode count */
static struct request WCURRENT;

static void fixstring (unsigned char *s, int bytecount)
{
	unsigned char *p, *end = &s[bytecount &= ~1];	/* bytecount must be even(偶数) */

	/* convert from big-endian to little-endian */
	for (p = end ; p != s;) {
		unsigned short *pp = (unsigned short *) (p -= 2);
		*pp = (*pp >> 8) | (*pp << 8);
	}

	/* strip(清除) leading blanks */
	while (s != end && *s == ' ')
		++s;

	/* compress internal blanks and strip trailing blanks */
	//这个或关系用的精巧啊
	while (s != end && *s) {
		if (*s++ != ' ' || (s != end && *s && *s != ' '))
			*p++ = *(s-1);
	}

	/* wipe out trailing garbage */
	while (p != end)
		*p++ = '\0';
}

static void identify_intr(void)
{
	//获取设备次设备号（对于硬盘，因为系统最多支持两个硬盘，所以dev的值0/1）
	unsigned int dev = DEVICE_NR(CURRENT->dev);
	//读取设备控制器状态信息
	unsigned short stat = inb_p(HD_STATUS);
	//id指向表示此设备信息的结构体
	struct hd_driveid *id = hd_ident_info[dev];

	if (unmask_intr[dev])
		sti();
	if (stat & (BUSY_STAT|ERR_STAT)) {
		printk ("  hd%c: non-IDE device, %dMB, CHS=%d/%d/%d\n", dev+'a',
			hd_info[dev].cyl*hd_info[dev].head*hd_info[dev].sect / 2048,
			hd_info[dev].cyl, hd_info[dev].head, hd_info[dev].sect);
		if (id != NULL) {
			hd_ident_info[dev] = NULL;
			kfree_s (id, 512);
		}
	} else {
		//将读取到的512字节的硬盘信息填入id指向的内核数据结构
		insw(HD_DATA, id, 256); /* get ID info */
		//设置此设备在多扇区读写模式下一次可读写的最大的扇区数
		max_mult[dev] = id->max_multsect;
		if ((id->field_valid&1) && id->cur_cyls && id->cur_heads && (id->cur_heads <= 16) && id->cur_sectors) {
			/*
			 * Extract the physical drive geometry for our use.
			 * Note that we purposely do *not* update the bios_info.
			 * This way, programs that use it (like fdisk) will 
			 * still have the same logical view as the BIOS does,
			 * which keeps the partition table from being screwed.
			 */
			//这里设置的应该是硬盘的实际物理信息参数 而bios_info中仍保留
			//逻辑参数 参加上面的hd_setup()
			hd_info[dev].cyl  = id->cur_cyls;
			hd_info[dev].head = id->cur_heads;
			hd_info[dev].sect = id->cur_sectors; 
		}
		fixstring (id->serial_no, sizeof(id->serial_no));
		fixstring (id->fw_rev, sizeof(id->fw_rev));
		fixstring (id->model, sizeof(id->model));
		printk ("  hd%c: %.40s, %dMB w/%dKB Cache, CHS=%d/%d/%d, MaxMult=%d\n",
			dev+'a', id->model, id->cyls*id->heads*id->sectors/2048,
			id->buf_size/2, bios_info[dev].cyl, bios_info[dev].head,
			bios_info[dev].sect, id->max_multsect);
		/*
		 * Early model Quantum drives go weird at this point,
		 *   but doing a recalibrate seems to "fix" them.
		 * (Doing a full reset confuses some other model Quantums)
		 */
/*
		quantum（美国昆腾公司）
		quantum 一种硬盘品牌，现在已经淡出硬盘市场，就是很早的昆腾硬盘。
		美国昆腾公司（Quantum Corporation）创立于1980年，昆腾是当时全
		球产量最高的个人电脑硬盘（ATA接口）供应商，也是高容量硬盘
		（SCSI接口）的领先供应商，并且是全球DLT磁带机营业额最高的厂商，
		昆腾还为遍布全球的原设备制造商（OEM）提供多种类型的存储产品。
		昆腾公司于2000年4月2日被迈拓公司收购。		
*/
		if (!strncmp(id->model, "QUANTUM", 7))
			special_op[dev] = recalibrate[dev] = 1;
	}
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	hd_request();
	return;
}

//请求设置硬盘控制器的muti_mode模式命令执行后要调用的中断函数，如果请求成功
//则将mult_count[dev]的值更新为新的值
static void set_multmode_intr(void)
{
	unsigned int dev = DEVICE_NR(CURRENT->dev), stat = inb_p(HD_STATUS);

	if (unmask_intr[dev])
		sti();
	if (stat & (BUSY_STAT|ERR_STAT)) {
		mult_req[dev] = mult_count[dev] = 0;
		dump_status("set multmode failed", stat);
	} else {
		if ((mult_count[dev] = mult_req[dev]))
			printk ("  hd%c: enabled %d-sector multiple mode\n",
				dev+'a', mult_count[dev]);
		else
			printk ("  hd%c: disabled multiple mode\n", dev+'a');
	}
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	hd_request();
	return;
}

//检测驱动器就绪：判断主状态寄存器的READY_STAT（位6）是否为1；
static int drive_busy(void)
{
	unsigned int i;
	unsigned char c;

	for (i = 0; i < 500000 ; i++) {
		c = inb_p(HD_STATUS);
		if ((c & (BUSY_STAT | READY_STAT | SEEK_STAT)) == STAT_OK)
			return 0;
	}
	dump_status("reset timed out", c);
	return 1;
}

//诊断复位（重新校正）硬盘控制器
static void reset_controller(void)
{
	int i;

	//向硬盘控制寄存器端口发送复位控制字节
	outb_p(4,HD_CMD);
	//等待一段时间
	for(i = 0; i < 1000; i++) nop();
	//发送正常控制字节（不禁止重试和重读）
	outb_p(hd_info[0].ctl & 0x0f,HD_CMD);
	//等待一段时间
	for(i = 0; i < 1000; i++) nop();
	//等待硬盘就绪，若等待超时，则显示警告信息
	if (drive_busy())
		printk("hd: controller still busy\n");
	//否则，读取错误寄存器内容，若其不为1（表示无错误），则显示硬盘控制器复位失败信息
	else if ((hd_error = inb(HD_ERROR)) != 1)
		printk("hd: controller reset failed: %02x\n",hd_error);
}


static void reset_hd(void)
{
	static int i;

repeat:
	if (reset) {
		reset = 0;
		i = -1;
		//复位（重新校正）硬盘控制器
		reset_controller();
	} else {
		//检测硬盘的运行状态
		check_status();
		if (reset)
			goto repeat;
	}
	if (++i < NR_HD) {
		special_op[i] = recalibrate[i] = 1;
		if (unmask_intr[i]) {
			unmask_intr[i] = DEFAULT_UNMASK_INTR;
			printk("hd%c: reset irq-unmasking to %d\n",i+'a',
				DEFAULT_UNMASK_INTR);
		}
		if (mult_req[i] || mult_count[i]) {
			mult_count[i] = 0;
			mult_req[i] = DEFAULT_MULT_COUNT;
			printk("hd%c: reset multiple mode to %d\n",i+'a',
				DEFAULT_MULT_COUNT);
		}
		hd_out(i,hd_info[i].sect,hd_info[i].sect,hd_info[i].head-1,
			hd_info[i].cyl,WIN_SPECIFY,&reset_hd);
		if (reset)
			goto repeat;
	} else
		hd_request();
}

/*
 * Ok, don't know what to do with the unexpected interrupts: on some machines
 * doing a reset and a retry seems to result in an eternal loop. Right now I
 * ignore it, and just set the timeout.
 *
 * On laptops (and "green" PCs), an unexpected interrupt occurs whenever the
 * drive enters "idle", "standby", or "sleep" mode, so if the status looks
 * "good", we just ignore the interrupt completely.
 */
//功能：对不期望的中断进行处理(设置SET_TIMER)
void unexpected_hd_interrupt(void)
{
	unsigned int stat = inb_p(HD_STATUS);

	if (stat & (BUSY_STAT|DRQ_STAT|ECC_STAT|ERR_STAT)) {
		dump_status ("unexpected interrupt", stat);
		SET_TIMER;
	}
}

/*
 * bad_rw_intr() now tries to be a bit smarter and does things
 * according to the error returned by the controller.
 * -Mika Liljeberg (liljeber@cs.Helsinki.FI)
 */
//功能： 当硬盘的读写操作出现错误时进行处理
/*
	每重复四次磁头复位；
	每重复八次控制器复位；
	每重复十六次放弃操作。
*/
/*
	当硬盘的操作出现错误的时候，硬盘驱动程序会把它尽量在接近硬件的地方解决掉，
	其方法是进行重复操作，这些在 bad_rw_intr()中进行，与其相关的函数有
	reset_controller()和reset_hd() 
*/
static void bad_rw_intr(void)
{
	int dev;

	if (!CURRENT)
		return;
	dev = DEVICE_NR(CURRENT->dev);
	if (++CURRENT->errors >= MAX_ERRORS || (hd_error & BBD_ERR)) {
		end_request(0);
		special_op[dev] = recalibrate[dev] = 1;
	} else if (CURRENT->errors % RESET_FREQ == 0)
		reset = 1;
	else if ((hd_error & TRK0_ERR) || CURRENT->errors % RECAL_FREQ == 0)
		special_op[dev] = recalibrate[dev] = 1;
	/* Otherwise just retry */
}

//wait_DRQ()对数据请求位进行测试
/*DRQ_STAT是硬盘状态寄存器的请求服务器位，表示驱动器已经准备好在主机和数据端口之间传输一个字
或一个字节的数据*/
static inline int wait_DRQ(void)
{
	int retries = 100000, stat;

	while (--retries > 0)
		if ((stat = inb_p(HD_STATUS)) & DRQ_STAT)
			return 0;
	dump_status("wait_DRQ", stat);
	return -1;
}

//读操作中断调用函数，功能：从硬盘读数据到缓冲区
/*
	该函数将在硬盘读命令结束时引发的中断过程中被调用。
	在读命令执行完毕后会产生硬盘中断信号，并执行硬盘
	中断处理程序，此时在硬盘中断处理程序中调用的C函数
	指针do_hd已经指向read_intr()(在do_hd_request()
	函数中由hd_out()设置)。因此会在一次读读扇区操作完
	成（或出错）后就会执行该函数
	
	笔者认为，只要理解了本函数中的read_intr()和write_intr()
	函数的执行机制，便可深刻理解硬盘读写机制了。
*/
static void read_intr(void)
{	
	//取硬盘号（0/1）
	unsigned int dev = DEVICE_NR(CURRENT->dev);
	int i, retries = 100000, msect = mult_count[dev], nsect;

	if (unmask_intr[dev])
		sti();			/* permit other IRQs during xfer */
	do {
		i = (unsigned) inb_p(HD_STATUS);
/*
		当驱动器接受了读命令，就会设置BUSY_STAT标志并且立即开始执行命令。
		但在完成命令后要清BUSY_STAT位并产生中断的，即使是这样，为了以防
		万一，仍需要判断BUSY_STAT，等待驱动器清此位。
*/
		if (i & BUSY_STAT)
			continue;
/*
		在读扇区的过程中，若发生错误，驱动器就会设置出错比特位，设置DRQ_STAT
		位并且产生一个中断。不管是否发生错误，驱动器总是会在读扇区后设置DRQ_STAT
		比特位。所以在判断DRQ_STAT标志之前需要判断是否有出错标志。
*/
		if (!OK_STATUS(i))
			break;
		//执行到这里，说明驱动器正确的完成了读命令，并将要读取的扇区数据放入了其缓冲区
		//所以要跳转到ok_to_read处进行处理。
		if (i & DRQ_STAT)
			goto ok_to_read;
	} while (--retries > 0);
	dump_status("read_intr", i);
	bad_rw_intr();
	hd_request();
	return;
ok_to_read:
	//如果msect不为0,表示此次读取是多扇区读操作，就是硬盘控制器内部缓冲区一次性可以
	//容纳不止一个扇区的数据，而是多个扇区的数据。
	if (msect) {
		//如果当前操作的请求项中的缓冲区所对应的扇区数大于设备一次性最多可操作的扇区数
		if ((nsect = CURRENT->current_nr_sectors) > msect)
			nsect = msect;	//设置此次要读取的扇区数
		msect -= nsect;	//msect中保存的是还剩下的下次要读取的扇区数
	} else	//否则表示是单扇区操作
		nsect = 1;
	//将数据从硬盘控制器内部缓冲区读入指定的内存缓冲区中
/*	
	在这里有个疑问。就是当要读取多个扇区的时候，比如要读取10个扇区的数据
	当没有开启硬盘mult_mode的时候，一次读取一个扇区并产生中断，而当
	mult_mode开启的时候，假设一次读取两个扇区数据到控制器内部缓冲中
	但是，因为是要读连续的10个扇区，剩下的扇区怎么同步？就是中断CPU
	然后到达这里读取数据，但是硬盘控制器还要继续读取剩下的扇区的，而此时
	硬盘控制器缓冲中已经存满了数据，硬盘控制器继续读取硬盘剩下的扇区的数据，
	存放在哪？或者说，什么时候开始读剩下的扇区？貌似硬盘没有提供直接的
	状态标志。但我想，只是我的猜测，应该是这里的读取硬盘控制器内部缓冲
	的命令insw()，当用此命令完全将硬盘缓冲中的数据读入内存后，驱动器复位
	DRQ_STAT和BUSY_STAT，然后继续读取剩下的扇区。硬盘控制器内部应该有一个
	寄存器记录insw()命令每次读取数据读到的位置，当此寄存器值为0或其他表明
	读取数据完毕的值时，驱动控制器将开始读取下一个或下一组扇区
	
*/
	insw(HD_DATA,CURRENT->buffer,nsect<<8);
	//更新当前要读取的扇区号
	CURRENT->sector += nsect;
	CURRENT->buffer += nsect<<9;
	CURRENT->errors = 0;
	//i表示当前请求项中还剩下未读取到的扇区数
	i = (CURRENT->nr_sectors -= nsect);

#ifdef DEBUG
	printk("hd%c: read: sectors(%ld-%ld), remaining=%ld, buffer=0x%08lx\n",
		dev+'a', CURRENT->sector, CURRENT->sector+nsect,
		CURRENT->nr_sectors, (unsigned long) CURRENT->buffer+(nsect<<9));
#endif
	//如果下面的if语句为真，则i<=0
	if ((CURRENT->current_nr_sectors -= nsect) <= 0)
		end_request(1);	//表示当前处理的缓冲区有效，处理下一个缓冲区或者下一个请求项
	//如果当前请求项中还有要读取的扇区数
	if (i > 0) {
		//如果msect不为0,表示多扇区读，硬盘缓冲区中还有未读取的扇区
		if (msect)
			goto ok_to_read;	//返回继续读取硬盘缓冲中的扇区数据
		//执行到这里，说明不管是单扇区读还是多扇区读，一次性读取到硬盘缓冲区中的所有
		//扇区数据都成功的复制到了内存缓冲区中，但是请求项中还有要读取的扇区
		//所以继续将read_intr设置为中断处理函数
		SET_INTR(&read_intr);
		//中断返回，CPU转而去做其他事情。而硬盘控制器在此期间仍继续读取下一个扇区（对于一次只读取一个扇区到内部缓冲）
		//或继续读下一组扇区到内部缓冲区。
		//因为在同一个请求项内的缓冲区所对应的扇区号是相邻并且递增的，所以，只要想硬盘控制器发送读取扇区
		//起始号和读取的扇区扇区数就可以了，硬盘控制器在完成一次读操作（读取一个或多个扇区）后会中断CPU
		return;
	}
	//执行到这里，说明处理完了一个请求项
	//这里读取状态寄存器的值，可能是为了复位某些状态标记
	(void) inb_p(HD_STATUS);
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	//如果此设备的请求项链表中还有请求项，则继续处理
	if (CURRENT)
		hd_request();
	return;
}

//当muti_mode硬盘读写模式开启时，要调用的将内存缓冲区数据写入控制器内部缓冲区的函数
static inline void multwrite (unsigned int dev)
{
	//取一次可以写入的扇区数
	unsigned int mcount = mult_count[dev];

	//循环写入，每次写一个扇区的数据量
	while (mcount--) {
		outsw(HD_DATA,WCURRENT.buffer,256);
		//如果当前请求项中的所有扇区都写完了
		if (!--WCURRENT.nr_sectors)
			return;
		WCURRENT.buffer += 512;
		//如果当前请求项的当前缓冲区写入完毕
		if (!--WCURRENT.current_nr_sectors) {
			//bh指向当前请求项的当前缓冲区的下一个缓冲区
			WCURRENT.bh = WCURRENT.bh->b_reqnext;
			//如果下一个缓冲区为空，即不存在
			if (WCURRENT.bh == NULL)
				panic("buffer list corrupted\n");
			WCURRENT.current_nr_sectors = WCURRENT.bh->b_size>>9;
			WCURRENT.buffer = WCURRENT.bh->b_data;
		}
	}
}

//在开启了硬盘控制器的muti_mode之后，要调用的写硬盘中断处理函数
static void multwrite_intr(void)
{
	int i;
	unsigned int dev = DEVICE_NR(WCURRENT.dev);

	if (unmask_intr[dev])
		sti();
	//读取硬盘控制器状态，如果没有出错
	if (OK_STATUS(i=inb_p(HD_STATUS))) {
		//如果DRQ_STAT置位，表示驱动器准备好了从主机接受下一组数据（多个扇区）
		if (i & DRQ_STAT) {
			//如果当前请求项中还有未处理的扇区
			if (WCURRENT.nr_sectors) {
				//调用multwrite将当前请求项中的数据写入驱动器内部缓冲
				multwrite(dev);
				//设置multwrite_intr函数为中断处理函数，因为驱动器在
				//将内部缓冲中的数据写入硬盘后，会产生中断
				SET_INTR(&multwrite_intr);
				return;
			}
		//DRQ_STAT没有置位
		} else {
			//如果当前请求项中的所有数据已写入硬盘
			if (!WCURRENT.nr_sectors) {	/* all done? */
				for (i = CURRENT->nr_sectors; i > 0;){
					i -= CURRENT->current_nr_sectors;
					end_request(1);
				}
#if (HD_DELAY > 0)
				last_req = read_timer();
#endif
				if (CURRENT)
					hd_request();
				return;
			}
		}
	}
	//执行到这里，说明驱动器写入数据出错了
	dump_status("multwrite_intr", i);
	bad_rw_intr();
	hd_request();
}

//功能： 从缓冲区读数据到硬盘
static void write_intr(void)
{
	int i;
	int retries = 100000;

	if (unmask_intr[DEVICE_NR(WCURRENT.dev)])
		sti();
/*
	在多扇区写操作期间，除了对第一个扇区的操作，当驱动器准备好从主机接受一个扇区的
	数据时就会设置DRQ_STAT标志，清BUSY_STAT标志并产生一个中断（从而执行此中断处理
	程序）。一旦一个扇区传送完毕，驱动器就会设置BUSY_STAT标志，并复位DRQ_STAT，从
	而转去将数据从内部缓冲区写入到硬盘中。
*/
	do {
		i = (unsigned) inb_p(HD_STATUS);
		if (i & BUSY_STAT)
			continue;
		//执行到这里，说明驱动器清了BUSY_STAT标志。
		if (!OK_STATUS(i))
			break;
		//执行到这里，说明驱动器已将数据正确的写入了硬盘。
		//并且驱动器要准备接受下一个扇区数据写入其内部缓冲了
		//如果DRQ_STAT置位，表明驱动器已经做好从主机接受一个
		//扇区的数据了。

/*
		当最后一个扇区被写到磁盘上后，驱动器会清BUSY_STAT，并产生一个中断
		（从而执行本中断处理程序），但这次的中断会将DRQ_STAT复位（这个很好理解
		，因为指定的扇区数已经全部写入硬盘了，当然没有必要再置DRQ_STAT以等待主
		机项驱动器内部缓冲写入数据了），所以需要判断是不是最后一次写入的扇区
		(CURRENT->nr_sectors <= 1)，若是，也要正常处理
*/
		if ((CURRENT->nr_sectors <= 1) || (i & DRQ_STAT))
			goto ok_to_write;
	} while (--retries > 0);
	//执行到这里，说明硬盘写入数据出错了
	dump_status("write_intr", i);
	bad_rw_intr();
	hd_request();
	return;
ok_to_write:
	//执行到这里，说明上次写入驱动器内部缓冲区的数据已成功写入硬盘

	//更新当前请求项当前要操作的扇区号
	CURRENT->sector++;
	//i表示当前请求项中剩余的还未写入硬盘的扇区数
	i = --CURRENT->nr_sectors;
	//减少当前请求项中当前被处理的缓冲区所对应的扇区数
	--CURRENT->current_nr_sectors;
	//增加要处理的内存缓冲区的起始数据位置
	//因为每次要写入一个扇区即512字节，所以要加512
	CURRENT->buffer += 512;
	//如果当前请求项中的所有数据都被写入了硬盘
	//或者当前请求项中的当前处理的bh处理完毕
	//那么调用end_request()处理有关的结束事宜
	if (!i || (CURRENT->bh && !SUBSECTOR(i)))
		end_request(1);
	//如果当前请求项中还存在剩余的还未写入硬盘的扇区数
	if (i > 0) {
		//重新将write_intr设置为中断处理函数
		SET_INTR(&write_intr);
		//写要处理的下一个缓冲区数据到驱动器内部缓冲区中
		//当写入完毕，驱动器将自动将这些数据写入硬盘扇区
		outsw(HD_DATA,CURRENT->buffer,256);
		sti();
	//否则，当前请求项处理完毕
	} else {
#if (HD_DELAY > 0)
		last_req = read_timer();
#endif
		//继续处理下一个请求项
		hd_request();
	}
	//中断返回
	return;
}

//功能：重新进行硬盘的本次操作
static void recal_intr(void)
{
	check_status();
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	hd_request();
}

/*
 * This is another of the error-routines I don't know what to do with. The
 * best idea seems to just set reset, and start all over again.
 */
static void hd_times_out(void)
{
	unsigned int dev;

	DEVICE_INTR = NULL;
	if (!CURRENT)
		return;
	disable_irq(HD_IRQ);
	sti();
	reset = 1;
	dev = DEVICE_NR(CURRENT->dev);
	printk("hd%c: timeout\n", dev+'a');
	if (++CURRENT->errors >= MAX_ERRORS) {
#ifdef DEBUG
		printk("hd%c: too many errors\n", dev+'a');
#endif
		end_request(0);
	}
	cli();
	hd_request();
	enable_irq(HD_IRQ);
}

int do_special_op (unsigned int dev)
{
	if (recalibrate[dev]) {
		recalibrate[dev] = 0;
		hd_out(dev,hd_info[dev].sect,0,0,0,WIN_RESTORE,&recal_intr);
		return reset;
	}
	//从这里可以看出，IDE硬盘信息的获取，系统只做一次(初始化了hd_ident_info数组)
	//因为identified是本地static变量，且仅在这里被引用
	if (!identified[dev]) {
		identified[dev]  = 1;
		unmask_intr[dev] = DEFAULT_UNMASK_INTR;
		mult_req[dev] = DEFAULT_MULT_COUNT;
		/* get IDE identification info */
		//当硬盘控制器将硬盘信息（512bytes）读入内部缓冲区后中断CPU
		//CPU继而执行中断处理函数identify_intr()
		hd_out(dev,0,0,0,0,WIN_IDENTIFY,&identify_intr);
		return reset;
	}
	if (mult_req[dev] != mult_count[dev]) {
		hd_out(dev,mult_req[dev],0,0,0,WIN_SETMULT,&set_multmode_intr);
		return reset;
	}
	if (hd_info[dev].head > 16) {
		printk ("hd%c: cannot handle device with more than 16 heads - giving up\n", dev+'a');
		end_request(0);
	}
	special_op[dev] = 0;
	return 1;
}

/*
 * The driver enables interrupts as much as possible.  In order to do this,
 * (a) the device-interrupt is disabled before entering hd_request(),
 * and (b) the timeout-interrupt is disabled before the sti().
 *
 * Interrupts are still masked (by default) whenever we are exchanging
 * data/cmds with a drive, because some drives seem to have very poor
 * tolerance for latency during I/O.  For devices which don't suffer from
 * that problem (most don't), the unmask_intr[] flag can be set to unmask
 * other interrupts during data/cmd transfers (by defining DEFAULT_UNMASK_INTR
 * to 1, or by using "hdparm -u1 /dev/hd?" from the shell).
 */

/*
	在研究本文件代码时，想到一个费解的问题。内核在处理请求项以及请求项内部的缓冲区链表
	的过程中，并没有对其进行加锁，这就会出现这样的情况：当内核调用hd_request()函数处理
	当前设备的请求链表中的第一个请求项（我们称之为当前请求项）时，内核发送相应的命令给
	硬盘控制器之后，转而去做其他的事情。而在这个过程中，内核可能又会收到其他进程的读写
	请求，并且内核为之分配一个新的请求项，然后插入对应的请求项链表中，虽然内核用开关中断
	的方式保证了内核链表数据结构的操作的原子性，但是，仍然不能避免这样的情况，那就是这个
	新的请求项可能插入到请求项链表当前项之前，成为或者说覆盖当前项，那么，问题就来了，之后
	硬盘控制器完成了相应的读写操作，然后产生中断，内核相应的中断程序（可以参见本文件中的
	xxx_intr()函数）,会将控制器内部缓冲中的数据读入到当前项的缓冲区中。但是，此时此请求项
	链表的当前请求项已经变了啊！还有一种情况就是对于当前请求项的当前正在处理的缓冲区，因为
	一个请求项内部可能存在多个缓冲区，所以形成了一个缓冲区链表，而且此缓冲区链表是以这样
	一种顺序排列的---其代表的相应的硬盘扇区号以升序排列并且相邻的两个扇区号相差1，即
	如果第一个缓冲区代表四个扇区，第二个缓冲区代表3个扇区，只是假设，那么假设第一个缓冲区
	的第一个扇区号为5，那么其剩下的扇区号必须依次是6，7，8，而第二个缓冲区的扇区号必须是
	9，10，11。这个缓冲区链表由于也存在当前缓冲区，即第一个缓冲区，所以同样会出现上面所说的
	问题，内核在hd_request()函数中向控制器发送了相应的命令，这个命令对应的扇区号是此请求项
	的当前缓冲区所对应的扇区，然后控制器执行相应的命令，而CPU转而去做其他的事情，但是，问题	
	又来了，当其他的进程发出了一个读写请求项时，内核在相应的设备请求链表中找到了可以合并此
	请求缓冲区的请求项的请求项，注意，此缓冲区可以插入到此请求项的内部缓冲区链表，而且只能是
	链表头部或者尾部（因为此链表的排序特性），插入到尾部不会产生任何问题，因为虽然内核执行当
	前请求项时想控制器发送的读写命令是针对当前请求项的，包括读写的开始扇区，读写的扇区数，虽然
	将缓冲区插入了尾部，增加了请求项的总扇区数，但是当内核“处理完”当前请求项时，由end_request()
	函数决定了新插入的缓冲区将独占此请求项，并且成为下一个要处理的当前请求项。但是要将其插入
	或者说覆盖掉当前请求项的当前缓冲区，就可能会出现问题了。
	
	所以内核要在插入新的请求项时，要跳过当前请求项，当要插入一个新的缓冲区时到一个已存在的请求
	项中时，也跳过了当前请求项，即从第二个请求项开始，以避免将缓冲区插入到当前请求项的缓冲链表头部，
	内核做的比较极端，也不会将一个缓冲区加入到当前请求项的缓冲区链表尾部。
	
	总起来说，就是内核对请求项链表操作，避开了第一项。同时，内核使用关中断的方式，保证对请求项链表的插入删除操作
*/
static void hd_request(void)
{
	unsigned int dev, block, nsect, sec, track, head, cyl;

	if (CURRENT && CURRENT->dev < 0) return;
	if (DEVICE_INTR)
		return;
repeat:
	timer_active &= ~(1<<HD_TIMER);
	sti();
	INIT_REQUEST;
	if (reset) {
		cli();
		reset_hd();
		return;
	}
	dev = MINOR(CURRENT->dev);
	block = CURRENT->sector;
	nsect = CURRENT->nr_sectors;
	if (dev >= (NR_HD<<6) || block >= hd[dev].nr_sects || ((block+nsect) > hd[dev].nr_sects)) {
#ifdef DEBUG
		if (dev >= (NR_HD<<6))
			printk("hd: bad minor number: device=0x%04x\n", CURRENT->dev);
		else
			printk("hd%c: bad access: block=%d, count=%d\n",
				(CURRENT->dev>>6)+'a', block, nsect);
#endif
		end_request(0);
		goto repeat;
	}
	block += hd[dev].start_sect;
	dev >>= 6;
	if (special_op[dev]) {
		if (do_special_op(dev))
			goto repeat;
		return;
	}
	sec   = block % hd_info[dev].sect + 1;
	track = block / hd_info[dev].sect;
	head  = track % hd_info[dev].head;
	cyl   = track / hd_info[dev].head;
#ifdef DEBUG
	printk("hd%c: %sing: CHS=%d/%d/%d, sectors=%d, buffer=0x%08lx\n",
		dev+'a', (CURRENT->cmd == READ)?"read":"writ",
		cyl, head, sec, nsect, (unsigned long) CURRENT->buffer);
#endif
	if (!unmask_intr[dev])
		cli();
	if (CURRENT->cmd == READ) {
		unsigned int cmd = mult_count[dev] > 1 ? WIN_MULTREAD : WIN_READ;
		//向驱动器发送读命令，当驱动器接受了此命令，就会设置BUSY_STAT标志并开始执行此命令。
		//命令执行完毕将产生中断。对于muti_mode模式，驱动器每次读取多个扇区的数据到其内部
		//缓冲区中，读取完毕就会产生中断，其中断程序负责将控制器内部缓冲中的数据读取到内存
		//缓冲区中，然后判断是否还有扇区要读取，如果有，则重新设置中断处理程序（因为每次中断后
		//中断程序变为无效，必须重新设置）。而对于非muti_mode模式，控制器每次读取一个扇区的数据到
		//内部缓冲区中，然后产生中断，若以这样的方式读取10个扇区，则会产生十次中断。
		hd_out(dev,nsect,sec,head,cyl,cmd,&read_intr);
		if (reset)
			goto repeat;
		return;
	}
	if (CURRENT->cmd == WRITE) {
		if (mult_count[dev])
			hd_out(dev,nsect,sec,head,cyl,WIN_MULTWRITE,&multwrite_intr);
		else
			hd_out(dev,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
		if (reset)
			goto repeat;
/*
		DRQ_STAT是硬盘状态寄存器的请求服务器位，表示驱动器已经
		准备好在主机和数据端口之间传输一个字或一个字节的数据。
		当驱动器接受了读命令，它将设置DRQ_STAT并等待扇区缓冲区
		被填满数据。在开始第一次向扇区缓冲区填入数据时并不会产
		生中断，一旦数据填满驱动器就会复位DRQ_STAT，设置BUSY_STAT
		标志并开始执行命令。
*/
		if (wait_DRQ()) {
			bad_rw_intr();
			goto repeat;
		}
		//向扇区缓冲区填入数据
	
		//如果开启了muti_mode模式,就可以一次写入多个扇区的数据
		if (mult_count[dev]) {
			//从这里可以看出，WCURRENT是当前请求项的一个副本
			//为什么不用CURRENT指针操作而用一个当前请求项的副本呢？
			WCURRENT = *CURRENT;
			multwrite(dev);
		//否则，只能一次写入一个扇区的数据
		} else
			outsw(HD_DATA,CURRENT->buffer,256);
		return;
	}
	panic("unknown hd-command");
}

static void do_hd_request (void)
{
	disable_irq(HD_IRQ);
	hd_request();
	enable_irq(HD_IRQ);
}

static int hd_ioctl(struct inode * inode, struct file * file,
	unsigned int cmd, unsigned long arg)
{
	struct hd_geometry *loc = (struct hd_geometry *) arg;
	int dev, err;
	unsigned long flags;

	if ((!inode) || (!inode->i_rdev))
		return -EINVAL;
	dev = DEVICE_NR(inode->i_rdev);
	if (dev >= NR_HD)
		return -EINVAL;
	switch (cmd) {
		case HDIO_GETGEO:
			if (!loc)  return -EINVAL;
			err = verify_area(VERIFY_WRITE, loc, sizeof(*loc));
			if (err)
				return err;
			put_fs_byte(bios_info[dev].head,
				(char *) &loc->heads);
			put_fs_byte(bios_info[dev].sect,
				(char *) &loc->sectors);
			put_fs_word(bios_info[dev].cyl,
				(short *) &loc->cylinders);
			put_fs_long(hd[MINOR(inode->i_rdev)].start_sect,
				(long *) &loc->start);
			return 0;
		case BLKRASET:
			if(!suser())  return -EACCES;
			if(arg > 0xff) return -EINVAL;
			read_ahead[MAJOR(inode->i_rdev)] = arg;
			return 0;
		case BLKRAGET:
			if (!arg)  return -EINVAL;
			err = verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
			if (err)
				return err;
			put_fs_long(read_ahead[MAJOR(inode->i_rdev)],(long *) arg);
			return 0;
         	case BLKGETSIZE:   /* Return device size */
			if (!arg)  return -EINVAL;
			err = verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
			if (err)
				return err;
			put_fs_long(hd[MINOR(inode->i_rdev)].nr_sects, (long *) arg);
			return 0;
		case BLKFLSBUF:
			if(!suser())  return -EACCES;
			fsync_dev(inode->i_rdev);
			invalidate_buffers(inode->i_rdev);
			return 0;

		case BLKRRPART: /* Re-read partition tables */
			return revalidate_hddisk(inode->i_rdev, 1);

		case HDIO_SET_UNMASKINTR:
			if (!suser()) return -EACCES;
			if ((arg > 1) || (MINOR(inode->i_rdev) & 0x3F))
				return -EINVAL;
			unmask_intr[dev] = arg;
			return 0;

                case HDIO_GET_UNMASKINTR:
			if (!arg)  return -EINVAL;
			err = verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
			if (err)
				return err;
			put_fs_long(unmask_intr[dev], (long *) arg);
			return 0;

                case HDIO_GET_MULTCOUNT:
			if (!arg)  return -EINVAL;
			err = verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
			if (err)
				return err;
			put_fs_long(mult_count[dev], (long *) arg);
			return 0;

		case HDIO_SET_MULTCOUNT:
			if (!suser()) return -EACCES;
			if (MINOR(inode->i_rdev) & 0x3F) return -EINVAL;
			save_flags(flags);
			cli();	/* a prior request might still be in progress */
			if (arg > max_mult[dev])
				err = -EINVAL;	/* out of range for device */
			else if (mult_req[dev] != mult_count[dev]) {
				special_op[dev] = 1;
				err = -EBUSY;	/* busy, try again */
			} else {
				mult_req[dev] = arg;
				special_op[dev] = 1;
				err = 0;
			}
			restore_flags(flags);
			return err;

		case HDIO_GET_IDENTITY:
			if (!arg)  return -EINVAL;
			if (MINOR(inode->i_rdev) & 0x3F) return -EINVAL;
			if (hd_ident_info[dev] == NULL)  return -ENOMSG;
			err = verify_area(VERIFY_WRITE, (char *) arg, sizeof(struct hd_driveid));
			if (err)
				return err;
			memcpy_tofs((char *)arg, (char *) hd_ident_info[dev], sizeof(struct hd_driveid));
			return 0;

		RO_IOCTLS(inode->i_rdev,arg);
		default:
			return -EINVAL;
	}
}
/*
	我们来看一下 hd_open() 和 hd_release() 函数，打开操作首先检测
	了设备的有效性，接着测试了它的忙标志，最后对请求硬盘的总数加1，来
	标识对硬盘的请求个数，hd_release() 函数则将请求的总数减1。
*/
static int hd_open(struct inode * inode, struct file * filp)
{
	int target;
	target =  DEVICE_NR(inode->i_rdev);

	while (busy[target])
		sleep_on(&busy_wait);
	access_count[target]++;
	return 0;
}

/*
 * Releasing a block device means we sync() it, so that it can safely
 * be forgotten about...
 */
static void hd_release(struct inode * inode, struct file * file)
{
        int target;
	sync_dev(inode->i_rdev);

	target =  DEVICE_NR(inode->i_rdev);
	access_count[target]--;

}

static void hd_geninit(void);

static struct gendisk hd_gendisk = {
	MAJOR_NR,	/* Major number */	
	"hd",		/* Major name */
	6,		/* Bits to shift to get real from partition */
	1 << 6,		/* Number of partitions per real */
	MAX_HD,		/* maximum number of real */
	hd_geninit,	/* init function */
	hd,		/* hd struct */
	hd_sizes,	/* block sizes */
	0,		/* number */
	(void *) bios_info,	/* internal */
	NULL		/* next */
};
	
//功能：决定硬盘中断所要调用的中断程序
/*
	在注册的时候，同硬盘中断联系的是 hd_interupt()，也就是说当硬盘
	中断到来的时候，执行的函数是hd_interupt()，在此函数中调用 
	DEVICE_INTR所指向的中断函数，如果 DEVICE_INTR 为空，则执行
	unexpected_hd_interrupt()函数。 
*/
static void hd_interrupt(int irq, struct pt_regs *regs)
{
	void (*handler)(void) = DEVICE_INTR;

	DEVICE_INTR = NULL;
	timer_active &= ~(1<<HD_TIMER);
	if (!handler)
		handler = unexpected_hd_interrupt;
	handler();
	sti();
}

/*
 * This is the harddisk IRQ description. The SA_INTERRUPT in sa_flags
 * means we run the IRQ-handler with interrupts disabled: this is bad for
 * interrupt latency, but anything else has led to problems on some
 * machines...
 *
 * We enable interrupts in some of the routines after making sure it's
 * safe.
 */
static void hd_geninit(void)
{
	int drive, i;
	//driver_info是指向硬盘参数表结构的指针，该硬盘参数表结构包含2个硬盘的参数表的信息
	//每个16字节，共32字节。
	extern struct drive_info drive_info;
	unsigned char *BIOS = (unsigned char *) &drive_info;
	int cmos_disks;

	if (!NR_HD) {	   
		for (drive=0 ; drive<2 ; drive++) {
			bios_info[drive].cyl   = hd_info[drive].cyl = *(unsigned short *) BIOS;//柱面数
			bios_info[drive].head  = hd_info[drive].head = *(2+BIOS);//磁头数
			bios_info[drive].wpcom = hd_info[drive].wpcom = *(unsigned short *) (5+BIOS);//写前预补偿柱面号
			bios_info[drive].ctl   = hd_info[drive].ctl = *(8+BIOS);//控制字节
			bios_info[drive].lzone = hd_info[drive].lzone = *(unsigned short *) (12+BIOS);//磁头着陆区柱面号
			bios_info[drive].sect  = hd_info[drive].sect = *(14+BIOS);//每磁道扇区数
#ifdef does_not_work_for_everybody_with_scsi_but_helps_ibm_vp
			//内核在取BIOS硬盘参数表信息时，如果系统中只有一个硬盘，就会将对应的第二个硬盘
			//的16字节全部清零。因此，这里只要判断第二个硬盘的柱面数是否为0就可以知道是否有第二个硬盘了
			if (hd_info[drive].cyl && NR_HD == drive)
				NR_HD++;
#endif
			//因为每个硬盘参数表长度为16字节，所以这里要指向下一个硬盘参数表
			BIOS += 16;
		}

	/*
		We query CMOS about hard disks : it could be that 
		we have a SCSI/ESDI/etc controller that is BIOS
		compatible with ST-506, and thus showing up in our
		BIOS table, but not register compatible, and therefore
		not present in CMOS.

		Furthermore, we will assume that our ST-506 drives
		<if any> are the primary drives in the system, and 
		the ones reflected as drive 1 or 2.

		The first drive is stored in the high nibble of CMOS
		byte 0x12, the second in the low nibble.  This will be
		either a 4 bit drive type or 0xf indicating use byte 0x19 
		for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.

		Needless to say, a non-zero value means we have 
		an AT controller hard disk for that drive.

		
	*/
		//从CMOS偏移地址0x12处读出硬盘类型字节。如果低半字节值（存放着第二个硬盘类型值）不为0
		//则表示系统有两个硬盘，否则表示系统只有一个硬盘。如果0x12处读出的值为0,则表示系统没有
		//AT兼容硬盘
		if ((cmos_disks = CMOS_READ(0x12)) & 0xf0)
			if (cmos_disks & 0x0f)
				NR_HD = 2;
			else
				NR_HD = 1;
	}
/*
	到这里，硬盘的信息数组hd_info[]已经设置好，并且确定了系统中含有的硬盘数NR_HD
	现在开始设置硬盘分区结构数组hd[]。
*/
	i = NR_HD;
	while (i-- > 0) {
		/*
		 * The newer E-IDE BIOSs handle drives larger than 1024
		 * cylinders by increasing the number of logical heads
		 * to keep the number of logical cylinders below the
		 * sacred INT13 limit of 1024 (10 bits).  If that is
		 * what's happening here, we'll find out and correct
		 * it later when "identifying" the drive.
		 */
		//i<<6表示只设置表示硬盘整体信息的项
		//这里为什么没有设置硬盘的起始扇区数？因为在genhd.c中的setup_dev()
		//函数中已经将hd数组清零
/*
		这里并没有设置各个硬盘的分区信息项，这个工作是在genhd.c中做的
		系统没添加一个分区就填充一个分区信息项。通过hd_gendisk的part项，
		在这里hd_gendisk.part就是hd数组。
*/
		hd[i<<6].nr_sects = bios_info[i].head *
				bios_info[i].sect * bios_info[i].cyl;	//求得硬盘总扇区数
		//为此硬盘分配一个表示其信息的项
		hd_ident_info[i] = (struct hd_driveid *) kmalloc(512,GFP_KERNEL);
		special_op[i] = 1;
	}
	if (NR_HD) {
		if (request_irq(HD_IRQ, hd_interrupt, SA_INTERRUPT, "hd")) {
			printk("hd: unable to get IRQ%d for the harddisk driver\n",HD_IRQ);
			NR_HD = 0;
		} else {
			request_region(HD_DATA, 8, "hd");
			request_region(HD_CMD, 1, "hd(cmd)");
		}
	}
	//系统中真实的硬盘设备个数
	hd_gendisk.nr_real = NR_HD;

	//设置硬盘各个分区的块大小
	for(i=0;i<(MAX_HD << 6);i++) hd_blocksizes[i] = 1024;
	blksize_size[MAJOR_NR] = hd_blocksizes;
}

/*
	Linux 中，硬盘被认为是计算机的最基本的配置，所以在装载内核的时候，
	硬盘驱动程序必须就被编译进内核，不能作为模块编译。硬盘驱动程序
	提供内核的接口为：　 
*/
static struct file_operations hd_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	hd_ioctl,		/* ioctl */
	NULL,			/* mmap */
	hd_open,		/* open */
	hd_release,		/* release */
	block_fsync		/* fsync */
};

unsigned long hd_init(unsigned long mem_start, unsigned long mem_end)
{
	//调用register_blkdev()把硬盘信息登记到blkdevs数组中
	if (register_blkdev(MAJOR_NR,"hd",&hd_fops)) {
		printk("hd: unable to get major %d for harddisk\n",MAJOR_NR);
		return mem_start;
	}
	//设置blk_dev.request_fn，这个函数是设备处理请求的入口
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	read_ahead[MAJOR_NR] = 8;		/* 8 sector (4kB) read-ahead */
/*
	把hd_gendisk结构挂入gendisk_head链表，hd_gendisk是一个非常重要的结构，
   	其中一个重要的成员是init，对于硬盘来说就是hd_geninit，这个函数负责设置
	设备在IDT中的表项
*/
	hd_gendisk.next = gendisk_head;
	gendisk_head = &hd_gendisk;
	timer_table[HD_TIMER].fn = hd_times_out;
	return mem_start;
}

#define DEVICE_BUSY busy[target]
#define USAGE access_count[target]
#define CAPACITY (bios_info[target].head*bios_info[target].sect*bios_info[target].cyl)
/* We assume that the the bios parameters do not change, so the disk capacity
   will not change */
#undef MAYBE_REINIT
#define GENDISK_STRUCT hd_gendisk

/*
 * This routine is called to flush all partitions and partition tables
 * for a changed scsi disk, and then re-read the new partition table.
 * If we are revalidating a disk because of a media change, then we
 * enter with usage == 0.  If we are using an ioctl, we automatically have
 * usage == 1 (we need an open channel to use an ioctl :-), so this
 * is our limit.
 */
static int revalidate_hddisk(int dev, int maxusage)
{
	int target, major;
	struct gendisk * gdev;
	int max_p;
	int start;
	int i;
	long flags;

	target =  DEVICE_NR(dev);
	gdev = &GENDISK_STRUCT;

	save_flags(flags);
	cli();
	if (DEVICE_BUSY || USAGE > maxusage) {
		restore_flags(flags);
		return -EBUSY;
	};
	DEVICE_BUSY = 1;
	restore_flags(flags);

	max_p = gdev->max_p;
	start = target << gdev->minor_shift;
	major = MAJOR_NR << 8;

	for (i=max_p - 1; i >=0 ; i--) {
		sync_dev(major | start | i);
		invalidate_inodes(major | start | i);
		invalidate_buffers(major | start | i);
		gdev->part[start+i].start_sect = 0;
		gdev->part[start+i].nr_sects = 0;
	};

#ifdef MAYBE_REINIT
	MAYBE_REINIT;
#endif

	gdev->part[start].nr_sects = CAPACITY;
	resetup_one_dev(gdev, target);

	DEVICE_BUSY = 0;
	wake_up(&busy_wait);
	return 0;
}

