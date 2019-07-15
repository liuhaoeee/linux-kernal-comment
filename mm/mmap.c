/*
 *	linux/mm/mmap.c
 *
 * Written by obz.
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

static int anon_map(struct inode *, struct file *, struct vm_area_struct *);

/*
 * description of effects of mapping type and prot in current implementation.
 * this is due to the limited x86 page protection hardware.  The expected
 * behavior is in parens:
 *
 * map_type	prot
 *		PROT_NONE	PROT_READ	PROT_WRITE	PROT_EXEC
 * MAP_SHARED	r: (no) no	r: (yes) yes	r: (no) yes	r: (no) yes
 *		w: (no) no	w: (no) no	w: (yes) yes	w: (no) no
 *		x: (no) no	x: (no) yes	x: (no) yes	x: (yes) yes
 *		
 * MAP_PRIVATE	r: (no) no	r: (yes) yes	r: (no) yes	r: (no) yes
 *		w: (no) no	w: (no) no	w: (copy) copy	w: (no) no
 *		x: (no) no	x: (no) yes	x: (no) yes	x: (yes) yes
 *
 */

pgprot_t protection_map[16] = {
	__P000, __P001, __P010, __P011, __P100, __P101, __P110, __P111,
	__S000, __S001, __S010, __S011, __S100, __S101, __S110, __S111
};

