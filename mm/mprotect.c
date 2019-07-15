#define THREE_LEVEL
/*
 *	linux/mm/mprotect.c
 *
 *  (C) Copyright 1994 Linus Torvalds
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
	本模块只提供一个系统调用，而无其他接口。这个系统调用就是sys_mprotect.
	系统调用sys_mprotect(unsigned long start, size_t len, unsigned long prot)
	改变地址范围[start，addr+len-1]涉及到的虚拟地址以及物理页面的保护方式。对于i386就是
	改变pte中的__pgprot。
	其中prot可用的参数有：
	PROT_WRITE/PROT_READ/PROT_EXEC
	PROT_NONE:此段内存不能被访问.
	新设置的属性会完全取代现有属性,而不是叠加到现有属性上.

	此系统调用实现起来也不复杂,思路也常见了:遍历涉及到的vma,根据属性的改
	变进行fixup,同时设置所涉及之pte的prot属性.
*/
 
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/errno.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/malloc.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/pgtable.h>

/*以下的函数用于修改页表项pte的保护属性*/

static inline void change_pte_range(pmd_t * pmd, unsigned long address,
	unsigned long size, pgprot_t newprot)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		printk("change_pte_range: bad pmd (%08lx)\n", pmd_val(*pmd));
		pmd_clear(pmd);
		return;
	}
	pte = pte_offset(pmd, address);
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t entry = *pte;
		//从这里来看 函数修改的只是当前存在内存中的物理页面的页表项的属性
		if (pte_present(entry))
			*pte = pte_modify(entry, newprot);	//这是本函数的目的
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline void change_pmd_range(pgd_t * pgd, unsigned long address,
	unsigned long size, pgprot_t newprot)
{
	pmd_t * pmd;
	unsigned long end;

	if (pgd_none(*pgd))
		return;
	if (pgd_bad(*pgd)) {
		printk("change_pmd_range: bad pgd (%08lx)\n", pgd_val(*pgd));
		pgd_clear(pgd);
		return;
	}
	pmd = pmd_offset(pgd, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		change_pte_range(pmd, address, end - address, newprot);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
}

//改变从start--end范围内的页表项保护属性
static void change_protection(unsigned long start, unsigned long end, pgprot_t newprot)
{
	pgd_t *dir;

	dir = pgd_offset(current, start);
	while (start < end) {
		change_pmd_range(dir, start, end - start, newprot);
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	invalidate();
	return;
}

//修正进程虚拟地址空间vma范围内内所有的内存保护属性
static inline int mprotect_fixup_all(struct vm_area_struct * vma,
	int newflags, pgprot_t prot)
{
	vma->vm_flags = newflags;
	vma->vm_page_prot = prot;
	return 0;
}

//修正进程虚拟地址空间vma内开始的一段连续的虚拟地址空间的保护属性
static inline int mprotect_fixup_start(struct vm_area_struct * vma,
	unsigned long end,
	int newflags, pgprot_t prot)
{
	struct vm_area_struct * n;

	//申请一项新的vm_area_struct内核数据结构空间
	n = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!n)
		return -ENOMEM;
	//将要修正的vma数据结构内容完全复制到新的n中，即*n是原*vma的一个副本
	*n = *vma;
	//修改原vma的开始地址，将其改为end
	vma->vm_start = end;
	//将新的n的结束地址设为end
	n->vm_end = end;
	//更新原vma对应磁盘文件内的开始位置vm_offset，本质上是加上end-原vma_start的差值
	vma->vm_offset += vma->vm_start - n->vm_start;
	//通过改变*n代表的虚拟地址空间的保护属性，达到了此函数的真正目的
	n->vm_flags = newflags;
	n->vm_page_prot = prot;
	if (n->vm_inode)
		n->vm_inode->i_count++;
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	insert_vm_struct(current, n);
	return 0;
}

//修正进程虚拟地址空间vma内尾端的一段连续的虚拟地址空间的保护属性
static inline int mprotect_fixup_end(struct vm_area_struct * vma,
	unsigned long start,
	int newflags, pgprot_t prot)
{
	struct vm_area_struct * n;

	n = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!n)
		return -ENOMEM;
	*n = *vma;
	vma->vm_end = start;
	n->vm_start = start;
	n->vm_offset += n->vm_start - vma->vm_start;
	n->vm_flags = newflags;
	n->vm_page_prot = prot;
	if (n->vm_inode)
		n->vm_inode->i_count++;
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	insert_vm_struct(current, n);
	return 0;
}

