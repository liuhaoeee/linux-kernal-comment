#define THREE_LEVEL
/*
 *  linux/mm/swap.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

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
 * This file should contain most things doing the swapping from/to disk.
 * Started 18.12.91
 */
 
 /*
												交换页分配方法 
	- 
	1) 交换空间由若干个块设备文件物理空间或普通文件的物理空间构成, 每一交换文件用交换结构(swap_info_struct)描述, 
	最多允许8个交换文件(MAX_SWAPFILES), 交换文件按其优先级(swap_info->prio)大小顺序排列在交换链表(swap_list->head)
	中, 当前使用的交换文件为(swap_list->next). 每一交换结构具有页分配表(swap_info->swap_map). 
	- 
	2) 在分配交换页时, 系统先在自由区内扫瞄一簇(256页)连续页块, 后继的分配页被约束在此分配簇中. 当没有分配簇可用时,
	 系统将在整个自由区内扫瞄. 每分配一页, 系统会切换到下一个相同优先级的交换文件进行分配. 当同一优先级的交换文件页
	 面用完后, 系统切换到更低一级的交换文件进行分配. 在添加交换文件时, 如果没有指定优先级, 系统使用递减的least_priority
	 分配优先级, 首先添加的交换文件具有较高的优先级. 
	- 
	3) 交换文件的起始页为交换头标(swap_header), 首页的最后10个字节为交换文件的特徵串. 特徵串"SWAP_SPACE"标识旧版的交换头标格式,
	 特徵串的前部为可用的交换页位图. 新版的交换文件使用"SWAPSPACE2"标识的头标, 它使用"坏页表"来标识坏页. 
	- 
	4) 交换空间用交换页目录项(swp_entry_t)寻址, 交换页目录项的最低位为零, 最低字节的剩余7位索引不同的交换文件, 
	剩余的字节代表交换文件内的页号. 当进程的某个物理页面被写入交换空间时, 该物理页面所在的页目录项被置换成交换页目录项, 
	交换页目录项的存在位为零, 当进程再次存取该页面时, 产生页不存在故障, 系统通过该交换页目录项从交换空间中读取页面内容, 
	并将新的物理页映射到故障区域. 
 */

/*
									交换高速缓存 
	*只有修改后的（脏）页面才保存在交换文件中。修改后的页面写入交换文件后，如果该页面再次被交换但未被修改时，
	*就没有必要写入交换文件，相反，只需丢弃该页面。交换高速缓存实际包含了一个页面表项链表，系统的每个物理页面
	*对应一个页面表项。对交换出的页面，该页面表项包含保存该页面的交换文件信息，以及该页面在交换文件中的位置信息。
	*如果某个交换页面表项非零，则表明保存在交换文件中的对应物理页面没有被修改。如果这一页面在后续的操作中被修改，
	*则处于交换缓存中的页面表项被清零。 Linux 需要从物理内存中交换出某个页面时，它首先分析交换缓存中的信息，
	*如果缓存中包含该物理页面的一个非零页面表项，则说明该页面交换出内存后还没有被修改过，这时，系统只需丢弃该页面。 
	*NOTE：交换高速缓存中保存的是swap磁盘页面指针并非物理页面 1.2交换高速缓冲是swap_cache数组结构而非链表
	
	向交换区来回传送页会引发很多竞争条件，具体地说，交换子系统必须仔细处理下面的情形： 
	多重换入：两个进程可能同时要换入同一个共享匿名页。 
	同时换入换出：一个进程可能换入正由PFRA换出的页。 
	交换高速缓存(swap cache)的引入就是为了解决这类同步问题。
	关键的原则是，没有检查交换高速缓存是否已包括了所涉及的页，
	就不能进行换入或换出操作。有了交换高速缓存，涉及同一页的
	并发交换操作总是作用于同一个页框的。因此，内核可以安全地
	依赖页描述符的PG_locked标志，以避免任何竞争条件。 
*/
 
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fs.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/bitops.h>
#include <asm/pgtable.h>

#define MAX_SWAPFILES 8

// 如果SWP_USED位置位，则表示该交换空间被占用。
#define SWP_USED	1
//如果SWP_WRITEOK位置位，则表示该交换空间准备就绪
#define SWP_WRITEOK	3

#define SWP_TYPE(entry) (((entry) >> 1) & 0x7f)
#define SWP_OFFSET(entry) ((entry) >> 12)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) << 12))

int min_free_pages = 20;

//静态变量(nr_swapfiles)来记录当前活动的交换文件数
static int nr_swapfiles = 0;
static struct wait_queue * lock_queue = NULL;

static struct swap_info_struct {
	//flags域(SWP_USED 或SWP_WRITEOK)用作控制访问交换文件。
	//当swapoff被调用（为了取消一个文件）时，SWP_WRITEOK置成 off，
	//使在文件中无法分配空间。如果swapon加入一个新的交换文件时，SWP_USED 被置位
	unsigned long flags;
	struct inode * swap_file;
	unsigned int swap_device;
	//swap_map指向一个数组，数组的大小为pages，数组中每个短整数代表了盘上（或者文件中）
	//的一个物理页面信息（页面使用计数），数组的下标表示其在交换设备上的位置。
	//而在每个交换设备上swap_map[0]是不用于页面交换的，它存储了自身的一些信息，
	//根据交换设备的不同，这些信息所占的空间也不一样
	//当用户程序调用swapon()时，有一页被分配给swap_map,swap_map为在交换文件中每
	//一个页插槽保留了一个字节，0代表可用页插槽，128代表不可用页插槽。它被用于
	//记下交换文件中每一页插槽上的swap请求
	unsigned char * swap_map;	
	unsigned char * swap_lockmap;
	int pages;
	//域lowest_bit ，highest_bit表明在交换文件中空闲范围的边界，这是为了快速寻址
	int lowest_bit;
	int highest_bit;
	//表示了设备的最大页面号，也就是设备或者文件的物理大小
	unsigned long max;	
} swap_info[MAX_SWAPFILES];	//Linux内核允许多个交换设备，所以在内核中就定义了一个数组来列出各个交换设备

extern int shm_swap (int);

unsigned long *swap_cache;

#ifdef SWAP_CACHE_INFO
unsigned long swap_cache_add_total = 0;
unsigned long swap_cache_add_success = 0;
unsigned long swap_cache_del_total = 0;
unsigned long swap_cache_del_success = 0;
unsigned long swap_cache_find_total = 0;
unsigned long swap_cache_find_success = 0;

extern inline void show_swap_cache_info(void)
{
	printk("Swap cache: add %ld/%ld, delete %ld/%ld, find %ld/%ld\n",
		swap_cache_add_total, swap_cache_add_success, 
		swap_cache_del_total, swap_cache_del_success,
		swap_cache_find_total, swap_cache_find_success);
}
#endif

//将entery加入到交换缓冲中去
static int add_to_swap_cache(unsigned long addr, unsigned long entry)
{
	//p指向entry所指向的交换设备/文件的swap_info_struct结构
	struct swap_info_struct * p = &swap_info[SWP_TYPE(entry)];

#ifdef SWAP_CACHE_INFO
	swap_cache_add_total++;
#endif
	//如果该交换空间准备就绪
	if ((p->flags & SWP_WRITEOK) == SWP_WRITEOK) {
		//xchg_ptr函数交换两个指针的值，并返回原第一个指针的值
		entry = (unsigned long) xchg_ptr(swap_cache + MAP_NR(addr), (void *) entry);
		//如果原交换缓冲数组指针项不为空，则说明替换了非空项
		if (entry)  {
			printk("swap_cache: replacing non-NULL entry\n");
		}
#ifdef SWAP_CACHE_INFO
		swap_cache_add_success++;
#endif
		return 1;
	}
	return 0;
}