//Linux使用do_mmap()函数完成可执行映像向虚存区域的映射，由它建立有关的虚存区域
/*
	addr欲映射的起始地址，即用户层的start
	len是这个虚存区域的长度。
	file是指向该文件结构体的指针，
	off是相对于文件起始位置的偏移量。
	若file为NULL，称为匿名映射(anonymous mapping)。
	prot指定了虚存区域的访问特性：
	PROT_READ   0x1  对虚存区域允许读取
	PROT_WEITE  0x2  对虚存区域允许写入
	PROT_EXEC   0x4  虚存区域（代码）允许执行
	PROT_NONE   0x0  不允许访问该虚存区域
	flag指定了虚存区域的属性：
	MAP_FIXED   指定虚存区域固定在addr的位置上。
	MAP_SHARED  指定对虚存区域的操作是作用在共享页面上   
	MAP_PRIVATE指定了对虚存区域的写入操作将引起页面拷
*/
/*
	do_mmap在Linux虚拟内存管理中是一个很重要的函数，它的主要功能是将可执行文件的映象映射到虚拟内存中，
	或将别的进程中的内存信息映射到该进程的虚拟空间中。并将映射了的虚拟块的vma加入到该进程的vma avl树中。
	其运行的流程如下:
	检验给定的映射长度len是大于1页，小于一个任务的最大长度3G且加上进程的加上偏移量off不会溢出。如不满足则退出。
	如果当前任务的内存是上锁的，检验加上len后是否会超过当前进程上锁长度的界限。如是则退出。
	如果从文件映射，检验文件是否有读的权限。如无在退出。
	调用get_unmaped取得从地址address开始未映射的连续虚拟空间大于len的虚存块。
	如从文件映射，保证该文件控制块有相应的映射操作。
	为映射组织该区域申请vma结构。
	调用vm_enough_memory有足够的内存。如无则释放6中申请的vma，退出。
	如果是文件映射，调用file->;f_op_mmap将该文件映射如vma中。
	调用insert_vm_struct将vma插入该进程的avl树中。
	归并该avl树。
*/
unsigned long do_mmap(struct file * file, unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long off)
{
	int error;
	struct vm_area_struct * vma;

	//#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)
	//检测len是否越界，len的范围在0~TASK_SIZE之间
	if ((len = PAGE_ALIGN(len)) == 0)
		return addr;

	//如果要映射的虚拟地址addr大于任务的最大长度TASK_SIZE或者要映射的长度大于任务的最大长度TASK_SIZE，或者addr > TASK_SIZE-len
	//则返回出错信息
	if (addr > TASK_SIZE || len > TASK_SIZE || addr > TASK_SIZE-len)
		return -EINVAL;

	/* offset overflow? */
	if (off + len < off)
		return -EINVAL;

	/*
	 * do simple checking here so the lower-level routines won't have
	 * to. we assume access permissions have been handled by the open
	 * of the memory object, so we don't do any here.
	 */

	if (file != NULL) {
		switch (flags & MAP_TYPE) {
		//MAP_SHARED 对映射区域的写入数据会复制回文件内，而且允许其他映射该文件的进程共享
		case MAP_SHARED:
			//如果传入的参数prot中包含要对映射的文件区域有写操作，但是文件本身禁止写操作，则返回EACCES
			if ((prot & PROT_WRITE) && !(file->f_mode & 2))
				return -EACCES;
			/* fall through */
		//MAP_PRIVATE 对映射区域的写入操作会产生一个映射文件的复制，即私人的“写入时复制”（copy on write）
		//对此区域作的任何修改都不会写回原来的文件内容
		case MAP_PRIVATE:
			//如果文件不可读，则返回EACCESS	NOTE:上一个case无break
			if (!(file->f_mode & 1))
				return -EACCES;
			break;

		default:
			return -EINVAL;
		}
		//MAP_DENYWRITE只允许对映射区域的写入操作，其他对文件直接写入的操作将会被拒绝
		//如果文件的inode内存节点的写进程数存在，则返回ETXTBSY
		if ((flags & MAP_DENYWRITE) && (file->f_inode->i_wcount > 0))
			return -ETXTBSY;
	} else if ((flags & MAP_TYPE) != MAP_PRIVATE)
		return -EINVAL;

	/*
	 * obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 */

	//MAP_FIXED 如果参数start所指的地址无法成功建立映射时，则放弃映射，不对地址做修正。通常不鼓励用此旗标
	//FIXED--固定的 不变的
	if (flags & MAP_FIXED) {
		if (addr & ~PAGE_MASK)
			return -EINVAL;
		if (len > TASK_SIZE || addr > TASK_SIZE - len)	//跨到了内核空间，返回错误码 
			return -EINVAL;
	} else {
		//在用户地址空间中寻找个合适的地址 通过 get_unmapped_area完成
		addr = get_unmapped_area(len);	//此函数内部对齐了addr 
		if (!addr)
			return -ENOMEM;
	}

	/*
	 * determine the object being mapped and call the appropriate
	 * specific mapper. the address has already been validated, but
	 * not unmapped, but the maps are removed from the list.
	 */
	if (file && (!file->f_op || !file->f_op->mmap))
		return -ENODEV;

	//在内核中申请一个新的内核数据结构vma
	vma = (struct vm_area_struct *)kmalloc(sizeof(struct vm_area_struct),
		GFP_KERNEL);
	if (!vma)
		return -ENOMEM;
	//初始化此vma结构 使其代表要映射的地址空间
	vma->vm_task = current;
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_flags = prot & (VM_READ | VM_WRITE | VM_EXEC);
	vma->vm_flags |= flags & (VM_GROWSDOWN | VM_DENYWRITE | VM_EXECUTABLE);

	if (file) {
		//如果文件可读
		if (file->f_mode & 1)
			vma->vm_flags |= VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
		if (flags & MAP_SHARED) {
			vma->vm_flags |= VM_SHARED | VM_MAYSHARE;	//尝试允许其他进程共享
			/*
			 * This looks strange, but when we don't have the file open
			 * for writing, we can demote（使降级） the shared mapping to a simpler
			 * private mapping. That also takes care of a security hole
			 * with ptrace() writing to a shared mapping without write
			 * permissions.
			 *
			 * We leave the VM_MAYSHARE bit on, just to get correct output
			 * from /proc/xxx/maps..
			 */
			 //如果文件不可写
			if (!(file->f_mode & 2))
				vma->vm_flags &= ~(VM_MAYWRITE | VM_SHARED);
		}
	} else
		vma->vm_flags |= VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;	//匿名映射
	vma->vm_page_prot = protection_map[vma->vm_flags & 0x0f];
	vma->vm_ops = NULL;
	vma->vm_offset = off;
	vma->vm_inode = NULL;	//匿名文件映射不会关联文件的i节点
	vma->vm_pte = 0;

	do_munmap(addr, len);	/* Clear old maps */

	if (file)
		//如果file不为空，则不是匿名映射，也说明此文件所属文件系统
		//支持mmap（支持bmap()）,则调用文件的mmap函数（一般是内核提供的通用
		//mmap函数--mm/filemap.c-generic_mmap()），函数实质是将vma关联文件的
		//i节点vma->vm_inode，并赋予其操作方法vma->vm_ops，以使其建立映射之后
		//不需要加载文件内容至内存，当发生缺页中断时可以根据vam计算出文件的磁盘块号并读入
		//由于vma保存了文件的i节点，而且支持bmap()函数，所以可以动态加载
		error = file->f_op->mmap(file->f_inode, file, vma);
	else
		//file为空，即为匿名映射。即文件不支持mmap(根本原因在于不支持bmap()：不能根据文件的逻辑块号
		//计算出其在磁盘上的物理块号)。进行匿名映射，只是申请建立相应的虚拟地址空间，并将其对应的进程表项
		//全部映射到内核“零页”
		error = anon_map(NULL, NULL, vma);
	
	if (error) {
		kfree(vma);
		return error;
	}
	insert_vm_struct(current, vma);
	merge_segments(current, vma->vm_start, vma->vm_end);
	return addr;
}

/*
 * Get an address range which is currently unmapped.
 * For mmap() without MAP_FIXED and shmat() with addr=0.
 * Return value 0 means ENOMEM.
 */
unsigned long get_unmapped_area(unsigned long len)
{
	struct vm_area_struct * vmm;
	unsigned long gap_start = 0, gap_end;

	for (vmm = current->mm->mmap; ; vmm = vmm->vm_next) {
		if (gap_start < SHM_RANGE_START)
			gap_start = SHM_RANGE_START;
		if (!vmm || ((gap_end = vmm->vm_start) > SHM_RANGE_END))
			gap_end = SHM_RANGE_END;
		gap_start = PAGE_ALIGN(gap_start);
		gap_end &= PAGE_MASK;
		if ((gap_start <= gap_end) && (gap_end - gap_start >= len))
			return gap_start;
		if (!vmm)
			return 0;
		gap_start = vmm->vm_end;
	}
}

