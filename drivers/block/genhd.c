/*
 *  Code extracted from
 *  linux/kernel/hd.c
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
/*
 *  Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  in the early extended-partition checks and added DM partitions
 */

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>


//想要支持分区的块设备驱动程序要包含<linux/genhd.h>，并声明结构gendisk。
//所有这样的结构被组织在一个链表中，它的头是全局指针gendisk_head。
struct gendisk *gendisk_head = NULL;

static int current_minor = 0;
extern int *blk_size[];
extern void rd_load(void);
extern int ramdisk_size;

/*
		分区表的位置及识别标志 
	分区表一般位于硬盘某柱面的0磁头 1扇区。而第1个分区表（主分区表）总是
	位于（0柱面，1磁头，1扇区），剩余的分区表位置可以由主分区表依次推导出来。
	分区表有64个字节，占据其所在扇区的[441-509]字节。要判定是不是分区表，就
	看其后紧邻的两个字节（[510-511]）是不是 "55AA"，若是，则为分区表。

		分区表的结构
	分区表由4项组成，每项16个字节。共4×16 = 64个字节，每项描述一个分区的基本信息。
	第0个字节：Activeflag，活动标志。若为0x80H，则表示该分区为活动分区；若为0x00H，则为非活动分区。
	第1、2、3字节：该分区的起始磁头号、扇区号、柱面号。第1字节为磁头号，取值范围是 0 磁头 -- 254 磁头；
	第2字节的低6位为扇区号，取值范围是 1 扇区 -- 63 扇区；第2字节的高2位与第3字节共同表示柱面号，取值
	范围是 0 柱面 -- 1023 柱面。第4字节：分区文件系统标志。0x00H表示分区未用；0x05H、0x0FH表示扩展分区；
	0x06H表示FAT16分区；0x0BH、0x1BH、0x0CH、0x1CH表示FAT32分区；	0x07H表示NTFS分区。
	第5、6、7字节：该分区的结束磁头号、扇区号、柱面号。含义参看第1、2、3字节。
	第8、9、10、11字节：逻辑起始扇区号。表示分区起点之前已用了的扇区数。如果是主分区表，则这 4 个字节表
	示该分区起始逻辑扇区号与逻辑 0 扇区（0柱面 0磁头 1扇区）之差；如果非主分区表，则这 4 个字节要么表示该
	分区起始逻辑扇区号与扩展分区起始逻辑扇区号之差，要么为63。
	第12、13、14、15字节：该分区所占用的扇区数

		关于移植
	需要注意的是可分区模块不能被加载到核心的1.2版，因为符号resetup_one_dev（在本节后面介绍）没有被引出到模块。
	在对SCSI盘的支持模块化之前，没有人会考虑可分区的模块。
 
*/

static char minor_name (struct gendisk *hd, int minor)
{
	char base_name = (hd->major == IDE1_MAJOR) ? 'c' : 'a';
	return base_name + (minor >> hd->minor_shift);
}

//添加磁盘分区信息的函数，负责向通用磁盘数据结构添加一个新的分区
static void add_partition (struct gendisk *hd, int minor, int start, int size)
{
	hd->part[minor].start_sect = start;
	hd->part[minor].nr_sects   = size;
	printk(" %s%c%d", hd->major_name, minor_name(hd, minor),
		minor & ((1 << hd->minor_shift) - 1));
}
/*
 * Create devices for each logical partition in an extended partition.
 * The logical partitions form a linked list, with each entry being
 * a partition table with two entries.  The first entry
 * is the real data partition (with a start relative to the partition
 * table start).  The second is a pointer to the next logical partition
 * (with a start relative to the entire extended partition).
 * We do not create a Linux partition for the partition tables, but
 * only for the actual data partitions.
 */