//修正进程虚拟地址空间vma范围内中间的一段连续的虚拟地址空间的保护属性
static inline int mprotect_fixup_middle(struct vm_area_struct * vma,
	unsigned long start, unsigned long end,
	int newflags, pgprot_t prot)
{
	struct vm_area_struct * left, * right;

	left = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!left)
		return -ENOMEM;
	right = (struct vm_area_struct *) kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!right) {
		kfree(left);
		return -ENOMEM;
	}
	*left = *vma;
	*right = *vma;
	left->vm_end = start;
	vma->vm_start = start;
	vma->vm_end = end;
	right->vm_start = end;
	vma->vm_offset += vma->vm_start - left->vm_start;
	right->vm_offset += right->vm_start - left->vm_start;
	vma->vm_flags = newflags;
	vma->vm_page_prot = prot;
	if (vma->vm_inode)
		vma->vm_inode->i_count += 2;
	if (vma->vm_ops && vma->vm_ops->open) {
		vma->vm_ops->open(left);
		vma->vm_ops->open(right);
	}
	insert_vm_struct(current, left);
	insert_vm_struct(current, right);
	return 0;
}

static int mprotect_fixup(struct vm_area_struct * vma, 
	unsigned long start, unsigned long end, unsigned int newflags)
{
	pgprot_t newprot;
	int error;

	if (newflags == vma->vm_flags)
		return 0;
	newprot = protection_map[newflags & 0xf];
	if (start == vma->vm_start)
		if (end == vma->vm_end)
			error = mprotect_fixup_all(vma, newflags, newprot);
		else
			error = mprotect_fixup_start(vma, end, newflags, newprot);
	else if (end == vma->vm_end)
		error = mprotect_fixup_end(vma, start, newflags, newprot);
	else
		error = mprotect_fixup_middle(vma, start, end, newflags, newprot);

	if (error)
		return error;

	change_protection(start, end, newprot);
	return 0;
}

/*
								mprotect: 设置内存访问权限
	mmap 的第三个参数指定对内存区域的保护，由标记读、写、执行权限的 PROT_READ、PROT_WRITE 和 PROT_EXEC 按位与操作获得，
	或者是限制没有访问权限的 PROT_NONE。如果程序尝试在不允许这些权限的本地内存上操作，它将被 SIGSEGV 信号（Segmentation 
	fault，段错误）终止。在内存映射完成后，这些权限仍可以被 mprotect 系统调用所修改。mprotect 的参数分别为内存区间的地址，
	区间的大小，新的保护标志设置。所指定的内存区间必须包含整个页：区间地址必须和整个系统页大小对齐，而区间长度必须是页大
	小的整数倍。这些页的保护标记被这里指定的新保护模式替换。
*/
/*
	prot可以取以下几个值，并且可以用“|”将几个属性合起来使用：

	1）PROT_READ：表示内存段内的内容可写；

	2）PROT_WRITE：表示内存段内的内容可读；

	3）PROT_EXEC：表示内存段中的内容可执行；

	4）PROT_NONE：表示内存段中的内容根本没法访问。
*/

asmlinkage int sys_mprotect(unsigned long start, size_t len, unsigned long prot)
{
	unsigned long nstart, end, tmp;
	struct vm_area_struct * vma, * next;
	int error;

	//区间开始的地址start必须是一个内存页的起始地址，并且区间长度len必须是页大小的整数倍。
	if (start & ~PAGE_MASK)
		return -EINVAL;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		return -EINVAL;
	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
		return -EINVAL;
	if (end == start)
		return 0;
	vma = find_vma(current, start);
	if (!vma || vma->vm_start > start)
		return -EFAULT;

	for (nstart = start ; ; ) {
		unsigned int newflags;

		/* Here we know that  vma->vm_start <= nstart < vma->vm_end. */

		newflags = prot | (vma->vm_flags & ~(PROT_READ | PROT_WRITE | PROT_EXEC));
		//该内存不能设置为相应权限就会返回EACCES。这是可能发生的，比如，如果你 mmap 映射一个文件为只读的，
		//接着使用 mprotect() 标志为 PROT_WRITE。
		if ((newflags & ~(newflags >> 4)) & 0xf) {
			error = -EACCES;
			break;
		}

		if (vma->vm_end >= end) {
			error = mprotect_fixup(vma, nstart, end, newflags);
			break;
		}

		tmp = vma->vm_end;
		next = vma->vm_next;
		error = mprotect_fixup(vma, nstart, tmp, newflags);
		if (error)
			break;
		nstart = tmp;
		vma = next;
		if (!vma || vma->vm_start != nstart) {
			error = -EFAULT;
			break;
		}
	}
	merge_segments(current, start, end);
	return error;
}
