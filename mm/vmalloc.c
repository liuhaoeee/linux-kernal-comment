#define THREE_LEVEL
/*
 *  linux/mm/vmalloc.c
 *
 *  Copyright (C) 1993  Linus Torvalds
 */
 
/*
	The PDF documents about understanding Linnux Kernel 1.2 is made by Liu Yihao
	这份学习Linux内核的PDF文档由刘以浩同学整理完成
	包含了作者在学习内核过程中搜集到的大量有关Linux
	内核的知识和自己的一些理解以及注释
	当然，最宝贵的还是这份原生的内核代码
	NOTE：如果在阅读过程中遇到什么问题，欢迎和我交流讨论
	Email：liuyihaolovem@163.com
*/


 /*
	 高端物理地址的分配采用vmalloc/vfree这组函数进行，什么是高端物理内存呢？
	 我们知道Linux给内核预留了一部分虚拟地址空间，这部分虚拟地址如果能全部直
	 接映射到物理地址空间就不存在高端内存。如果这部分内存有一部分不能直接映射
	 到地址空间，那么这部分虚拟地址空间称为高端内存。因此，高端内存是虚拟地址
	 空间中的概念。举个例子：如果你的物理内存为512M，那么就不存在高端内存的分
	 配，如果你的物理地址为2G，那么有1G+128M（预留给VMALLOC区）是属于高端内存
	 的。高端内存的分配即便是逻辑上连续，也不要求物理上是连续的。
 */
 
 /*
	Linux将内核地址空间划分为三部分ZONE_DMA、ZONE_NORMAL和ZONE_HIGHMEM，高端
	内存HIGH_MEM地址空间范围为 0xF8000000 ~ 0xFFFFFFFF（896MB～1024MB）。那
	么如内核是如何借助128MB高端内存地址空间是如何实现访问可以所有物理内存？
	当内核想访问高于896MB物理地址内存时，从0xF8000000 ~ 0xFFFFFFFF地址空间
	范围内找一段相应大小空闲的逻辑地址空间，借用一会。借用这段逻辑地址空间，
	建立映射到想访问的那段物理内存（即填充内核PTE页面表），临时用一会，用完
	后归还。这样别人也可以借用这段地址空间访问其他物理内存，实现了使用有限的
	地址空间，访问所有所有物理内存。例如内核想访问2G开始的一段 大小为1MB的物理内存，
	即物理地址范围为0×80000000 ~ 0x800FFFFF。访问之前先找到一段1MB大小的空闲地址空间，
	假设找到的空闲地址空间为0xF8700000 ~ 0xF87FFFFF，用这1MB的逻辑地址空间映射到物理
	地址空间0×80000000 ~ 0x800FFFFF的内存。当内核访问完0×80000000 ~ 0x800FFFFF物理内存
	后，就将0xF8700000 ~ 0xF87FFFFF内核线性空间释放。这样其他进程或代码也可以使用
	0xF8700000 ~ 0xF87FFFFF这段地址访问其他物理内存。从上面的描述，我们可以知道高端内
	存的最基本思想：借一段地址空间，建立临时地址映射，用完后释放，达到这段地址空间可
	以循环使用，访问所有物理内存。
 */
 
 
#include <asm/system.h>

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/malloc.h>
#include <linux/mm.h>

#include <asm/segment.h>
#include <asm/pgtable.h>

//Linux用vm_struct结构来表示vmalloc使用的线性地址.vmalloc所使用的线性地址区间为: VMALLOC_START VMALLOC_END
//每一个vmalloc_area用4KB隔开,这样做是为了很容易就能捕捉到越界访问,因为中间是一个 “空洞”.

struct vm_struct {
	unsigned long flags;	// 标志位
	void * addr;	// 虚拟地址的开始
	unsigned long size;		// 分配大小
	struct vm_struct * next;
};

static struct vm_struct * vmlist = NULL;

static inline void set_pgdir(unsigned long address, pgd_t entry)
{
	struct task_struct * p;

	for_each_task(p)
		*pgd_offset(p,address) = entry;
}