/*
		分区表链的查找 
	分区表链实际上相当于一个单向链表结构。第一个分区表，也即主分区表，
	可以有一项描述扩展分区。而这一项就相当于指针，指向扩展分区。然后
	我们根据该指针来到扩展分区起始柱面的0头1扇区，找到第二个分区表。
	对于该分区表，通常情况下：第一项描述了扩展分区中第一个分区的信息，
	第二项描述下一个分区，而这第二项就相当于指向第二个分区的指针，第
	三项，第四项一般均为0。我们可以根据该指针来到扩展分区中第二个分区
	起始柱面的0头1扇区，找到第三个分区表。以此类推，直到最后一个分区表。
	而最后一个分区表只有第一项有信息，余下三项均为0。相当于其指针为空。
	所以只要找到了一个分区表就可以推导找出其后面所有分区表。不过该分区
	表前面的分区表就不好推导出来了。但令人高兴的是这个链表的头节点，也
	即主分区表的位置是固定的位于（0柱面， 0磁头， 1扇区）处，我们可以
	很轻易的找到它，然后把剩下的所有分区表一一找到。
*/
//遍历此扩展分区链表，并将其逻辑分区信息加入分区表
static void extended_partition(struct gendisk *hd, int dev)
{
	struct buffer_head *bh;
	struct partition *p;
	unsigned long first_sector, this_sector;
	int mask = (1 << hd->minor_shift) - 1;

	first_sector = hd->part[MINOR(dev)].start_sect;
	this_sector = first_sector;

	while (1) {
		//max_p:maximum partitions per device
		if ((current_minor & mask) >= (4 + hd->max_p))
			return;
		//读取此分区设备的开头1024字节
		if (!(bh = bread(dev,0,1024)))
			return;
	  /*
	   * This block is from a device that we're about to stomp on.
	   * So make sure nobody thinks this block is usable.
	   */
		bh->b_dirt = 0;
		bh->b_uptodate = 0;
		bh->b_req = 0;
		if (*(unsigned short *) (bh->b_data+510) == 0xAA55) {
			p = (struct partition *) (0x1BE + bh->b_data);
		/*
		 * Process the first entry, which should be the real
		 * data partition.
		 */
			if (p->sys_ind == EXTENDED_PARTITION || !p->nr_sects)
				goto done;  /* shouldn't happen */
			//扩展分区中的逻辑分区中的分区表的第一项描述了此逻辑分区，将其加入分区表
			add_partition(hd, current_minor, this_sector+p->start_sect, p->nr_sects);
			current_minor++;
			p++;	//p此时指向第二项分区表项
		/*
		 * Process the second entry, which should be a link
		 * to the next logical partition.  Create a minor
		 * for this just long enough to get the next partition
		 * table.  The minor will be reused for the real
		 * data partition.
		 */
			//如果第二项分区表项不是扩展分区类型或者扇区数为0,表明逻辑分区链表遍历完毕
			//正是因为这里的判断，所以上面的判断注释说其“shouldn't happen”
			if (p->sys_ind != EXTENDED_PARTITION ||
			    !(hd->part[current_minor].nr_sects = p->nr_sects))
				goto done;  /* no more logicals in this partition */
			//否则，继续遍历分区链表
			hd->part[current_minor].start_sect = first_sector + p->start_sect;
			this_sector = first_sector + p->start_sect;
			//设置dev指向下一个分区
			dev = ((hd->major) << 8) | current_minor;
			brelse(bh);
		} else
			goto done;
	}
done:
	brelse(bh);
}
/*
	分区表上有四项，每一项表示一个分区，所以一个分区表最多只能表示4个分区。
	其中，主分区表上的4项用来表示主分区和扩展分区的信息。因为扩展分区最多
	只能有一个，所以硬盘最多可以有四个主分区或者三个主分区加一个扩展分区。
	其余的分区表是表示逻辑分区的。逻辑区都是位于扩展分区里面的，因此逻辑分
	区的个数没有限制。
	分区表所在扇区通常在（0磁头，1扇区），而该分区的开始扇区通常位于（1磁头
	，1扇区），中间隔了63 个隐藏扇区。
*/
//参数dev对于硬盘设备而言，是其主设备号+次设备号，注意此处的次设备号
//要去除其代表其逻辑设备号的低六位
static void check_partition(struct gendisk *hd, unsigned int dev)
{
	static int first_time = 1;
	//查阅本文件可知，在调用check_partition()函数之前，已经将current_minor
	//设置为了相应设备的第一个分区索引号。即对于第一个硬盘来说，
	//current_minor就是1,即hd[0<<6+1]。hd[0]描述了第一个硬盘的整体信息。
	//而hd[1]~hd[1<<6]描述了第一个硬盘的分区信息。进一步来说，hd[1]~hd[4]
	//保存了硬盘的主分区信息。剩下的保存了扩展分区的信息。
	int i, minor = current_minor;
	struct buffer_head *bh;
	struct partition *p;
	unsigned long first_sector;
	int mask = (1 << hd->minor_shift) - 1;

	if (first_time)
		printk("Partition check:\n");
	first_time = 0;
	//由dev参数的意义得知，此处取得的是真实硬盘设备的总体信息
	//即对第一个硬盘而言就是hd[0<<6]数组项，对第二个硬盘而言就是hd[1<<6]数组项
	//first_sector代表了真实硬盘的起始扇区号
	first_sector = hd->part[MINOR(dev)].start_sect;

	/*
	 * This is a kludge to allow the partition check to be
	 * skipped for specific drives (ie. IDE cd-rom drives)
	 */
	if ((int)first_sector == -1) {
		hd->part[MINOR(dev)].start_sect = 0;
		return;
	}

	//读取指定硬盘设备的前两个扇区数据，即开头的1024字节
	//因为第一个扇区可以存放系统引导代码，而第二个扇区可以存放分区表
	if (!(bh = bread(dev,0,1024))) {
		printk("  unable to read partition table of device %04x\n",dev);
		return;
	}
	printk("  %s%c:", hd->major_name, minor_name(hd, minor));
	//这里将current_minor+4是因为扩展逻辑扇区号是从5开始的
	//而在此之前已将current_minor的值赋给了minor
	current_minor += 4;  /* first "extra" minor (for extended partitions) */
	if (*(unsigned short *) (bh->b_data+510) == 0xAA55) {
		//p指向第一个硬盘分区表项
		p = (struct partition *) (0x1BE + bh->b_data);
		for (i=1 ; i<=4 ; minor++,i++,p++) {
			if (!p->nr_sects)
				continue;
			//将主分区信息加入分区表（最多四个）
			add_partition(hd, minor, first_sector+p->start_sect, p->nr_sects);
			//如果current_minor & 0x3f) >= 60表明其属于此设备的分区已经达到上限
			//（主分区占了四个扩展分区占了60个）
			if ((current_minor & 0x3f) >= 60)
				continue;
			//如果此硬盘分区表项表明其是扩展分区类型的分区
			if (p->sys_ind == EXTENDED_PARTITION) {
				printk(" <");
				//遍历此扩展分区链表，并将其逻辑分区加入分区表
				extended_partition(hd, (hd->major << 8) | minor);
				printk(" >");
			}
		}
		/*
		 * check for Disk Manager partition table
		 */
		//0xfc=252
		if (*(unsigned short *) (bh->b_data+0xfc) == 0x55AA) {
			//p指向第一个硬盘分区表项
			p = (struct partition *) (0x1BE + bh->b_data);
			for (i = 4 ; i < 16 ; i++, current_minor++) {
				p--;
				if ((current_minor & mask) >= mask-2)
					break;
				if (!(p->start_sect && p->nr_sects))
					continue;
				add_partition(hd, current_minor, p->start_sect, p->nr_sects);
			}
		}
	} else
		printk(" bad partition table");
	printk("\n");
	brelse(bh);
}

