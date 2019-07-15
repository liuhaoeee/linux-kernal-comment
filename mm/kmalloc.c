/*
 *  linux/mm/kmalloc.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds & Roger Wolff.
 *
 *  Written by R.E. Wolff Sept/Oct '93.
 *
 */
 
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
 * Modified by Alex Bligh (alex@cconcepts.co.uk) 4 Apr 1994 to use multiple
 * pages. So for 'page' throughout, read 'area'.
 */

#include <linux/mm.h>
#include <asm/system.h>
#include <linux/delay.h>

#define GFP_LEVEL_MASK 0xf

/* I want this low enough for a while to catch errors.
   I want this number to be increased in the near future:
        loadable device drivers should use this function to get memory */

#define MAX_KMALLOC_K ((PAGE_SIZE<<(NUM_AREA_ORDERS-1))>>10)


/* This defines how many times we should try to allocate a free page before
   giving up. Normally this shouldn't happen at all. */
#define MAX_GET_FREE_PAGE_TRIES 4


/* Private flags. */

#define MF_USED 0xffaa0055
#define MF_FREE 0x0055ffaa


/* 
 * Much care has gone into making these routines in this file reentrant.
 *
 * The fancy bookkeeping of nbytesmalloced and the like are only used to
 * report them to the user (oooohhhhh, aaaaahhhhh....) are not 
 * protected by cli(). (If that goes wrong. So what?)
 *
 * These routines restore the interrupt status to allow calling with ints
 * off. 
 */
//http://linux.chinaunix.net/techdoc/net/2006/04/17/931014.shtml
/* 
 * A block header. This is in front of every malloc-block, whether free or not.
 */
//由sizes[]管理的各个页面块中每个块（空闲块和占用块）的头部还有一个对该块进行描述的块头
/*
	bh_flages是块的标志，有三种：
	MF_FREE指明该块是空闲块，
	MF_USED表示该块已占用，
	MF_DMA 表示该块是DMA可访问。
	ubh_length和fbh_next是联合体成员项，
	当块占用时使用ubh_length表示该块的长度。
	当块空闲时使用fbh_next链接下一个空闲块。
	在一个页块中的空闲块组成一个链表，
	表头由页块的page_descriptor中firstfree指出。
*/
struct block_header {
	unsigned long bh_flags;	 /* 块的分配标志 */
	union {
		unsigned long ubh_length;	 /* 块长度 */
		struct block_header *fbh_next;	/*指向下一空闲块的指针 */ 
	} vp;
};


#define bh_length vp.ubh_length
#define bh_next   vp.fbh_next
#define BH(p) ((struct block_header *)(p))


/* 
 * The page descriptor is at the front of every page that malloc has in use. 
 */
/*
	对kmalloc()分配的内存页面块中加上一个信息头，它处于该页面块的前部。
	页面块中信息头后的空间是可以分配的内存空间。
	加在页面块前部的信息头称为页描述符 具有相同块单位和使用特性的页面块组成若干个链表
*/
struct page_descriptor {
	struct page_descriptor *next;	/* 指向下一个页面块的指针 */
	struct block_header *firstfree;	/* 本页中空闲块链表的头 */
	int order;	/* 本页中块长度的级别 */
	int nfree;	 /* 本页中空闲的数目 */
};


#define PAGE_DESC(p) ((struct page_descriptor *)(((unsigned long)(p)) & PAGE_MASK))


/*
 * A size descriptor describes a specific class of malloc sizes.
 * Each class of sizes has its own freelist.
 */
 
  /*由kmalloc()分配的每种块长度的页面块链接成两个链表，一个是DMA可以访问的页面块链表，dmafree指向这个链表。
  一个是一般的链表， firstfree指向这个链表。成员项gfporder是0～5，它做为2的幂数表示所含的页面数。*/
struct size_descriptor {
	struct page_descriptor *firstfree;	/* 一般页块链表的头指针 */
	struct page_descriptor *dmafree; /* DMA-able memory *//* DMA页块链表的头指针 */
	int size;		/* 页块中每块的大小 */
	int nblocks;	/* 页块中划分的块数目 */