asmlinkage int sys_mmap(unsigned long *buffer)
{
	int error;
	unsigned long flags;
	struct file * file = NULL;

	error = verify_area(VERIFY_READ, buffer, 6*sizeof(long));
	if (error)
		return error;
	flags = get_fs_long(buffer+3);
	if (!(flags & MAP_ANONYMOUS)) {
		unsigned long fd = get_fs_long(buffer+4);
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			return -EBADF;
	}
	return do_mmap(file, get_fs_long(buffer), get_fs_long(buffer+1),
		get_fs_long(buffer+2), flags, get_fs_long(buffer+5));
}


/*
 * Searching a VMA in the linear list task->mm->mmap is horribly slow.
 * Use an AVL (Adelson-Velskii and Landis) tree to speed up this search
 * from O(n) to O(log n), where n is the number of VMAs of the task
 * (typically around 6, but may reach 3000 in some cases).
 * Written by Bruno Haible <haible@ma2s2.mathematik.uni-karlsruhe.de>.
 */

/* We keep the list and tree sorted by address. */
#define vm_avl_key	vm_end
#define vm_avl_key_t	unsigned long	/* typeof(vma->avl_key) */

/*
 * task->mm->mmap_avl is the AVL tree corresponding to task->mm->mmap
 * or, more exactly, its root.
 * A vm_area_struct has the following fields:
 *   vm_avl_left     left son of a tree node
 *   vm_avl_right    right son of a tree node
 *   vm_avl_height   1+max(heightof(left),heightof(right))
 * The empty tree is represented as NULL.
 */
#define avl_empty	(struct vm_area_struct *) NULL

/* Since the trees are balanced, their height will never be large. */
#define avl_maxheight	41	/* why this? a small exercise */
#define heightof(tree)	((tree) == avl_empty ? 0 : (tree)->vm_avl_height)
/*
 * Consistency and balancing rules:
 * 1. tree->vm_avl_height == 1+max(heightof(tree->vm_avl_left),heightof(tree->vm_avl_right))
 * 2. abs( heightof(tree->vm_avl_left) - heightof(tree->vm_avl_right) ) <= 1
 * 3. foreach node in tree->vm_avl_left: node->vm_avl_key <= tree->vm_avl_key,
 *    foreach node in tree->vm_avl_right: node->vm_avl_key >= tree->vm_avl_key.
 */

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
struct vm_area_struct * find_vma (struct task_struct * task, unsigned long addr)
{
#if 0 /* equivalent, but slow */
	struct vm_area_struct * vma;

	for (vma = task->mm->mmap ; ; vma = vma->vm_next) {
		if (!vma)
			return NULL;
		if (vma->vm_end > addr)
			return vma;
	}
#else
	struct vm_area_struct * result = NULL;
	struct vm_area_struct * tree;

	for (tree = task->mm->mmap_avl ; ; ) {
		if (tree == avl_empty)
			return result;
		if (tree->vm_end > addr) {
			if (tree->vm_start <= addr)
				return tree;
			result = tree;
			tree = tree->vm_avl_left;
		} else
			tree = tree->vm_avl_right;
	}
#endif
}

/* Look up the first VMA which intersects the interval start_addr..end_addr-1,
   NULL if none.  Assume start_addr < end_addr. */
struct vm_area_struct * find_vma_intersection (struct task_struct * task, unsigned long start_addr, unsigned long end_addr)
{
	struct vm_area_struct * vma;

#if 0 /* equivalent, but slow */
	for (vma = task->mm->mmap; vma; vma = vma->vm_next) {
		if (end_addr <= vma->vm_start)
			break;
		if (start_addr < vma->vm_end)
			return vma;
	}
	return NULL;
#else
	vma = find_vma(task,start_addr);
	if (!vma || end_addr <= vma->vm_start)
		return NULL;
	return vma;
#endif
}

/* Look up the nodes at the left and at the right of a given node. */
static void avl_neighbours (struct vm_area_struct * node, struct vm_area_struct * tree, struct vm_area_struct ** to_the_left, struct vm_area_struct ** to_the_right)
{
	vm_avl_key_t key = node->vm_avl_key;

	*to_the_left = *to_the_right = NULL;
	for (;;) {
		if (tree == avl_empty) {
			printk("avl_neighbours: node not found in the tree\n");
			return;
		}
		if (key == tree->vm_avl_key)
			break;
		if (key < tree->vm_avl_key) {
			*to_the_right = tree;
			tree = tree->vm_avl_left;
		} else {
			*to_the_left = tree;
			tree = tree->vm_avl_right;
		}
	}
	if (tree != node) {
		printk("avl_neighbours: node not exactly found in the tree\n");
		return;
	}
	if (tree->vm_avl_left != avl_empty) {
		struct vm_area_struct * node;
		for (node = tree->vm_avl_left; node->vm_avl_right != avl_empty; node = node->vm_avl_right)
			continue;
		*to_the_left = node;
	}
	if (tree->vm_avl_right != avl_empty) {
		struct vm_area_struct * node;
		for (node = tree->vm_avl_right; node->vm_avl_left != avl_empty; node = node->vm_avl_left)
			continue;
		*to_the_right = node;
	}
	if ((*to_the_left && ((*to_the_left)->vm_next != node)) || (node->vm_next != *to_the_right))
		printk("avl_neighbours: tree inconsistent with list\n");
}