//初始化交换高速缓冲数组，隐式的在物理内存始端分得一块内存作为swap_cache数组来管理交换缓冲
static unsigned long init_swap_cache(unsigned long mem_start,
	unsigned long mem_end)
{
	unsigned long swap_cache_size;

	//为了更好地利用硬件高速缓冲
	mem_start = (mem_start + 15) & ~15;
	//swap_cache指向内存始端
	swap_cache = (unsigned long *) mem_start;
	//求得交换高速缓冲数组长度，即要管理的物理页面总数
	swap_cache_size = MAP_NR(mem_end);
	//将数组内容清零
	memset(swap_cache, 0, swap_cache_size * sizeof (unsigned long));
	//返回数组末端作为新的物理内存始端，即改变了mem_start
	return (unsigned long) (swap_cache + swap_cache_size);
}

void rw_swap_page(int rw, unsigned long entry, char * buf)
{
	unsigned long type, offset;
	struct swap_info_struct * p;

	//取得entry所指向的交换设备序号
	type = SWP_TYPE(entry);
	//如果此序号大于系统所允许的最大值
	if (type >= nr_swapfiles) {
		printk("Internal error: bad swap-device\n");
		return;
	}
	//p指向entry所指向的交换设备的swap_info_struct结构
	p = &swap_info[type];
	//取得entry所指向的交换设备中的对应的页面号
	offset = SWP_OFFSET(entry);
	//如果此页面号大于此交换设备中最大的页面号
	if (offset >= p->max) {
		printk("rw_swap_page: weirdness\n");
		return;
	}
	//如果此交换设备的swap_map不为空，但是此交换分配位图中对应的offset页面位为0
	//表示此页面尚未被分配使用，说明系统出了问题
	if (p->swap_map && !p->swap_map[offset]) {
		printk("Hmm.. Trying to use unallocated swap (%08lx)\n", entry);
		return;
	}
	//如果此交换设备没有处于正使用的状态
	if (!(p->flags & SWP_USED)) {
		printk("Trying to swap to unused swap-device\n");
		return;
	}
	//尝试锁住此交换页面
	//setbit()尝试将交换设备的swap_lockmap位图的offset位置位，并返回原来的值
	//如果原来就是1（即被其他进程锁住了），那就睡眠，直到位图锁被其他进程释放（置零）
	//然后唤醒本进程，本进程将其置位并返回原值0，则继续向下执行
	while (set_bit(offset,p->swap_lockmap))
		sleep_on(&lock_queue);
	//如果是读交换页面
	if (rw == READ)
		//增加内核读交换页面的次数
		kstat.pswpin++;
	//否则，是写交换页面
	else
		//增加内核写交换页面的次数
		kstat.pswpout++;
	//如果交换空间是设备文件
	if (p->swap_device) {
		//读写此设备文件
		ll_rw_page(rw,p->swap_device,offset,buf);
	//否则，如果交换页在交换文件中
	} else if (p->swap_file) {
		//取得此交换文件的i节点
		struct inode *swapf = p->swap_file;
		unsigned int zones[8];
		int i;
		if (swapf->i_op->bmap == NULL
			&& swapf->i_op->smap != NULL){
			/*
				With MsDOS, we use msdos_smap which return
				a sector number (not a cluster or block number).
				It is a patch to enable the UMSDOS project.
				Other people are working on better solution.

				It sounds like ll_rw_swap_file defined
				it operation size (sector size) based on
				PAGE_SIZE and the number of block to read.
				So using bmap or smap should work even if
				smap will require more blocks.
			*/
			int j;
			unsigned int block = offset << 3;

			for (i=0, j=0; j< PAGE_SIZE ; i++, j += 512){
				if (!(zones[i] = swapf->i_op->smap(swapf,block++))) {
					printk("rw_swap_page: bad swap file\n");
					return;
				}
			}
		}else{
			int j;
			//因为offset是交换页面号，是物理内存页面（4k）交换到交换文件中去之后
			//在文件中的页面号，其大小还是4k
			//但是，对于一个文件，其所在的磁盘设备的块大小却不一定是4k大小
			//这里是要将交换页面号转换为具体文件所在磁盘设备上的文件块号
			//例如，交换页面号2，就是交换文件4k-8k的内容，如果此交换文件所在磁盘设备
			//的块大小为1k，那么此交换页面号对应的文件块号就是4、5、6、7号
			unsigned int block = offset << (12 - swapf->i_sb->s_blocksize_bits);

			for (i=0, j=0; j< PAGE_SIZE ; i++, j +=swapf->i_sb->s_blocksize)
				if (!(zones[i] = bmap(swapf,block++))) {
					printk("rw_swap_page: bad swap file\n");
					return;
				}
		}
		ll_rw_swap_file(rw,swapf->i_dev, zones, i,buf);
	} else
		printk("re_swap_page: no swap file or device\n");
	//解锁此交换页面
	if (offset && !clear_bit(offset,p->swap_lockmap))
		printk("rw_swap_page: lock already cleared\n");
	//唤醒等待此交换页面解锁的进程
	wake_up(&lock_queue);
}

//内存中的一页被换出时，调用get_swap_page()会得到一个记录换出位置的索引，然后在页表项中回填（1--31位）此索引。
//这是为了在发生缺页异常时进行处理（do_no_page)。索引的高7位给定交换文件，后24位给定设备中的页插槽号。
unsigned int get_swap_page(void)
{
	struct swap_info_struct * p;
	unsigned int offset, type;

	p = swap_info;
	//遍历swap_info数组，试图找到一个可以使用的交换设备/文件
	for (type = 0 ; type < nr_swapfiles ; type++,p++) {
		//如果当前交换设备/文件没有处于准备就绪状态，则继续遍历下一项
		if ((p->flags & SWP_WRITEOK) != SWP_WRITEOK)
			continue;
		//执行到这里，说明当前交换设备/文件处于就绪状态，则需找其中可以使用的一个交换页面
		
		//遍历交换页面位图
		for (offset = p->lowest_bit; offset <= p->highest_bit ; offset++) {
			//如果当前的交换页面位图被置位（被其他进程使用），则continue
			if (p->swap_map[offset])
				continue;
			//如果当前交换页面处于锁定状态
			if (test_bit(offset, p->swap_lockmap))
				continue;
			//将此交换页面位图置位
			p->swap_map[offset] = 1;
			//减少系统中的可用交换页面数
			nr_swap_pages--;
			if (offset == p->highest_bit)
				p->highest_bit--;
			p->lowest_bit = offset;
			//返回此交换页面的entry
			return SWP_ENTRY(type,offset);
		}
	}
	return 0;
}
//函数swap_duplicate()被copy_page_tables()调用来实现子进程在fork()时
//继承被换出的页面，这里要增加域swap_map中此页面的count值，
//任何进程访问此页面时，会换入它的独立的拷贝。
void swap_duplicate(unsigned long entry)	//duplicate:复制;复印;重复
{
	struct swap_info_struct * p;
	unsigned long offset, type;

	if (!entry)
		return;
	offset = SWP_OFFSET(entry);
	type = SWP_TYPE(entry);
	if (type == SHM_SWP_TYPE)
		return;
	if (type >= nr_swapfiles) {
		printk("Trying to duplicate nonexistent swap-page\n");
		return;
	}
	p = type + swap_info;
	if (offset >= p->max) {
		printk("swap_duplicate: weirdness\n");
		return;
	}
	if (!p->swap_map[offset]) {
		printk("swap_duplicate: trying to duplicate unused page\n");
		return;
	}
	p->swap_map[offset]++;
	return;
}
//swap_free()减少swap_info_struct结构类型swap_info中域swap_map中的count值，
//如果count减到0时，则这页面又可再次分配（get_swap_page)，
//在把一个换出页面调入(swap_in)内存时或放弃一个页面时（free_one_table) 调用swap_free()。
void swap_free(unsigned long entry)
{
	struct swap_info_struct * p;
	unsigned long offset, type;

	if (!entry)
		return;
	type = SWP_TYPE(entry);
	if (type == SHM_SWP_TYPE)
		return;
	if (type >= nr_swapfiles) {
		printk("Trying to free nonexistent swap-page\n");
		return;
	}
	p = & swap_info[type];
	offset = SWP_OFFSET(entry);
	if (offset >= p->max) {
		printk("swap_free: weirdness\n");
		return;
	}
	if (!(p->flags & SWP_USED)) {
		printk("Trying to free swap from unused swap-device\n");
		return;
	}
	if (offset < p->lowest_bit)
		p->lowest_bit = offset;
	if (offset > p->highest_bit)
		p->highest_bit = offset;
	if (!p->swap_map[offset])
		printk("swap_free: swap-space map bad (entry %08lx)\n",entry);
	else
		if (!--p->swap_map[offset])
			nr_swap_pages++;
}