	int nmallocs;	/* 链表中各页块中已分配的块总数 */
	int nfrees;		/* 链表中各页块中尚空闲的块总数 */
	int nbytesmalloced;	 /* 链表中各页块中已分配的字节总数 */
	int npages;		/* 链表中页块数目 */
	unsigned long gfporder; /* number of pages in the area required *//* 页块的页面数目 */
};

/*
 * For now it is unsafe to allocate bucket sizes between n & n=16 where n is
 * 4096 * any power of two
 */
// Linux设置了sizes[]数组，对页面块进行描述 数组元素是size_descriptor结构体 
//应该都是至少留下16字节的空间 用于存放struct page_descriptor结构
struct size_descriptor sizes[] = { 
	{ NULL, NULL,  32,127, 0,0,0,0, 0},		//32字节 127块 4K
	{ NULL, NULL,  64, 63, 0,0,0,0, 0 },	//2的6次方*2的6次方 2的12次方 64字节 63块 4K
	{ NULL, NULL, 128, 31, 0,0,0,0, 0 },	//128字节 3块
	{ NULL, NULL, 252, 16, 0,0,0,0, 0 },
	{ NULL, NULL, 508,  8, 0,0,0,0, 0 },	//508*8=4064 4096-4064=32 若4096-16=4080 4080/508=8
	{ NULL, NULL,1020,  4, 0,0,0,0, 0 },	//1020字节 4块 
	{ NULL, NULL,2040,  2, 0,0,0,0, 0 },	//2040字节 2块
	{ NULL, NULL,4096-16,  1, 0,0,0,0, 0 },
	{ NULL, NULL,8192-16,  1, 0,0,0,0, 1 },	//8192-16字节 1块 2个页面
	{ NULL, NULL,16384-16,  1, 0,0,0,0, 2 },	//16384-16字节 1块 4个页面
	{ NULL, NULL,32768-16,  1, 0,0,0,0, 3 },
	{ NULL, NULL,65536-16,  1, 0,0,0,0, 4 },
	{ NULL, NULL,131072-16,  1, 0,0,0,0, 5 },
	{ NULL, NULL,   0,  0, 0,0,0,0, 0 }
};


#define NBLOCKS(order)          (sizes[order].nblocks)
#define BLOCKSIZE(order)        (sizes[order].size)
#define AREASIZE(order)		(PAGE_SIZE<<(sizes[order].gfporder))

/*
	kmalloc代表的是kernel_malloc的意思，它是用于内核的内存分配函数。而这个针对kmalloc的初始化函数用来对
	内存中可用内存的大小进行检查，以确定kmalloc所能分配的内存的大小。所以，这种检查只是检测当前在系统段内
	可分配的内存块的大小
*/
long kmalloc_init (long start_mem,long end_mem)
{
	int order;

/* 
 * Check the static info array. Things will blow up terribly if it's
 * incorrect. This is a late "compile time" check.....
 */
for (order = 0;BLOCKSIZE(order);order++)
    {
	//如果本size表示的内存空间大于最大的应该拥有的内存空间 则死机
    if ((NBLOCKS (order)*BLOCKSIZE(order) + sizeof (struct page_descriptor)) >
        AREASIZE(order)) 
        {
        printk ("Cannot use %d bytes out of %d in order = %d block mallocs\n",
                (int) (NBLOCKS (order) * BLOCKSIZE(order) + 
                        sizeof (struct page_descriptor)),
                (int) AREASIZE(order),
                BLOCKSIZE (order));
        panic ("This only happens if someone messes with kmalloc");
        }
    }
return start_mem;
}


//已知size 获取对应sizes[]中对应最小元素的order，即数组下标
int get_order (int size)
{
	int order;

	/* Add the size of the header */
	size += sizeof (struct block_header); 
	for (order = 0;BLOCKSIZE(order);order++)
		if (size <= BLOCKSIZE (order))
			return order; 
	return -1;
}