/*
 * Rebalance a tree.
 * After inserting or deleting a node of a tree we have a sequence of subtrees
 * nodes[0]..nodes[k-1] such that
 * nodes[0] is the root and nodes[i+1] = nodes[i]->{vm_avl_left|vm_avl_right}.
 */
static void avl_rebalance (struct vm_area_struct *** nodeplaces_ptr, int count)
{
	for ( ; count > 0 ; count--) {
		struct vm_area_struct ** nodeplace = *--nodeplaces_ptr;
		struct vm_area_struct * node = *nodeplace;
		struct vm_area_struct * nodeleft = node->vm_avl_left;
		struct vm_area_struct * noderight = node->vm_avl_right;
		int heightleft = heightof(nodeleft);
		int heightright = heightof(noderight);
		if (heightright + 1 < heightleft) {
			/*                                                      */
			/*                            *                         */
			/*                          /   \                       */
			/*                       n+2      n                     */
			/*                                                      */
			struct vm_area_struct * nodeleftleft = nodeleft->vm_avl_left;
			struct vm_area_struct * nodeleftright = nodeleft->vm_avl_right;
			int heightleftright = heightof(nodeleftright);
			if (heightof(nodeleftleft) >= heightleftright) {
				/*                                                        */
				/*                *                    n+2|n+3            */
				/*              /   \                  /    \             */
				/*           n+2      n      -->      /   n+1|n+2         */
				/*           / \                      |    /    \         */
				/*         n+1 n|n+1                 n+1  n|n+1  n        */
				/*                                                        */
				node->vm_avl_left = nodeleftright; nodeleft->vm_avl_right = node;
				nodeleft->vm_avl_height = 1 + (node->vm_avl_height = 1 + heightleftright);
				*nodeplace = nodeleft;
			} else {
				/*                                                        */
				/*                *                     n+2               */
				/*              /   \                 /     \             */
				/*           n+2      n      -->    n+1     n+1           */
				/*           / \                    / \     / \           */
				/*          n  n+1                 n   L   R   n          */
				/*             / \                                        */
				/*            L   R                                       */
				/*                                                        */
				nodeleft->vm_avl_right = nodeleftright->vm_avl_left;
				node->vm_avl_left = nodeleftright->vm_avl_right;
				nodeleftright->vm_avl_left = nodeleft;
				nodeleftright->vm_avl_right = node;
				nodeleft->vm_avl_height = node->vm_avl_height = heightleftright;
				nodeleftright->vm_avl_height = heightleft;
				*nodeplace = nodeleftright;
			}
		}
		else if (heightleft + 1 < heightright) {
			/* similar to the above, just interchange 'left' <--> 'right' */
			struct vm_area_struct * noderightright = noderight->vm_avl_right;
			struct vm_area_struct * noderightleft = noderight->vm_avl_left;
			int heightrightleft = heightof(noderightleft);
			if (heightof(noderightright) >= heightrightleft) {
				node->vm_avl_right = noderightleft; noderight->vm_avl_left = node;
				noderight->vm_avl_height = 1 + (node->vm_avl_height = 1 + heightrightleft);
				*nodeplace = noderight;
			} else {
				noderight->vm_avl_left = noderightleft->vm_avl_right;
				node->vm_avl_right = noderightleft->vm_avl_left;
				noderightleft->vm_avl_right = noderight;
				noderightleft->vm_avl_left = node;
				noderight->vm_avl_height = node->vm_avl_height = heightrightleft;
				noderightleft->vm_avl_height = heightright;
				*nodeplace = noderightleft;
			}
		}
		else {
			int height = (heightleft<heightright ? heightright : heightleft) + 1;
			if (height == node->vm_avl_height)
				break;
			node->vm_avl_height = height;
		}
	}
}

/* Insert a node into a tree. */
static void avl_insert (struct vm_area_struct * new_node, struct vm_area_struct ** ptree)
{
	vm_avl_key_t key = new_node->vm_avl_key;
	struct vm_area_struct ** nodeplace = ptree;
	struct vm_area_struct ** stack[avl_maxheight];
	int stack_count = 0;
	struct vm_area_struct *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	for (;;) {
		struct vm_area_struct * node = *nodeplace;
		if (node == avl_empty)
			break;
		*stack_ptr++ = nodeplace; stack_count++;
		if (key < node->vm_avl_key)
			nodeplace = &node->vm_avl_left;
		else
			nodeplace = &node->vm_avl_right;
	}
	new_node->vm_avl_left = avl_empty;
	new_node->vm_avl_right = avl_empty;
	new_node->vm_avl_height = 1;
	*nodeplace = new_node;
	avl_rebalance(stack_ptr,stack_count);
}

/* Insert a node into a tree, and
 * return the node to the left of it and the node to the right of it.
 */