/* This function is used to re-read partition tables for removable disks.
   Much of the cleanup from the old partition tables should have already been
   done */

/* This function will re-read the partition tables for a given device,
and set things back up again.  There are some important caveats,
however.  You must ensure that no one is using the device, and no one
can start using the device while this function is being executed. */
//为单个物理设备扫描分区
void resetup_one_dev(struct gendisk *dev, int drive)
{
	int i;
	int start = drive<<dev->minor_shift;
	int j = start + dev->max_p;
	int major = dev->major << 8;

	current_minor = 1+(drive<<dev->minor_shift);
	check_partition(dev, major+(drive<<dev->minor_shift));

	for (i=start ; i < j ; i++)
		dev->sizes[i] = dev->part[i].nr_sects >> (BLOCK_SIZE_BITS - 9);
}

static void setup_dev(struct gendisk *dev)
{
	int i;
	int j = dev->max_nr * dev->max_p;
	int major = dev->major << 8;
	int drive;
	

	for (i = 0 ; i < j; i++)  {
		dev->part[i].start_sect = 0;
		dev->part[i].nr_sects = 0;
	}
	//设备初始化，对于硬盘就是hd_geninit()函数，检测系统中硬盘个数及其相关信息
	dev->init();
	//对于dev设备所对应的具体的物理设备，初始化其分区表
	for (drive=0 ; drive<dev->nr_real ; drive++) {
		current_minor = 1+(drive<<dev->minor_shift);
		check_partition(dev, major+(drive<<dev->minor_shift));
	}
	/* size of device（分区） in blocks */
	for (i=0 ; i < j ; i++)
		dev->sizes[i] = dev->part[i].nr_sects >> (BLOCK_SIZE_BITS - 9);
	blk_size[dev->major] = dev->sizes;
}

//磁盘初始化及分区检查
void device_setup(void)
{
	struct gendisk *p;
	int nr=0;

	for (p = gendisk_head ; p ; p=p->next) {
		setup_dev(p);
		nr += p->nr_real;
	}
		
	if (ramdisk_size)
		rd_load();
}