/*
 * The tests may look silly, but it essentially makes sure that
 * no other process did a swap-in on us just as we were waiting.
 *
 * Also, don't bother to add to the swap cache if this page-in
 * was due to a write access.
 */
void swap_in(struct vm_area_struct * vma, pte_t * page_table,
	unsigned long entry, int write_access)
{
	//申请一页空闲物理内存
	unsigned long page = get_free_page(GFP_KERNEL);
	//在get_free_page期间，进程可能睡眠，在睡眠期间可能由其它一些原因已经将页面交换回来了
	if (pte_val(*page_table) != entry) {
		free_page(page);
		return;
	}
	//如果申请物理内存失败
	if (!page) {
		*page_table = BAD_PAGE;
		//为什么要释放交换页面？？？
		swap_free(entry);
		oom(current);
		return;
	}
	//将交换页面中的内容读入page物理页面中
	read_swap_page(entry, (char *) page);
	//read_swap_page同样可能导致睡眠...
	if (pte_val(*page_table) != entry) {
		free_page(page);
		return;
	}
	vma->vm_task->mm->rss++;
	vma->vm_task->mm->maj_flt++;
	//如果page_in操作不是由写操作引起的，则说明此操作暂时不会修改物理页面内容，
	//之前保存在交换文件中的页面内容将和物理页面中的内容保持一致
	//则将指向交换文件页面的指针保存在交换缓冲中，这样的话，在之后如果要将此页
	//面交换到交换文件中去，则只需要简单地废弃该页面表项，搜索
	//交换缓冲，将之前保存的指向交换文件中的对应页面指针替换掉也表指针就可以了，
	//从而避免了将物理页面实际写到磁盘交换文件中去
	//但如果在之后的操作中修改了物理页面，则调用delete_from_swap_cache函数将交换缓冲中的表项删除
	//参见最上方的"交换高速缓冲"注释介绍
	//add_to_swap_cache(page, entry):page在函数内部作为swap_cache数组下标 entry为要保存在数组项中
	//的值，是一个指向swap磁盘页面的指针
	if (!write_access && add_to_swap_cache(page, entry)) {
		//将pte指向此swap_in进来的物理内存页面，此时在交换高速缓冲
		//swap_cache数组中保存了此物理内存页面对应的磁盘上的swap页面
		//而且磁盘页面和内存页面中的内容是一致的 以后当要想把此内存页
		//面swap_out出去的时候，先查看交换高速缓冲中是否存在对应的项
		//若存在，则说明磁盘交换文件中存在此页面且磁盘页面和内存页面
		//中的内容是一致的 只需将swap_cache中对应项的swap_entry替换 pte即可
		//但如果在之后的操作中修改了物理页面，则调用delete_from_swap_cache函数将交换缓冲中的表项删除
		/*这里应该是将页面的属性设置为了不可写，当对此页面进行写操作时，会将其设置为可写脏状态*/
		*page_table = mk_pte(page, vma->vm_page_prot);
		//注意，这里直接返回了，并没有swap_free(entry);
		return;
	}
	*page_table = pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	//释放交换文件中的页面 因为执行到这里，说明swao_in是由写操作引起的或者没有成功将交换页面加入缓冲中
	//那么，就把交换页面释放吧，它没有存在的必要了
  	swap_free(entry);
  	return;
}

/*
 * The swap-out functions return 1 of(if?) they successfully
 * threw something out, and we got a free page. It returns
 * zero if it couldn't do anything, and any other value
 * indicates it decreased rss, but the page was shared.
 *
 * NOTE! If it sleeps, it *must* return 1 to make sure we
 * don't continue with the swap-out. Otherwise we may be
 * using a process that no longer actually exists (it might
 * have died while we slept).
 */
 //当本函数返回0时，其上一层程序就会跳过这个页面，而尝试着换出同一个页面表中映射的下一个页面
 //如果一个页面表已经穷尽，就再往上退一层尝试下一个页面表
static inline int try_to_swap_out(struct vm_area_struct* vma, unsigned long address, pte_t * page_table)
{
	pte_t pte;
	unsigned long entry;
	unsigned long page;

	pte = *page_table;
	//如果对应的物理内存页面不在内存中，则不能交换出去（都没有，拿什么交换出去呢？）
	if (!pte_present(pte))
		return 0;
	page = pte_page(pte);
	//如果page大于内存高端，直接返回
	if (page >= high_memory)
		return 0;
	//内核空间页面常驻内存，不能交换出去
	if (mem_map[MAP_NR(page)] & MAP_PAGE_RESERVED)
		return 0;
	//如果页面是脏的，则尝试从交换高速缓冲中删除该项（因为交换页面中的内容和盘上内容已经不一致了）
	//若删除成功或者页面是年轻的（最近被访问过），则将此页面设置为未访问过的
	//那么下次就可能将其交换出去（体现了LRU思想）
	//若删除失败...
	if ((pte_dirty(pte) && delete_from_swap_cache(page)) || pte_young(pte))  {
		//将页面属性设置为old，表明此次不将其交换出去，但下一次就可能将其交换出去
		*page_table = pte_mkold(pte);
		//返回0，注意，这里并没有成功的交换出此页面
		return 0;
	}
	//程序执行到此处，说明如果页面是脏的，页面没有在交换缓冲区中，则将页面换出到交换区中去
	//为的是保持盘上内容和物理内存页面内容的一致性
	if (pte_dirty(pte)) {
		//如果page被共享着，返回0 说明共享页面不能被swap_out
		//但是一个交换页面是可以共享的，参见swap_duplicate()
		if (mem_map[MAP_NR(page)] != 1)
			return 0;
		//若对应的虚拟区对应着磁盘文件等，则调用其函数将页面换出去
		if (vma->vm_ops && vma->vm_ops->swapout) {
			vma->vm_task->mm->rss--;	//将进程内容驻留在物理内存的页面总数减一
			vma->vm_ops->swapout(vma, address-vma->vm_start, page_table);
		} else {
			//否则在交换文件中获取一页页面，将物理内存内容写入，并修改页面表项指针，使其指向交换文件中对应的页面
			if (!(entry = get_swap_page()))
				return 0;
			vma->vm_task->mm->rss--;
			pte_val(*page_table) = entry;
			invalidate();
			write_swap_page(entry, (char *) page);
		}
		free_page(page);	//释放物理内存页面（swap_out的真正目的），说明已经将其换到交换文件中去了
		return 1;	/* we slept: the process may not exist any more */
	}
	//执行到这里，说明页面是"干净的"
	//对于干净的页面，只需简单的丢弃就可以了，因为其是从可执行文件中加载的，当需要时，只需
	//让缺页中断程序处理就行了。只有脏页面才需要交换到交换设备中去
	if ((entry = find_in_swap_cache(page)))  {
		//如果在交换高速缓冲中找到了此页面，并且此页面被共享了，则将此页面设为脏的
		//这里和memory.c中的try_to_shart()函数中的do we copy?中的代码产生联系
		if (mem_map[MAP_NR(page)] != 1) {
			*page_table = pte_mkdirty(pte);
			printk("Aiee.. duplicated cached swap-cache entry\n");
			return 0;
		}
		vma->vm_task->mm->rss--;
		pte_val(*page_table) = entry;
		invalidate();
		free_page(page);
		return 1;
	} 
	vma->vm_task->mm->rss--;
	pte_clear(page_table);
	invalidate();
	entry = mem_map[MAP_NR(page)];
	free_page(page);
	return entry;
}