static void avl_insert_neighbours (struct vm_area_struct * new_node, struct vm_area_struct ** ptree,
	struct vm_area_struct ** to_the_left, struct vm_area_struct ** to_the_right)
{
	vm_avl_key_t key = new_node->vm_avl_key;
	struct vm_area_struct ** nodeplace = ptree;
	struct vm_area_struct ** stack[avl_maxheight];
	int stack_count = 0;
	struct vm_area_struct *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	*to_the_left = *to_the_right = NULL;
	for (;;) {
		struct vm_area_struct * node = *nodeplace;
		if (node == avl_empty)
			break;
		*stack_ptr++ = nodeplace; stack_count++;
		if (key < node->vm_avl_key) {
			*to_the_right = node;
			nodeplace = &node->vm_avl_left;
		} else {
			*to_the_left = node;
			nodeplace = &node->vm_avl_right;
		}
	}
	new_node->vm_avl_left = avl_empty;
	new_node->vm_avl_right = avl_empty;
	new_node->vm_avl_height = 1;
	*nodeplace = new_node;
	avl_rebalance(stack_ptr,stack_count);
}

/* Removes a node out of a tree. */
static void avl_remove (struct vm_area_struct * node_to_delete, struct vm_area_struct ** ptree)
{
	vm_avl_key_t key = node_to_delete->vm_avl_key;
	struct vm_area_struct ** nodeplace = ptree;
	struct vm_area_struct ** stack[avl_maxheight];
	int stack_count = 0;
	struct vm_area_struct *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	struct vm_area_struct ** nodeplace_to_delete;
	for (;;) {
		struct vm_area_struct * node = *nodeplace;
		if (node == avl_empty) {
			/* what? node_to_delete not found in tree? */
			printk("avl_remove: node to delete not found in tree\n");
			return;
		}
		*stack_ptr++ = nodeplace; stack_count++;
		if (key == node->vm_avl_key)
			break;
		if (key < node->vm_avl_key)
			nodeplace = &node->vm_avl_left;
		else
			nodeplace = &node->vm_avl_right;
	}
	nodeplace_to_delete = nodeplace;
	/* Have to remove node_to_delete = *nodeplace_to_delete. */
	if (node_to_delete->vm_avl_left == avl_empty) {
		*nodeplace_to_delete = node_to_delete->vm_avl_right;
		stack_ptr--; stack_count--;
	} else {
		struct vm_area_struct *** stack_ptr_to_delete = stack_ptr;
		struct vm_area_struct ** nodeplace = &node_to_delete->vm_avl_left;
		struct vm_area_struct * node;
		for (;;) {
			node = *nodeplace;
			if (node->vm_avl_right == avl_empty)
				break;
			*stack_ptr++ = nodeplace; stack_count++;
			nodeplace = &node->vm_avl_right;
		}
		*nodeplace = node->vm_avl_left;
		/* node replaces node_to_delete */
		node->vm_avl_left = node_to_delete->vm_avl_left;
		node->vm_avl_right = node_to_delete->vm_avl_right;
		node->vm_avl_height = node_to_delete->vm_avl_height;
		*nodeplace_to_delete = node; /* replace node_to_delete */
		*stack_ptr_to_delete = &node->vm_avl_left; /* replace &node_to_delete->vm_avl_left */
	}
	avl_rebalance(stack_ptr,stack_count);
}

#ifdef DEBUG_AVL

/* print a list */
static void printk_list (struct vm_area_struct * vma)
{
	printk("[");
	while (vma) {
		printk("%08lX-%08lX", vma->vm_start, vma->vm_end);
		vma = vma->vm_next;
		if (!vma)
			break;
		printk(" ");
	}
	printk("]");
}

/* print a tree */
static void printk_avl (struct vm_area_struct * tree)
{
	if (tree != avl_empty) {
		printk("(");
		if (tree->vm_avl_left != avl_empty) {
			printk_avl(tree->vm_avl_left);
			printk("<");
		}
		printk("%08lX-%08lX", tree->vm_start, tree->vm_end);
		if (tree->vm_avl_right != avl_empty) {
			printk(">");
			printk_avl(tree->vm_avl_right);
		}
		printk(")");
	}
}

static char *avl_check_point = "somewhere";

/* check a tree's consistency and balancing */
static void avl_checkheights (struct vm_area_struct * tree)
{
	int h, hl, hr;

	if (tree == avl_empty)
		return;
	avl_checkheights(tree->vm_avl_left);
	avl_checkheights(tree->vm_avl_right);
	h = tree->vm_avl_height;
	hl = heightof(tree->vm_avl_left);
	hr = heightof(tree->vm_avl_right);
	if ((h == hl+1) && (hr <= hl) && (hl <= hr+1))
		return;
	if ((h == hr+1) && (hl <= hr) && (hr <= hl+1))
		return;
	printk("%s: avl_checkheights: heights inconsistent\n",avl_check_point);
}

/* check that all values stored in a tree are < key */
static void avl_checkleft (struct vm_area_struct * tree, vm_avl_key_t key)
{
	if (tree == avl_empty)
		return;
	avl_checkleft(tree->vm_avl_left,key);
	avl_checkleft(tree->vm_avl_right,key);
	if (tree->vm_avl_key < key)
		return;
	printk("%s: avl_checkleft: left key %lu >= top key %lu\n",avl_check_point,tree->vm_avl_key,key);
}

/* check that all values stored in a tree are > key */
static void avl_checkright (struct vm_area_struct * tree, vm_avl_key_t key)
{
	if (tree == avl_empty)
		return;
	avl_checkright(tree->vm_avl_left,key);
	avl_checkright(tree->vm_avl_right,key);
	if (tree->vm_avl_key > key)
		return;
	printk("%s: avl_checkright: right key %lu <= top key %lu\n",avl_check_point,tree->vm_avl_key,key);
}