void * kmalloc (size_t size, int priority)
{
	unsigned long flags;
	int order,tries,i,sz;
	int dma_flag;
	struct block_header *p;
	struct page_descriptor *page;

	dma_flag = (priority & GFP_DMA);
	priority &= GFP_LEVEL_MASK;
	  
/* Sanity check... */
	if (intr_count && priority != GFP_ATOMIC) {
		static int count = 0;
		if (++count < 5) {
			printk("kmalloc called nonatomically from interrupt %p\n",
				__builtin_return_address(0));
			priority = GFP_ATOMIC;
		}
	}

order = get_order (size);
if (order < 0)
    {
    printk ("kmalloc of too large a block (%d bytes).\n",(int) size);
    return (NULL);
    }

save_flags(flags);

/* It seems VERY unlikely to me that it would be possible that this 
   loop will get executed more than once. */
tries = MAX_GET_FREE_PAGE_TRIES; 
while (tries --)
    {
    /* Try to allocate a "recently" freed memory block */
    cli ();
	//page指向当前本页块“链表头部” p指向本页块链表头部（即可用的块）当前指向的block_header头描述符结构
    if ((page = (dma_flag ? sizes[order].dmafree : sizes[order].firstfree)) &&
        (p = page->firstfree))	
	{
		//如果当前块的头描述符结构指出其是空闲的
		if (p->bh_flags == MF_FREE)
		{
			//更新当前page_sises[]
			page->firstfree = p->bh_next;	//指向下一个可用块的头
			page->nfree--;	//本页块可用块数减一
			//如果本页块没有可用块了 则将链表置空
			if (!page->nfree)
			{
			  if(dma_flag)
				sizes[order].dmafree = page->next;
			  else
				sizes[order].firstfree = page->next;
			  page->next = NULL;
			}
			restore_flags(flags);
			//更新本页块分配信息
			sizes[order].nmallocs++;	/* 链表中各页块中已分配的块总数加一*/
			sizes[order].nbytesmalloced += size;	/* 链表中各页块中已分配的字节总数 */
			p->bh_flags =  MF_USED; /* As of now this block is officially in use */	//将本块置为已使用
			p->bh_length = size;	//本块的大小信息
			return p+1; /* Pointer arithmetic: increments past header */	//函数返回时，跳过块头信息结构
		}
		printk ("Problem: block on freelist at %08lx isn't free.\n",(long)p);
		return (NULL);
    }
    restore_flags(flags);


    /* Now we're in trouble: We need to get a new free page..... */

    sz = BLOCKSIZE(order); /* sz is the size of the blocks we're dealing with *///块大小

    /* This can be done with ints on: This is private to this invocation */
	//请求分配相应的物理页面
    if (dma_flag)
      page = (struct page_descriptor *) __get_dma_pages (priority & GFP_LEVEL_MASK, sizes[order].gfporder);
    else
      page = (struct page_descriptor *) __get_free_pages (priority & GFP_LEVEL_MASK, sizes[order].gfporder);

    if (!page) {
        static unsigned long last = 0;
        if (last + 10*HZ < jiffies) {
        	last = jiffies;
	        printk ("Couldn't get a free page.....\n");
	}
        return NULL;
    }
#if 0
    printk ("Got page %08x to use for %d byte mallocs....",(long)page,sz);
#endif
	//申请页面成功，将本page_sizes[]中的页面块数信息更新
    sizes[order].npages++;

    /* Loop for all but last block: */
	//初始化新得到的页面 将其初始化为一个page_descriptor结构
	//初始化其块头结构 这里 跳过第一个块 在NBLOCKS(order)中已经为page_descriptor结构结构预留了空间
    for (i=NBLOCKS(order),p=BH (page+1);i > 1;i--,p=p->bh_next) 
        {
        p->bh_flags = MF_FREE;
        p->bh_next = BH ( ((long)p)+sz);
        }
    /* Last block: */
	//初始化第一个块 链表尾置为空
    p->bh_flags = MF_FREE;
    p->bh_next = NULL;

    page->order = order;
    page->nfree = NBLOCKS(order); 
    page->firstfree = BH(page+1);
#if 0
    printk ("%d blocks per page\n",page->nfree);
#endif
	//将新的page_descriptor插入到链表中去
    /* Now we're going to muck with the "global" freelist for this size:
       this should be uninterruptible */
    cli ();
    /* 
     * sizes[order].firstfree used to be NULL, otherwise we wouldn't be
     * here, but you never know.... 
     */
    if (dma_flag) {
      page->next = sizes[order].dmafree;
      sizes[order].dmafree = page;
    } else {
      page->next = sizes[order].firstfree;
      sizes[order].firstfree = page;
    }
    restore_flags(flags);
    }

/* Pray that printk won't cause this to happen again :-) */

printk ("Hey. This is very funny. I tried %d times to allocate a whole\n"
        "new page for an object only %d bytes long, but some other process\n"
        "beat me to actually allocating it. Also note that this 'error'\n"
        "message is soooo very long to catch your attention. I'd appreciate\n"
        "it if you'd be so kind as to report what conditions caused this to\n"
        "the author of this kmalloc: wolff@dutecai.et.tudelft.nl.\n"
        "(Executive summary: This can't happen)\n", 
                MAX_GET_FREE_PAGE_TRIES,
                (int) size);
return NULL;
}