/*
 * A new implementation of swap_out().  We do not swap complete processes,
 * but only a small number of blocks, before we continue with the next
 * process.  The number of blocks actually swapped is determined on the
 * number of page faults, that this process actually had in the last time,
 * so we won't swap heavily used processes all the time ...
 *
 * Note: the priority argument is a hint on much CPU to waste with the
 *       swap block search, not a hint, of how much blocks to swap with
 *       each process.
 *
 * (C) 1993 Kai Petzke, wpp@marie.physik.tu-berlin.de
 */

/*
 * These are the minimum and maximum number of pages to swap from one process,
 * before proceeding to the next:
 */
#define SWAP_MIN	4
#define SWAP_MAX	32

/*
 * The actual number of pages to swap is determined as:
 * SWAP_RATIO / (number of recent major page faults)
 */
#define SWAP_RATIO	128

static inline int swap_out_pmd(struct vm_area_struct * vma, pmd_t *dir,
	unsigned long address, unsigned long end)
{
	pte_t * pte;
	unsigned long pmd_end;

	if (pmd_none(*dir))
		return 0;
	if (pmd_bad(*dir)) {
		printk("swap_out_pmd: bad pmd (%08lx)\n", pmd_val(*dir));
		pmd_clear(dir);
		return 0;
	}
	
	pte = pte_offset(dir, address);
	
	pmd_end = (address + PMD_SIZE) & PMD_MASK;
	if (end > pmd_end)
		end = pmd_end;

	do {
		int result;
		vma->vm_task->mm->swap_address = address + PAGE_SIZE;
		result = try_to_swap_out(vma, address, pte);
		if (result)
			return result;
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
	return 0;
}

static inline int swap_out_pgd(struct vm_area_struct * vma, pgd_t *dir,
	unsigned long address, unsigned long end)
{
	pmd_t * pmd;
	unsigned long pgd_end;

	if (pgd_none(*dir))
		return 0;
	if (pgd_bad(*dir)) {
		printk("swap_out_pgd: bad pgd (%08lx)\n", pgd_val(*dir));
		pgd_clear(dir);
		return 0;
	}

	pmd = pmd_offset(dir, address);

	pgd_end = (address + PGDIR_SIZE) & PGDIR_MASK;	
	if (end > pgd_end)
		end = pgd_end;
	
	do {
		int result = swap_out_pmd(vma, pmd, address, end);
		if (result)
			return result;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

static int swap_out_vma(struct vm_area_struct * vma, pgd_t *pgdir,
	unsigned long start)
{
	unsigned long end;

	end = vma->vm_end;
	while (start < end) {
		int result = swap_out_pgd(vma, pgdir, start, end);
		if (result)
			return result;
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	}
	return 0;
}

static int swap_out_process(struct task_struct * p)
{
	unsigned long address;
	struct vm_area_struct* vma;

	/*
	 * Go through process' page directory.
	 */
	address = p->mm->swap_address;
	p->mm->swap_address = 0;

	/*
	 * Find the proper vm-area
	 */
	vma = find_vma(p, address);
	if (!vma)
		return 0;
	if (address < vma->vm_start)
		address = vma->vm_start;

	for (;;) {
		int result = swap_out_vma(vma, pgd_offset(p, address), address);
		if (result)
			return result;
		vma = vma->vm_next;
		if (!vma)
			break;
		address = vma->vm_start;
	}
	p->mm->swap_address = 0;
	return 0;
}

//参见《Linux内核源代码情景分析》第二章第八节的内容 很经典
//这个函数最然叫'swap_out'，但实际上只是为把一些页面交换到交换设备上做好准备
//并不一定是物理意义上的页面换出（因为最终函数要调用到try_to_swap_out()函数）
static int swap_out(unsigned int priority)
{
	static int swap_task;
	int loop, counter;
	struct task_struct *p;

/*
	这个函数的主体是一个for循环，循环的次数取决于counter。这个数值决定了
	吧页面换出去的决心有多大--即外层for循环的次数。
*/
	counter = 2*NR_TASKS >> priority;
	for(; counter >= 0; counter--) {
		/*
		 * Check that swap_task is suitable for swapping.  If not, look for
		 * the next suitable process.
		 */
		loop = 0;
		//此while循环试图找到一个合适的、可以做swap操作的进程
		while(1) {
			if (swap_task >= NR_TASKS) {
				swap_task = 1;
				//如果loop为真了，表明扫描了所有的进程，并且所有的进程都是不可交换的或者已经被交换了出去
				if (loop)
					/* all processes are unswappable or already swapped out */
					return 0;
				//将loop置为1，表明下次再进入此if语句，就是扫描完了所有的进程
				loop = 1;
			}

			p = task[swap_task];
			//如果当前被遍历的进程存在并且其内存是可以交换的，并且存在物理内存页面
			if (p && p->mm->swappable && p->mm->rss)
				break;
			//执行到这里，说明p进程不满足交换的条件 则为遍历下一个进程做准备
			swap_task++;
		}

		/*
		 * Determine the number of pages to swap from this process.
		 */
		 //执行到这里，说明找到了一个可以交换的进程p

/*
		mm->swap_cnt意义：每次考察和处理了进程的一个页面，就将mm->swap_cnt减一，直至最后变为0
		所以，mm->rss反映了一个进程占用的内存页面数量，而mm->swap_cnt则反映了进程在一轮换出内
		存页面的努力中尚未收到考察的页面的数量（这里之所以说是考察，是因为此函数并不一定是物理
		意义上的页面换出）。
		dec_flt		 page fault count of the last time （这里的last time其实是相对于上一次swap_out时）
		swap_cnt		 number of pages to swap on next pass 
		old_maj_flt	 old value of maj_flt 
		注意：mm->swap_cnt mm->dec_flt p->mm->old_maj_flt在整个内核代码中，只在这里进行了引用设置
		即，他们都只为swap_out服务
*/
 
		//如果mm->swap_cnt为0 说明此进程的所有该考察的页面都已经被考察过了 所以需要重新设置一些值
		if (!p->mm->swap_cnt) {
			//p->mm->maj_flt - p->mm->old_maj_flt的值的意义是此进程自上次swap_out到此次swap_out之间
			//所产生的缺页次数 而四分之三的意义在于，将p->mm->dec_flt的值设置为原值的四分之三加上上一次
			//缺页次数
			p->mm->dec_flt = (p->mm->dec_flt * 3) / 4 + p->mm->maj_flt - p->mm->old_maj_flt;
			p->mm->old_maj_flt = p->mm->maj_flt;
/*
			SWAP_RATIO / SWAP_MIN和SWAP_RATIO / SWAP_MIN是两个界值
			SWAP_RATIO 128
			SWAP_MIN   4
			SWAP_RATIO 32
			细细品味上面三个值，就可以体会到一些深刻的东西
*/
			if (p->mm->dec_flt >= SWAP_RATIO / SWAP_MIN) {
				p->mm->dec_flt = SWAP_RATIO / SWAP_MIN;
				p->mm->swap_cnt = SWAP_MIN;
			} else if (p->mm->dec_flt <= SWAP_RATIO / SWAP_MAX)
				p->mm->swap_cnt = SWAP_MAX;
			else
				//从这里可以看出，进程缺页次数越多，要交换的页面就越少
				p->mm->swap_cnt = SWAP_RATIO / p->mm->dec_flt;
		}
		//如果考察了此进程的一个页面后，mm->swap_cnt的值为0了，则为遍历下一个进程做准备
		if (!--p->mm->swap_cnt)
			swap_task++;
		//试图swap_out进程p的一页物理内存
		switch (swap_out_process(p)) {
			case 0:
				//如果返回0，表明没有交换出任何一页页面
				if (p->mm->swap_cnt)
					//这里要为遍历下一个进程做准备，这里的意义是，此进程的rss都被交换出去了
					swap_task++;
				break;
			case 1:
				//如果返回1，表明成功的swap_out了一页物理内存，得到了一页空闲的物理页面
				return 1;
			default:
				break;
		}
	}
	return 0;
}

/*
 * we keep on shrinking one resource until it's considered "too hard",
 * and then switch to the next one (priority being an indication on how
 * hard we should try with the resource).
 *
 * This should automatically find the resource that can most easily be
 * free'd, so hopefully we'll get reasonable behaviour even under very
 * different circumstances.
 */
static int try_to_free_page(int priority)
{
	static int state = 0;
	int i=6;

	switch (state) {
		do {
		case 0:
			if (priority != GFP_NOBUFFER && shrink_buffers(i))
				return 1;
			state = 1;
		case 1:
			if (shm_swap(i))
				return 1;
			state = 2;
		default:
			if (swap_out(i))
				return 1;
			state = 0;
		} while(--i);
	}
	return 0;
}

static inline void add_mem_queue(struct mem_list * head, struct mem_list * entry)
{
	entry->prev = head;
	(entry->next = head->next)->prev = entry;
	head->next = entry;
}

static inline void remove_mem_queue(struct mem_list * head, struct mem_list * entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

/*
 * Free_page() adds the page to the free lists. This is optimized for
 * fast normal cases (no error jumps taken normally).
 *
 * The way to optimize jumps for gcc-2.2.2 is to:
 *  - select the "normal" case and put it inside the if () { XXX }
 *  - no else-statements if you can avoid them
 *
 * With the above two rules, you get a straight-line execution path
 * for the normal case, giving better asm-code.
 *
 * free_page() may sleep since the page being freed may be a buffer
 * page or present in the swap cache. It will not sleep, however,
 * for a freshly allocated page (get_free_page()).
 */

/*
 * Buddy system. Hairy. You really aren't expected to understand this
 */
 //伙伴系统
static inline void free_pages_ok(unsigned long addr, unsigned long order)
{
	unsigned long index = MAP_NR(addr) >> (1 + order);	//找到要释放的内存块在相应空闲链表中的索引号
	unsigned long mask = PAGE_MASK << order;	//对应空闲链表中页面块大小屏蔽码

	addr &= mask;	//地址对齐
	nr_free_pages += 1 << order;	//增加空闲页面数
	//遍历空闲页面链表数组
	while (order < NR_MEM_LISTS-1) {
		//change_bit()函数返回位图原值，加上！的意思就是改变之后的值
		//即，若change_bit后，值为1，则说明将此页面块归还之后，相应“伙伴”中，只有此页面块空闲，也即归还之前两个“伙伴”
		//都分配了出去。如果是这样的情况，则break，将此页面块加入到此空闲链表中
		//若change_bit后，值为0，则说明之前的值为1，即归还之前只有“伙伴”中的一个分配了出去，归还之后，两个“伙伴”
		//都空闲了，此时，继续遍历下一个空闲链表，看能不能将此“伙伴”合并进去
		if (!change_bit(index, free_area_map[order]))
			break;
		//值为0，说明两个“伙伴”都归还了，将此“伙伴”从此链表中移除
		//从这也可以看出来，只有将两个“伙伴”都归还之后，才从链表移除
		//只要有“伙伴”之一空闲，链表项就存在？
		remove_mem_queue(free_area_list+order, (struct mem_list *) (addr ^ (1+~mask)));	//1+~mask==-mask
		//遍历下一个链表，看能不能将其合并进去，即相当于释放此“伙伴”，释放过程得以循环
		order++;
		index >>= 1;
		mask <<= 1;
		addr &= mask;
	}
	//“合并”
	add_mem_queue(free_area_list+order, (struct mem_list *) addr);
}

static inline void check_free_buffers(unsigned long addr)
{
	struct buffer_head * bh;

	bh = buffer_pages[MAP_NR(addr)];
	if (bh) {
		struct buffer_head *tmp = bh;
		do {
			if (tmp->b_list == BUF_SHARED && tmp->b_dev != 0xffff)
				refile_buffer(tmp);
			tmp = tmp->b_this_page;
		} while (tmp != bh);
	}
}

void free_pages(unsigned long addr, unsigned long order)
{
	if (addr < high_memory) {
		unsigned long flag;
		mem_map_t * map = mem_map + MAP_NR(addr);
		if (*map) {
			if (!(*map & MAP_PAGE_RESERVED)) {
				save_flags(flag);
				cli();
				if (!--*map)  {
					free_pages_ok(addr, order);
					delete_from_swap_cache(addr);
				}
				restore_flags(flag);
				if (*map == 1)
					check_free_buffers(addr);
			}
			return;
		}
		printk("Trying to free free memory (%08lx): memory probably corrupted\n",addr);
		printk("PC = %p\n", __builtin_return_address(0));
		return;
	}
}

/*
 * Some ugly macros to speed up __get_free_pages()..
 */
#define RMQUEUE(order) \
do { struct mem_list * queue = free_area_list+order; \	//取得相应链表头指针
     unsigned long new_order = order; \					//记录分配得到的链表的oeder
	do { struct mem_list *next = queue->next; \			//指向链表头指针下一项
		if (queue != next) { \							//若链表不为空（表示有空闲页面）
			(queue->next = next->next)->prev = queue; \	//将此“伙伴”项从链表中移除
			mark_used((unsigned long) next, new_order); \	//change_bit
			nr_free_pages -= 1 << order; \	//减少空闲页面数
			restore_flags(flags); \			
			EXPAND(next, order, new_order); \	//
			return (unsigned long) next; \
		} new_order++; queue++; \	//此链表为空，则遍历下一链表
	} while (new_order < NR_MEM_LISTS); \
} while (0)

static inline int mark_used(unsigned long addr, unsigned long order)
{
	return change_bit(MAP_NR(addr) >> (1+order), free_area_map[order]);
}

#define EXPAND(addr,low,high) \
do { unsigned long size = PAGE_SIZE << high; \	//得到相应的页面大小
	while (high > low) { \					//若high==low，说明分配的页面数也是请求的页面数，则不必循环，
		high--; size >>= 1; cli(); \	//为下次的循环做准备
		add_mem_queue(free_area_list+high, addr); \	//将大“伙伴”中分配剩下的一半加入到次级链表中（high已经减一了）
		mark_used((unsigned long) addr, high); \	//change_bit
		restore_flags(flags); \
		addr = (struct mem_list *) (size + (unsigned long) addr); \	//将一半的一半加入到次次级链表中，如此循环
	} mem_map[MAP_NR((unsigned long) addr)] = 1; \
} while (0)

unsigned long __get_free_pages(int priority, unsigned long order)
{
	unsigned long flags;
	int reserved_pages;

	if (intr_count && priority != GFP_ATOMIC) {
		static int count = 0;
		if (++count < 5) {
			printk("gfp called nonatomically from interrupt %p\n",
				__builtin_return_address(0));
			priority = GFP_ATOMIC;
		}
	}
	reserved_pages = 5;
	if (priority != GFP_NFS)
		reserved_pages = min_free_pages;
	save_flags(flags);
repeat:
	cli();
	if ((priority==GFP_ATOMIC) || nr_free_pages > reserved_pages) {
		RMQUEUE(order);	//从伙伴系统分配页面
		restore_flags(flags);
		return 0;
	}
	restore_flags(flags);
	if (priority != GFP_BUFFER && try_to_free_page(priority))
		goto repeat;
	return 0;
}

/*
 * Yes, I know this is ugly. Don't tell me.
 */
unsigned long __get_dma_pages(int priority, unsigned long order)
{
	unsigned long list = 0;
	unsigned long result;
	unsigned long limit = MAX_DMA_ADDRESS;

	/* if (EISA_bus) limit = ~0UL; */
	if (priority != GFP_ATOMIC)
		priority = GFP_BUFFER;
	for (;;) {
		result = __get_free_pages(priority, order);
		if (result < limit) /* covers failure as well */
			break;
		*(unsigned long *) result = list;
		list = result;
	}
	while (list) {
		unsigned long tmp = list;
		list = *(unsigned long *) list;
		free_pages(tmp, order);
	}
	return result;
}

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 */
void show_free_areas(void)
{
 	unsigned long order, flags;
 	unsigned long total = 0;

	printk("Free pages:      %6dkB\n ( ",nr_free_pages<<(PAGE_SHIFT-10));
	save_flags(flags);
	cli();
 	for (order=0 ; order < NR_MEM_LISTS; order++) {
		struct mem_list * tmp;
		unsigned long nr = 0;
		for (tmp = free_area_list[order].next ; tmp != free_area_list + order ; tmp = tmp->next) {
			nr ++;
		}
		total += nr * ((PAGE_SIZE>>10) << order);
		printk("%lu*%lukB ", nr, (PAGE_SIZE>>10) << order);
	}
	restore_flags(flags);
	printk("= %lukB)\n", total);
#ifdef SWAP_CACHE_INFO
	show_swap_cache_info();
#endif	
}

/*
 * Trying to stop swapping from a file is fraught with races, so
 * we repeat quite a bit here when we have to pause. swapoff()
 * isn't exactly timing-critical, so who cares (but this is /really/
 * inefficient, ugh).
 *
 * We return 1 after having slept, which makes the process start over
 * from the beginning for this process..
 */
 
 /*
 *试图停用一个交换设备文件的过程充满了竞争条件，所以当我们不得不睡眠的时候就要要做大量的重复性工作
 *swapoff()函数对时间要求不是很苛刻，所以谁会关心swapoff()要花费很长的时间呢？但这效率是非常低的。
 *
 *只要我们睡眠了（即从磁盘读入了交换页面），就必须从头（从进程虚拟空间地址开始处）开始处理了
 *所以效率是非常低的
 */
 
 //通过研究unuse_XXX函数中的do_while循环可深刻的体会到这一点 效率太低了！！！
 //unuse_XXX函数簇负责实现将一个进程中所有和指定交换设备/文件相关的交换页面
 //读入物理内存并废弃交换页面。
 
//注意：这个page参数用的稍妙，在try_to_unuse函数中每次循环中申请了一页内存，
//层层传递，传递到了此函数中，但传递的是形参，副本，在函数中并不能改变原page
//变量的值，所以在本函数中，当判断页面在内存中时，将page值指向此页面，并不会改变
//try_to_unuse函数中page的值，而且当本函数中判断页面不在内存中后，page依然指向
//try_to_unuse函数中为其分配的物理页面
static inline int unuse_pte(struct vm_area_struct * vma, unsigned long address,
	pte_t *dir, unsigned int type, unsigned long page)
{
	pte_t pte = *dir;

	//如果页面表项为空，即页面即不再物理内存中，也不在交换设备中
	if (pte_none(pte))
		return 0;
	//如果页面在物理内存中
	if (pte_present(pte)) {
		unsigned long page = pte_page(pte);
		if (page >= high_memory)
			return 0;
		//如果不在交换高速缓冲中
		if (!in_swap_cache(page))
			return 0;
		//页面在交换高速缓冲中，但此交换缓冲页面不属于指定的（要关闭的）交换设备
		if (SWP_TYPE(in_swap_cache(page)) != type)
			return 0;
		//页面在交换高速缓冲中，并且交换缓冲页面属于指定的（要关闭的）交换设备，则删除交换缓冲中的页面
		delete_from_swap_cache(page);
		//把页面表项设为脏的why？？？，并映射
		//这里将页表项置为脏状态，是为了之后将页面写入磁盘
		//以保持一致性。但是，我觉得这里这样做或许没有必要
		//因为既然交换页面存在于交换高速缓冲中，那么就说明
		//进程尚未对此页面进行写操作，此时交换页面中的内容
		//和磁盘内容是一致的，只需要简单的丢弃交换页面就可以
		//了。之所以可以肯定进程还尚未对此页面进行写操作
		//是因为在此物理页面有了对应的交换页面并且将交换页面
		//存于交换高速缓冲中时，交换页面和磁盘页面的内容是一致
		//的，此后，只要进程对此页面进程写操作，就会导致丢弃该
		//交换缓冲，释放此交换页面。参见本文件中的swap_in()函数
		//NOTE:上面分析的不对，进程对页面的写操作并不会立即导致
		//丢弃其交换缓冲，释放其交换页面
		/*
			以上的相关注释都是错误的，在读了《The Linux Kernel》
			内存管理相关的章节后，我明白了，只有脏页面是可以交换
			到交换设备中去的，而对于干净的页面，其本身就来源和
			映射于可执行文件，这样的页面只需要简单的舍弃就可以了
			当再需要时，可由缺页中断程序重新从可执行文件加载。
			而脏的页面交换出去后，如果再读入内存并且其内容和交
			换文件中的内容一致时，就将其加入交换缓冲，并将物理
			页面设置为干净的，注意，这里的干净是相对于其在交换
			缓冲中的“交换页面”而言的，并不是相对于其映射的可执行文件
			而言的，所以这样的“干净的”页面也可以交换出去。
			
			可见，交换机制，实质上是将进程运行中相对于原可执行文件
			改变了的物理页面暂时保存到交换设备/文件中去，以此扩展了
			物理内存
		*/
		//程序执行到这里，相应的页面肯定在物理内存中，并且有对应的
		//交换缓冲页面，这表明，此物理页面相对于可执行文件的页面
		//必定是脏的状态，因为舍弃了交换缓冲，所以必须将页面置为脏
		//之前之所以不为脏，是因为有了交换缓冲，其相对于交换缓冲页面
		//是干净的
		*dir = pte_mkdirty(pte);
		return 0;
	}
	//执行到这里，说明页面不在内存中，而是在交换设备中
	//页面不属于指定的（要关闭的）交换设备,返回0
	if (SWP_TYPE(pte_val(pte)) != type)
		return 0;
	//页面属于指定的（要关闭的）交换设备
	//从交换设备中读出交换出去的页面
	read_swap_page(pte_val(pte), (char *) page);
	//在读操作中，进程可能睡眠...正是因为这里可能导致睡眠
	//从而导致系统可能在进程睡眠期间将进程的某些页面交换到
	//该交换设备/文件中去，而这些页面可能正是这里之前已经经过
	//考察处理的页面，所以只要swap_in了页面，就要返回1，导致
	//上层函数也不断向上层函数返回1，最终导致重新从头开始扫描
	//处理进程的页表。如此导致非常低的效率
	//上面我分析的有问题，内核在调用此类函数之前已经将指定
	//类型的交换设备标志位占用状态，此状态的交换设备不会被进程所
	//使用，所以避免了一些竞争条件
	//而且，此版本的内核是不允许抢占内核的，所以在内核态执行程序
	//时，即使发生中断，也不会发生进程调度，也不会返回到用户态执行
	//除非内核代表进程睡眠，主动调度别的程序运行
	/*这里所说的讨厌的竞争条件 到底是啥 没想明白*/
	if (pte_val(*dir) != pte_val(pte)) {
		free_page(page);
		return 1;
	}
	//将读入的页面属性处理后，映射回原页面表项
	//页面是从交换缓冲中读取回来的，所以其物理页面的
	//属性必然是脏的（只有脏页面可以交换出去）
	*dir = pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	++vma->vm_task->mm->rss;	//增加相应进程所占物理页面总数
	swap_free(pte_val(pte));	//将交换设备中的页面释放
	return 1;
}

static inline int unuse_pmd(struct vm_area_struct * vma, pmd_t *dir,
	unsigned long address, unsigned long size, unsigned long offset,
	unsigned int type, unsigned long page)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*dir))
		return 0;
	if (pmd_bad(*dir)) {
		printk("unuse_pmd: bad pmd (%08lx)\n", pmd_val(*dir));
		pmd_clear(dir);
		return 0;
	}
	pte = pte_offset(dir, address);
	offset += address & PMD_MASK;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		//只有当unuse_pte一直返回0，直到此页表项页面中所有1024项页表指针全部有了物理映射，才会结束while循环，返回0
		if (unuse_pte(vma, offset+address-vma->vm_start, pte, type, page))
			return 1;
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
	return 0;
}

static inline int unuse_pgd(struct vm_area_struct * vma, pgd_t *dir,
	unsigned long address, unsigned long size,
	unsigned int type, unsigned long page)
{
	pmd_t * pmd;
	unsigned long offset, end;

	if (pgd_none(*dir))
		return 0;
	if (pgd_bad(*dir)) {
		printk("unuse_pgd: bad pgd (%08lx)\n", pgd_val(*dir));
		pgd_clear(dir);
		return 0;
	}
	pmd = pmd_offset(dir, address);
	offset = address & PGDIR_MASK;
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		//只有当unuse_pmd一直返回0，直到此pmd表项页面中所有1024项页表指针全部有了物理映射，才会结束while循环，返回0
		if (unuse_pmd(vma, pmd, address, end - address, offset, type, page))
			return 1;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

static int unuse_vma(struct vm_area_struct * vma, pgd_t *pgdir,
	unsigned long start, unsigned long end,
	unsigned int type, unsigned long page)
{
	while (start < end) {
		//只有当unuse_pgd一直返回0，直到此pgd表项页面中所有1024项页目录指针全部有了物理映射，才会结束while循环，返回0
		if (unuse_pgd(vma, pgdir, start, end - start, type, page))
			return 1;
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	}
	return 0;
}

static int unuse_process(struct task_struct * p, unsigned int type, unsigned long page)
{
	struct vm_area_struct* vma;

	/*
	 * Go through process' page directory.
	 */
	vma = p->mm->mmap;
	while (vma) {
		pgd_t * pgd = pgd_offset(p, vma->vm_start);
		//只有当unuse_vma一直返回0，直到遍历完本进程所有虚拟区间，才返回0
		if (unuse_vma(vma, pgd, vma->vm_start, vma->vm_end, type, page))
			return 1;
		vma = vma->vm_next;
	}
	return 0;
}

/*
 * To avoid races, we repeat for each process after having
 * swapped something in. That gets rid of a few pesky(讨厌的) races,
 * and "swapoff" isn't exactly timing critical.
 */
static int try_to_unuse(unsigned int type)
{
	int nr;
	unsigned long page = get_free_page(GFP_KERNEL);

	if (!page)
		return -ENOMEM;
	nr = 0;
	while (nr < NR_TASKS) {
		if (task[nr]) {	//这也是提高效率的方法？若在遍历过程中，进程终止了...
			//只有当unuse_process调用的unuse_XXX确实swap_in了页面，才会层层返回1
			//只有unuse_process返回0（表示本进程所有虚拟区间已经遍历完毕，所有该
			//swap_in的页面也已swai_in了），才会遍历下一个进程但是在遍历过程中，
			//进程是会睡眠，并且也有进程进程消亡和新的进程创建的问题（races竞争条件？）
			//解决的办法应该是在此之前将交换设备结构信息中的flags=SWP_USED，使得交换设备不可用？
			if (unuse_process(task[nr], type, page)) {
				page = get_free_page(GFP_KERNEL);
				if (!page)
					return -ENOMEM;
				continue;
			}
		}
		nr++;
	}
	free_page(page);
	return 0;
}

asmlinkage int sys_swapoff(const char * specialfile)
{
	struct swap_info_struct * p;
	struct inode * inode;
	unsigned int type;
	struct file filp;
	int i;

	if (!suser())
		return -EPERM;
	//在文件系统中寻找要关闭交换设备（文件）i节点
	i = namei(specialfile,&inode);
	//若没找到 则直接返回
	if (i)
		return i;
	p = swap_info;
	//遍历交换设备结构数组，找到要关闭的交换设备项结构信息
	for (type = 0 ; type < nr_swapfiles ; type++,p++) {
		if ((p->flags & SWP_WRITEOK) != SWP_WRITEOK)
			continue;
		//如果swap_file不为空，则交换设备映射磁盘文件
		if (p->swap_file) {
			//如果映射的磁盘文件i节点和要关闭的交换设备（文件）相等，则表明找到了相应的交换设备项
			if (p->swap_file == inode)
				break;
		} else {	//否则，交换设备项映射的是磁盘设备
			if (!S_ISBLK(inode->i_mode))	//如果不是块设备，则继续寻找下一项
				continue;
			//如果是块设备，并且交换设备所在设备号等于要关闭的交换设备所在的设备号，则表明找到了要找的项
			if (p->swap_device == inode->i_rdev)
				break;
		}
	}
	//没有找到，退出
	if (type >= nr_swapfiles){
		iput(inode);
		return -EINVAL;
	}
	p->flags = SWP_USED;
	i = try_to_unuse(type);	//应该是将swap_out到要关闭的交换设备中的页面swap_in回进程的物理页面
	if (i) {
		iput(inode);
		p->flags = SWP_WRITEOK;
		return i;
	}

	if(p->swap_device){
		memset(&filp, 0, sizeof(filp));		
		filp.f_inode = inode;
		filp.f_mode = 3; /* read write */
		/* open it again to get fops */
		if( !blkdev_open(inode, &filp) &&
		   filp.f_op && filp.f_op->release){
			filp.f_op->release(inode,&filp);
			filp.f_op->release(inode,&filp);
		}
	}
	iput(inode);

	nr_swap_pages -= p->pages;
	iput(p->swap_file);
	p->swap_file = NULL;
	p->swap_device = 0;
	vfree(p->swap_map);
	p->swap_map = NULL;
	free_page((long) p->swap_lockmap);
	p->swap_lockmap = NULL;
	p->flags = 0;
	return 0;
}

/*
 * Written 01/25/92 by Simmule Turner, heavily changed by Linus.
 *
 * The swapon system call
 */
asmlinkage int sys_swapon(const char * specialfile)
{
	struct swap_info_struct * p;
	struct inode * swap_inode;
	unsigned int type;
	int i,j;
	int error;
	struct file filp;

	memset(&filp, 0, sizeof(filp));
	if (!suser())
		return -EPERM;
	p = swap_info;	//取得交换设备数组首指针
	//遍历交换设备结构数组，找到一个未在使用中的交换设备项
	for (type = 0 ; type < nr_swapfiles ; type++,p++)
		if (!(p->flags & SWP_USED))
			break;
	if (type >= MAX_SWAPFILES)
		return -EPERM;
	if (type >= nr_swapfiles)
		nr_swapfiles = type+1;
	p->flags = SWP_USED;	//将此交换设备结构设为已使用															
	p->swap_file = NULL;
	p->swap_device = 0;
	p->swap_map = NULL;
	p->swap_lockmap = NULL;
	p->lowest_bit = 0;
	p->highest_bit = 0;
	p->max = 1;
	//在文件系统中找到指定的交换设备（文件）i节点
	error = namei(specialfile,&swap_inode);
	if (error)
		goto bad_swap_2;
	//将此i节点赋给swap_file
	p->swap_file = swap_inode;
	error = -EBUSY;
	if (swap_inode->i_count != 1)
		goto bad_swap_2;
	error = -EINVAL;

	//如果交换设备是块设备
	if (S_ISBLK(swap_inode->i_mode)) {
		p->swap_device = swap_inode->i_rdev;

		filp.f_inode = swap_inode;
		filp.f_mode = 3; /* read write */
		error = blkdev_open(swap_inode, &filp);
		p->swap_file = NULL;
		iput(swap_inode);
		if(error)
			goto bad_swap_2;
		error = -ENODEV;
		if (!p->swap_device)
			goto bad_swap;
		error = -EBUSY;
		//查看此交换设备是否已存在于其他的交换设备文件结构够，若是，则失败
		for (i = 0 ; i < nr_swapfiles ; i++) {
			if (i == type)	//跳过本身
				continue;
			if (p->swap_device == swap_info[i].swap_device)
				goto bad_swap;
		}
	} else if (!S_ISREG(swap_inode->i_mode))
		goto bad_swap;
	//为交换设备结构分配一页物理页面作为其交换设备锁位图
	p->swap_lockmap = (unsigned char *) get_free_page(GFP_USER);
	if (!p->swap_lockmap) {
		printk("Unable to start swapping: out of memory :-)\n");
		error = -ENOMEM;
		goto bad_swap;
	}
	//将此交换设备磁盘上存储的位图信息读入内存位图页面
	read_swap_page(SWP_ENTRY(type,0), (char *) p->swap_lockmap);
	//验证此交换设备是不是合法的交换设备（文件）
	if (memcmp("SWAP-SPACE",p->swap_lockmap+4086,10)) {
		printk("Unable to find swap-space signature\n");
		error = -EINVAL;
		goto bad_swap;
	}
	//清楚内存位图中的"SWAP-SPACE"标志
	memset(p->swap_lockmap+PAGE_SIZE-10,0,10);
	j = 0;
	p->lowest_bit = 0;
	p->highest_bit = 0;
	//初始化还可以作为交换磁盘页面的范围，*8是因为一个字节8位
	for (i = 1 ; i < 8*PAGE_SIZE ; i++) {
		if (test_bit(i,p->swap_lockmap)) {
			if (!p->lowest_bit)
				p->lowest_bit = i;
			p->highest_bit = i;
			p->max = i+1;
			j++;
		}
	}
	if (!j) {
		printk("Empty swap-file\n");
		error = -EINVAL;
		goto bad_swap;
	}
	//vmalloc()函数的一个实例 可参照理解 分配大量“虚拟连续”的内存
	p->swap_map = (unsigned char *) vmalloc(p->max);
	if (!p->swap_map) {
		error = -ENOMEM;
		goto bad_swap;
	}
	for (i = 1 ; i < p->max ; i++) {
		if (test_bit(i,p->swap_lockmap))
			p->swap_map[i] = 0;
		else
			p->swap_map[i] = 0x80;
	}
	p->swap_map[0] = 0x80;
	memset(p->swap_lockmap,0,PAGE_SIZE);
	p->flags = SWP_WRITEOK;
	p->pages = j;	//初始化本交换设备可用的磁盘页面
	nr_swap_pages += j;		//增加可用到磁盘交换页面
	printk("Adding Swap: %dk swap-space\n",j<<2);
	return 0;
bad_swap:
	if(filp.f_op && filp.f_op->release)
		filp.f_op->release(filp.f_inode,&filp);
bad_swap_2:
	free_page((long) p->swap_lockmap);
	vfree(p->swap_map);
	iput(p->swap_file);
	p->swap_device = 0;
	p->swap_file = NULL;
	p->swap_map = NULL;
	p->swap_lockmap = NULL;
	p->flags = 0;
	return error;
}

//统计交换缓冲信息 在kernel/info.c中的sys_sysinfo()被调用
void si_swapinfo(struct sysinfo *val)
{
	unsigned int i, j;

	val->freeswap = val->totalswap = 0;
	for (i = 0; i < nr_swapfiles; i++) {
		if ((swap_info[i].flags & SWP_WRITEOK) != SWP_WRITEOK)
			continue;
		for (j = 0; j < swap_info[i].max; ++j)
			switch (swap_info[i].swap_map[j]) {
				case 128:
					continue;
				case 0:
					++val->freeswap;
				default:
					++val->totalswap;
			}
	}
	val->freeswap <<= PAGE_SHIFT;
	val->totalswap <<= PAGE_SHIFT;
	return;
}

/*
 * set up the free-area data structures:
 *   - mark all pages MAP_PAGE_RESERVED
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 */
unsigned long free_area_init(unsigned long start_mem, unsigned long end_mem)
{
	mem_map_t * p;
	unsigned long mask = PAGE_MASK;
	int i;

	/*
	 * select nr of pages we try to keep free for important stuff
	 * with a minimum of 16 pages. This is totally arbitrary
	 */
	i = (end_mem - PAGE_OFFSET) >> (PAGE_SHIFT+6);
	if (i < 16)
		i = 16;
	min_free_pages = i;
	start_mem = init_swap_cache(start_mem, end_mem);
	mem_map = (mem_map_t *) start_mem;
	p = mem_map + MAP_NR(end_mem);
	start_mem = (unsigned long) p;
	//全部页面初始化为MAP_PAGE_RESERVED的
	while (p > mem_map)
		*--p = MAP_PAGE_RESERVED;

	//初始化free_area_list及free_area_map
	for (i = 0 ; i < NR_MEM_LISTS ; i++) {
		unsigned long bitmap_size;	//相应的free_area_map位图的长度
		free_area_list[i].prev = free_area_list[i].next = &free_area_list[i];
		mask += mask;	//屏蔽码 用于"对齐"页面数
		//"对齐"页面数 比如，若实际物理页面数为N=19，
		//order=1的free_area_list对应的单个list元素为2个连续的页面，则页面总数要求为2的整倍数,则N截为18
		//order=2的free_area_list对应的单个list元素为4个连续的页面，则页面总数要求为2的整倍数,则N截为16
		end_mem = (end_mem + ~mask) & mask;
		//求得相应的free_area_list链表长度
		bitmap_size = (end_mem - PAGE_OFFSET) >> (PAGE_SHIFT + i);
		//求得相应free_area_map位图的长度，+7是因为每个字节八位，可表示8个位图使用状态，右移3位表示除以8，即整个位图所需字节数
		bitmap_size = (bitmap_size + 7) >> 3;
		//因为start_mem以unsigned long类型，所以又需要一次unsigned long类型的"对齐"。
		//我觉得这里可能有一个bug，因为在memset中，bitmap_size需要的是char类型的个数
		bitmap_size = (bitmap_size + sizeof(unsigned long) - 1) & ~(sizeof(unsigned long)-1);
		free_area_map[i] = (unsigned char *) start_mem;
		memset((void *) start_mem, 0, bitmap_size);
		start_mem += bitmap_size;
	}
	return start_mem;
}