/* check that all values are properly increasing */
static void avl_checkorder (struct vm_area_struct * tree)
{
	if (tree == avl_empty)
		return;
	avl_checkorder(tree->vm_avl_left);
	avl_checkorder(tree->vm_avl_right);
	avl_checkleft(tree->vm_avl_left,tree->vm_avl_key);
	avl_checkright(tree->vm_avl_right,tree->vm_avl_key);
}

/* all checks */
static void avl_check (struct task_struct * task, char *caller)
{
	avl_check_point = caller;
/*	printk("task \"%s\", %s\n",task->comm,caller); */
/*	printk("task \"%s\" list: ",task->comm); printk_list(task->mm->mmap); printk("\n"); */
/*	printk("task \"%s\" tree: ",task->comm); printk_avl(task->mm->mmap_avl); printk("\n"); */
	avl_checkheights(task->mm->mmap_avl);
	avl_checkorder(task->mm->mmap_avl);
}

#endif


/*
 * Normal function to fix up a mapping
 * This function is the default for when an area has no specific
 * function.  This may be used as part of a more specific routine.
 * This function works out what part of an area is affected and
 * adjusts the mapping information.  Since the actual page
 * manipulation is done in do_mmap(), none need be done here,
 * though it would probably be more appropriate.
 *
 * By the time this function is called, the area struct has been
 * removed from the process mapping list, so it needs to be
 * reinserted if necessary.
 *
 * The 4 main cases are:
 *    Unmapping the whole area
 *    Unmapping from the start of the segment to a point in it
 *    Unmapping from an intermediate point to the end
 *    Unmapping between to intermediate points, making a hole.
 *
 * Case 4 involves the creation of 2 new areas, for each side of
 * the hole.
 */
 /*
	释放虚拟空间中的某些区域的时候，将会出现四种情况：
	将整个vma释放掉
	将vma的前半部分释放掉
	将vma的后半部分释放掉
	将vma的中间部分释放掉
	为了正常维护vma树，当第一种情况是，将整个vma释放掉。同时释放vma结构所占的空间。
	第二种，释放后半部分，修改vma的相关信息。第三种，释放前半部分，修改vma的相关信息。
	第四种，由于在vma中出现了一个洞，则需增加一个vma结构描述新出现的vma块。
	unmap_fixup所执行的工作就是当释放空间时，修正对vma树的影响。
 */
void unmap_fixup(struct vm_area_struct *area,
		 unsigned long addr, size_t len)
{
	struct vm_area_struct *mpnt;
	unsigned long end = addr + len;

	if (addr < area->vm_start || addr >= area->vm_end ||
	    end <= area->vm_start || end > area->vm_end ||
	    end < addr)
	{
		printk("unmap_fixup: area=%lx-%lx, unmap %lx-%lx!!\n",
		       area->vm_start, area->vm_end, addr, end);
		return;
	}

	/* Unmapping the whole area */
	if (addr == area->vm_start && end == area->vm_end) {
		if (area->vm_ops && area->vm_ops->close)
			area->vm_ops->close(area);
		if (area->vm_inode)
			iput(area->vm_inode);
		return;
	}

	/* Work out to one of the ends */
	if (end == area->vm_end)
		area->vm_end = addr;
	else
	if (addr == area->vm_start) {
		area->vm_offset += (end - area->vm_start);
		area->vm_start = end;
	}
	else {
	/* Unmapping a hole: area->vm_start < addr <= end < area->vm_end */
		/* Add end mapping -- leave beginning for below */
		mpnt = (struct vm_area_struct *)kmalloc(sizeof(*mpnt), GFP_KERNEL);

		if (!mpnt)
			return;
		*mpnt = *area;
		mpnt->vm_offset += (end - area->vm_start);
		mpnt->vm_start = end;
		if (mpnt->vm_inode)
			mpnt->vm_inode->i_count++;
		if (mpnt->vm_ops && mpnt->vm_ops->open)
			mpnt->vm_ops->open(mpnt);
		area->vm_end = addr;	/* Truncate area */
		insert_vm_struct(current, mpnt);
	}

	/* construct whatever mapping is needed */
	mpnt = (struct vm_area_struct *)kmalloc(sizeof(*mpnt), GFP_KERNEL);
	if (!mpnt)
		return;
	*mpnt = *area;
	if (mpnt->vm_ops && mpnt->vm_ops->open)
		mpnt->vm_ops->open(mpnt);
	if (area->vm_ops && area->vm_ops->close) {
		area->vm_end = area->vm_start;
		area->vm_ops->close(area);
	}
	insert_vm_struct(current, mpnt);
}

asmlinkage int sys_munmap(unsigned long addr, size_t len)
{
	return do_munmap(addr, len);
}