void kfree_s (void *ptr,int size)
{
	unsigned long flags;
	int order;
	register struct block_header *p=((struct block_header *)ptr) -1;
	struct page_descriptor *page,*pg2;

	page = PAGE_DESC (p);	//取得此内存块所在的物理页面首地址 并将其转化为page_descriptor类型的指针
	order = page->order;
	if ((order < 0) || 
		(order > sizeof (sizes)/sizeof (sizes[0])) ||
		(((long)(page->next)) & ~PAGE_MASK) ||	//如果此page_descriptor中next指向的下一项page_descriptor地址没有对齐，则视为错误
		(p->bh_flags != MF_USED))
	{
		printk ("kfree of non-kmalloced memory: %p, next= %p, order=%d\n",
					p, page->next, page->order);
		return;
	}
	if (size && size != p->bh_length)
	{
		printk ("Trying to free pointer at %p with wrong size: %d instead of %lu.\n",
			p,size,p->bh_length);
		return;
	}
	size = p->bh_length;
	p->bh_flags = MF_FREE; /* As of now this block is officially free */
	save_flags(flags);
	//将释放的块结构链到链表头部。并更新页块信息
	cli ();
	p->bh_next = page->firstfree;
	page->firstfree = p;
	page->nfree++;
	//如果释放后，空闲块为1，则说明之前为本页块的块全部分配了出去
	if (page->nfree == 1)
	   { /* Page went from full to one free block: put it on the freelist.  Do not bother
		  trying to put it on the DMA list. */
	   if (page->next)
			{
			printk ("Page %p already on freelist dazed and confused....\n", page);
			}
	   else
			{
			page->next = sizes[order].firstfree;
			sizes[order].firstfree = page;
			}
	   }

	/* If page is completely free, free it */
	//如果释放后，本页块全部空闲了，则释放此页块
	if (page->nfree == NBLOCKS (page->order))
		{
	#if 0
		printk ("Freeing page %08x.\n", (long)page);
	#endif
		if (sizes[order].firstfree == page)
			{
			sizes[order].firstfree = page->next;
			}
		else if (sizes[order].dmafree == page)
			{
			sizes[order].dmafree = page->next;
			}
		else
			{
			for (pg2=sizes[order].firstfree;
					(pg2 != NULL) && (pg2->next != page);
							pg2=pg2->next)
				/* Nothing */;
		if (!pg2)
		  for (pg2=sizes[order].dmafree;
			   (pg2 != NULL) && (pg2->next != page);
			   pg2=pg2->next)
				/* Nothing */;
			if (pg2 != NULL)
				pg2->next = page->next;
			else
				printk ("Ooops. page %p doesn't show on freelist.\n", page);
			}
	/* FIXME: I'm sure we should do something with npages here (like npages--) */
		free_pages ((long)page, sizes[order].gfporder);
		}
	restore_flags(flags);

	/* FIXME: ?? Are these increment & decrement operations guaranteed to be
	 *	     atomic? Could an IRQ not occur between the read & the write?
	 *	     Maybe yes on a x86 with GCC...??
	 */
	sizes[order].nfrees++;      /* Noncritical (monitoring) admin stuff */
	sizes[order].nbytesmalloced -= size;
}