//free_area_XXX函数将所指定的虚拟线性空间范围内分配并映射的全部物理内存释放
static inline void free_area_pte(pmd_t * pmd, unsigned long address, unsigned long size)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		printk("free_area_pte: bad pmd (%08lx)\n", pmd_val(*pmd));
		pmd_clear(pmd);
		return;
	}
	pte = pte_offset(pmd, address);
	address &= ~PMD_MASK;	//PMD_SIZE-1
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	while (address < end) {
		pte_t page = *pte;
		pte_clear(pte);
		address += PAGE_SIZE;
		pte++;
		if (pte_none(page))
			continue;
		if (pte_present(page)) {
			free_page(pte_page(page));
			continue;
		}
		printk("Whee.. Swapped out page in kernel page table\n");
	}
}

static inline void free_area_pmd(pgd_t * dir, unsigned long address, unsigned long size)
{
	pmd_t * pmd;
	unsigned long end;

	if (pgd_none(*dir))
		return;
	if (pgd_bad(*dir)) {
		printk("free_area_pmd: bad pgd (%08lx)\n", pgd_val(*dir));
		pgd_clear(dir);
		return;
	}
	pmd = pmd_offset(dir, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	while (address < end) {
		free_area_pte(pmd, address, end - address);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	}
}

static void free_area_pages(unsigned long address, unsigned long size)
{
	pgd_t * dir;
	unsigned long end = address + size;

	dir = pgd_offset(&init_task, address);	//为什么这里不将所有进程的页目录对应的表项清除？？？
	while (address < end) {
		free_area_pmd(dir, address, end - address);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	invalidate();
}

//alloc_area_XXX函数将所指定的虚拟线性空间范围分配并映射物理内存页面
static inline int alloc_area_pte(pte_t * pte, unsigned long address, unsigned long size)
{
	unsigned long end;

	address &= ~PMD_MASK;	//while循环中最多为一页页表表项分配内存页面，所对应的就是一项pmd对应的内存大小（一般是4M），这里是对齐基地址
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	while (address < end) {
		unsigned long page;
		if (!pte_none(*pte))
			printk("alloc_area_pte: page already exists\n");
		page = __get_free_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		*pte = mk_pte(page, PAGE_KERNEL);
		address += PAGE_SIZE;
		pte++;
	}
	return 0;
}

static inline int alloc_area_pmd(pmd_t * pmd, unsigned long address, unsigned long size)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	while (address < end) {
		pte_t * pte = pte_alloc_kernel(pmd, address);
		if (!pte)
			return -ENOMEM;
		if (alloc_area_pte(pte, address, end - address))
			return -ENOMEM;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	}
	return 0;
}

static int alloc_area_pages(unsigned long address, unsigned long size)
{
	pgd_t * dir;
	unsigned long end = address + size;

	dir = pgd_offset(&init_task, address);	//vmalloc是在内核虚拟区域申请内存
	while (address < end) {
		pmd_t *pmd = pmd_alloc_kernel(dir, address);
		if (!pmd)
			return -ENOMEM;
		if (alloc_area_pmd(pmd, address, end - address))
			return -ENOMEM;
		//由于vmalloc函数申请的"虚拟区域"位于内核空间，故将其更新到所有进程页目录中去
		//在2.4内核中，一个进程访问到相应的虚拟地址时，才会通过缺页中断建立起联系
		set_pgdir(address, *dir);	
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	invalidate();
	return 0;
}


/*
								vmalloc()/vfree()
	函数作用：从高端（如果存在，优先从高端）申请内存页面，并把申请的内存页面映射到内核的动态映射空间。
	vmalloc()函数的功能和alloc_pages(_GFP_HIGHMEM)+kmap() 的功能相似，只所以说是相似而不是相同，原因在
	于用vmalloc()申请的物理内存页面映射到内核的动态映射区（见下图），并且，用vmalloc()申请的页面的物理
	地址可能是不连续的。而alloc_pages(_GFP_HIGHMEM)+kmap()申请的页面的物理地址是连续的，被映射到内核的KMAP区。
	vmalloc分配的地址则限于vmalloc_start与vmalloc_end之间。每一块vmalloc分配的内核虚拟内存都对应一个vm_struct
	结构体（可别和vm_area_struct搞混，那可是进程虚拟内存区域的结构），不同的内核虚拟地址被4k大小的空闲区间隔，
	以防止越界）。与进程虚拟地址的特性一样，这些虚拟地址与物理内存没有简单的位移关系，必须通过内核页表
	才可转换为物理地址或物理页。它们有可能尚未被映射，在发生缺页时才真正分配物理页面。
	如果内存紧张，连续区域无法满足，调用vmalloc分配是必须的，因为它可以将物理不连续的空间组合后分配，
	所以更能满足分配要求。vmalloc可以映射高端页框，也可以映射底端页框。vmalloc的作用只是为了提供逻辑上连续的地址。。。
*/

void vfree(void * addr)
{
	struct vm_struct **p, *tmp;

	if (!addr)
		return;
	if ((PAGE_SIZE-1) & (unsigned long) addr) {
		printk("Trying to vfree() bad address (%p)\n", addr);
		return;
	}
	for (p = &vmlist ; (tmp = *p) ; p = &tmp->next) {
		if (tmp->addr == addr) {
			*p = tmp->next;
			//将线性地址空间所覆盖的范围内分配并映射的全部物理内存释放掉
			free_area_pages(VMALLOC_VMADDR(tmp->addr), tmp->size);
			kfree(tmp);
			return;
		}
	}
	printk("Trying to vfree() nonexistent vm area (%p)\n", addr);
}

//而vmalloc申请的内存则位于vmalloc_start～vmalloc_end之间，与物理地址没有简单的转换关系，
//虽然在逻辑上它们也是连续的，但是在物理上它们不要求连续。
void * vmalloc(unsigned long size)
{
	void * addr;
	struct vm_struct **p, *tmp, *area;

	/* to align the pointer to the (next) page boundary */
	////检查请求分配的内存大小有没有超过最大的物理页面数。如果超过返回 0 ，表示分配失败
	size = PAGE_ALIGN(size);
	if (!size || size > high_memory)
		return NULL;
	//kmalloc()函数与用户空间malloc一组函数类似，获得以字节为单位的一块内核内存,
	//这里是在为vm_struct结构分配内存空间
	area = (struct vm_struct *) kmalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		return NULL;
	addr = (void *) VMALLOC_START;	//VMALLOC_START开始遍历vmlist链表,将申请到的vm_struct结构插入到 vm_list链表中.
	area->size = size + PAGE_SIZE;	//不同的内核虚拟地址被4k大小(PAGE_SIZE)的空闲区间隔，以防止越界
	area->next = NULL;
	//将area插入到vmlist链表中去，我在想，是不是有bug，比如第一次申请high_memory大小的空间，第二次再申请high_memory大小的空间
	for (p = &vmlist; (tmp = *p) ; p = &tmp->next) {
		//遍历vmlist链表,将area插入到前后两者间间隙放得下area的位置
		if (size + (unsigned long) addr < (unsigned long) tmp->addr)
			break;
		addr = (void *) (tmp->size + (unsigned long) tmp->addr);
	}
	area->addr = addr;
	area->next = *p;
	*p = area;
	//将线性地址空间所覆盖的范围全部分配并映射物理内存
	if (alloc_area_pages(VMALLOC_VMADDR(addr), size)) {
		vfree(addr);
		return NULL;
	}
	return addr;
}

int vread(char *buf, char *addr, int count)
{
	struct vm_struct **p, *tmp;
	char *vaddr, *buf_start = buf;
	int n;

	for (p = &vmlist; (tmp = *p) ; p = &tmp->next) {
		vaddr = (char *) tmp->addr;
		while (addr < vaddr) {
			if (count == 0)
				goto finished;
			put_fs_byte('\0', buf++), addr++, count--;
		}
		n = tmp->size - PAGE_SIZE;
		if (addr > vaddr)
			n -= addr - vaddr;
		while (--n >= 0) {
			if (count == 0)
				goto finished;
			put_fs_byte(*addr++, buf++), count--;
		}
	}
finished:
	return buf - buf_start;
}