/*
 * Munmap is split into 2 main parts -- this part which finds
 * what needs doing, and the areas themselves, which do the
 * work.  This now handles partial unmappings.
 * Jeremy Fitzhardine <jeremy@sw.oz.au>
 */
 
 /*
					do_mmap()/do_ummap()
	内核使用do_mmap()函数为进程创建一个新的线性地址区间。但是说该函数创建了
	一个新VMA并不非常准确，因为如果创建的地址区间和一个已经存在的地址区间相邻，
	并且它们具有相同的访问权限的话，那么两个区间将合并为一个。如果不能合并，
	那么就确实需要创建一个新的VMA了。但无论哪种情况， do_mmap()函数都会将一个
	地址区间加入到进程的地址空间中－－无论是扩展已存在的内存区域还是创建一个新的区域。
	同样，释放一个内存区域应使用函数do_ummap()，它会销毁对应的内存区域。
 */
 /*
	do_munmap将释放落在从地址addr开始，长度为len空间内的vma所对应的虚拟空间。do_munmap被系统调用sys_munmap所调用
	下面是该函数的流程：
	通过find_vma根据addr找到第一块vma->;end>;addr的vma块mpnt。
	调用avl_neighbours找到mpnt在链表中的相邻指针prev和next。
	将检查中所有与虚拟空间addr~addr+len相交的vma块放入free链表中。同时如果该vma链接在共享内存中，则将其从该环形链表中释放出来。
	按序搜索free链表，调用unmap_fixup释放空间。
	调用zap_page_range将指向释放掉的虚拟空间中的pte页表项清零。
	调用kfree释放mpnt结构占用的空间。
 */
int do_munmap(unsigned long addr, size_t len)
{
	struct vm_area_struct *mpnt, *prev, *next, **npp, *free;

	if ((addr & ~PAGE_MASK) || addr > TASK_SIZE || len > TASK_SIZE-addr)
		return -EINVAL;

	if ((len = PAGE_ALIGN(len)) == 0)
		return 0;

	/*
	 * Check if this memory area is ok - put it on the temporary
	 * list if so..  The checks here are pretty simple --
	 * every area affected in some way (by any overlap) is put
	 * on the list.  If nothing is put on, nothing is affected.
	 */
	mpnt = find_vma(current, addr);
	if (!mpnt)
		return 0;
	avl_neighbours(mpnt, current->mm->mmap_avl, &prev, &next);
	/* we have  prev->vm_next == mpnt && mpnt->vm_next = next */
	/* and  addr < mpnt->vm_end  */

	npp = (prev ? &prev->vm_next : &current->mm->mmap);
	free = NULL;
	for ( ; mpnt && mpnt->vm_start < addr+len; mpnt = *npp) {
		*npp = mpnt->vm_next;
		mpnt->vm_next = free;
		free = mpnt;
		avl_remove(mpnt, &current->mm->mmap_avl);
	}

	if (free == NULL)
		return 0;

	/*
	 * Ok - we have the memory areas we should free on the 'free' list,
	 * so release them, and unmap the page range..
	 * If the one of the segments is only being partially unmapped,
	 * it will put new vm_area_struct(s) into the address space.
	 */
	while (free) {
		unsigned long st, end;

		mpnt = free;
		free = free->vm_next;

		remove_shared_vm_struct(mpnt);

		st = addr < mpnt->vm_start ? mpnt->vm_start : addr;
		end = addr+len;
		end = end > mpnt->vm_end ? mpnt->vm_end : end;

		if (mpnt->vm_ops && mpnt->vm_ops->unmap)
			mpnt->vm_ops->unmap(mpnt, st, end-st);

		unmap_fixup(mpnt, st, end-st);
		kfree(mpnt);
	}

	unmap_page_range(addr, len);
	return 0;
}

/* Build the AVL tree corresponding to the VMA list. */
void build_mmap_avl(struct task_struct * task)
{
	struct vm_area_struct * vma;

	task->mm->mmap_avl = NULL;
	for (vma = task->mm->mmap; vma; vma = vma->vm_next)
		avl_insert(vma, &task->mm->mmap_avl);
}

/* Release all mmaps. */
void exit_mmap(struct task_struct * task)
{
	struct vm_area_struct * mpnt;

	mpnt = task->mm->mmap;
	task->mm->mmap = NULL;
	task->mm->mmap_avl = NULL;
	while (mpnt) {
		struct vm_area_struct * next = mpnt->vm_next;
		if (mpnt->vm_ops && mpnt->vm_ops->close)
			mpnt->vm_ops->close(mpnt);
		remove_shared_vm_struct(mpnt);
		if (mpnt->vm_inode)
			iput(mpnt->vm_inode);
		kfree(mpnt);
		mpnt = next;
	}
}

/*
 * Insert vm structure into process list sorted by address
 * and into the inode's i_mmap ring.
 */
void insert_vm_struct(struct task_struct *t, struct vm_area_struct *vmp)
{
	struct vm_area_struct *share;
	struct inode * inode;

#if 0 /* equivalent, but slow */
	struct vm_area_struct **p, *mpnt;

	p = &t->mm->mmap;
	while ((mpnt = *p) != NULL) {
		if (mpnt->vm_start > vmp->vm_start)
			break;
		if (mpnt->vm_end > vmp->vm_start)
			printk("insert_vm_struct: overlapping memory areas\n");
		p = &mpnt->vm_next;
	}
	vmp->vm_next = mpnt;
	*p = vmp;
#else
	struct vm_area_struct * prev, * next;

	avl_insert_neighbours(vmp, &t->mm->mmap_avl, &prev, &next);
	if ((prev ? prev->vm_next : t->mm->mmap) != next)
		printk("insert_vm_struct: tree inconsistent with list\n");
	if (prev)
		prev->vm_next = vmp;
	else
		t->mm->mmap = vmp;
	vmp->vm_next = next;
#endif

	inode = vmp->vm_inode;
	if (!inode)
		return;

	/* insert vmp into inode's circular share list */
	if ((share = inode->i_mmap)) {
		vmp->vm_next_share = share->vm_next_share;
		vmp->vm_next_share->vm_prev_share = vmp;
		share->vm_next_share = vmp;
		vmp->vm_prev_share = share;
	} else
		inode->i_mmap = vmp->vm_next_share = vmp->vm_prev_share = vmp;
}

/*
 * Remove one vm structure from the inode's i_mmap ring.
 */
void remove_shared_vm_struct(struct vm_area_struct *mpnt)
{
	struct inode * inode = mpnt->vm_inode;

	if (!inode)
		return;

	if (mpnt->vm_next_share == mpnt) {
		if (inode->i_mmap != mpnt)
			printk("Inode i_mmap ring corrupted\n");
		inode->i_mmap = NULL;
		return;
	}

	if (inode->i_mmap == mpnt)
		inode->i_mmap = mpnt->vm_next_share;

	mpnt->vm_prev_share->vm_next_share = mpnt->vm_next_share;
	mpnt->vm_next_share->vm_prev_share = mpnt->vm_prev_share;
}

/*
 * Merge the list of memory segments if possible.
 * Redundant vm_area_structs are freed.
 * This assumes that the list is ordered by address.
 * We don't need to traverse the entire list, only those segments
 * which intersect or are adjacent to a given interval.
 */
 /*
	经过对进程虚拟空间不断的映射，在进程中的vma块有许多是可以合并的，为了提高avl树查找的效率，减少avl树中不必要的vma块，
	通常需要将这些块和并，merge_segments的功能为合并虚拟空间中从start_addr到end_addr中类型相同，首尾相连的vma块。由于只
	有经过增加操作才有可能合并，所有merge_segments只在do_mmap和unmap_fixup中被调用。该函数的流程如下：
	根据起始地址start_addr从找到第一块满足vm_end>start_addr的vma块mpnt。
	调用avl_neighbours找到在vma双向链表上与mpnt前后相连的vma块prev和next。
	如果prev和mpnt首尾相连，且有同样在swap file中的节点，同样的标志，同样的操作等则将其合并。
	调用avl_remove将mpnt从avl树中删除，调整prev的结束地址和后序指针。
	将结构mpnt所占的物理空间删除。prev、mpnt、next依次下移
 */
void merge_segments (struct task_struct * task, unsigned long start_addr, unsigned long end_addr)
{
	struct vm_area_struct *prev, *mpnt, *next;

	mpnt = find_vma(task, start_addr);
	if (!mpnt)
		return;
	avl_neighbours(mpnt, task->mm->mmap_avl, &prev, &next);
	/* we have  prev->vm_next == mpnt && mpnt->vm_next = next */

	if (!prev) {
		prev = mpnt;
		mpnt = next;
	}

	/* prev and mpnt cycle through the list, as long as
	 * start_addr < mpnt->vm_end && prev->vm_start < end_addr
	 */
	for ( ; mpnt && prev->vm_start < end_addr ; prev = mpnt, mpnt = next) {
#if 0
		printk("looping in merge_segments, mpnt=0x%lX\n", (unsigned long) mpnt);
#endif

		next = mpnt->vm_next;

		/*
		 * To share, we must have the same inode, operations.. 
		 */
		if (mpnt->vm_inode != prev->vm_inode)
			continue;
		if (mpnt->vm_pte != prev->vm_pte)
			continue;
		if (mpnt->vm_ops != prev->vm_ops)
			continue;
		if (mpnt->vm_flags != prev->vm_flags)
			continue;
		if (prev->vm_end != mpnt->vm_start)
			continue;
		/*
		 * and if we have an inode, the offsets must be contiguous..
		 */
		if ((mpnt->vm_inode != NULL) || (mpnt->vm_flags & VM_SHM)) {
			if (prev->vm_offset + prev->vm_end - prev->vm_start != mpnt->vm_offset)
				continue;
		}

		/*
		 * merge prev with mpnt and set up pointers so the new
		 * big segment can possibly merge with the next one.
		 * The old unused mpnt is freed.
		 */
		avl_remove(mpnt, &task->mm->mmap_avl);
		prev->vm_end = mpnt->vm_end;
		prev->vm_next = mpnt->vm_next;
		if (mpnt->vm_ops && mpnt->vm_ops->close) {
			mpnt->vm_offset += mpnt->vm_end - mpnt->vm_start;
			mpnt->vm_start = mpnt->vm_end;
			mpnt->vm_ops->close(mpnt);
		}
		remove_shared_vm_struct(mpnt);
		if (mpnt->vm_inode)
			mpnt->vm_inode->i_count--;
		kfree_s(mpnt, sizeof(*mpnt));
		mpnt = prev;
	}
}

/*
 * Map memory not associated with any file into a process
 * address space.  Adjacent（临近的） memory is merged.
 */
static int anon_map(struct inode *ino, struct file * file, struct vm_area_struct * vma)
{
	if (zeromap_page_range(vma->vm_start, vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -ENOMEM;
	return 0;
}
