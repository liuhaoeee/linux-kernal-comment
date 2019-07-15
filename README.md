# linux-kernal-comment
对Linux 1.2内核的注释注解

此源代码是Linux1.2内核源代码，当然了，是我上大学期间（2012-2016）根据各种资料和书籍以及自己的理解增加了对代码的注释，最近突然想起来自己还干过这么一件事，就把代码上传到Git了，欢迎大家Start。

当然了，现成的相关数据也有很多，比如《Linux内核源代码完全注释》，《Linux内核源代码情景分析》等等。


```
/*
						关于high_memory
	每个进程都有自己的私有用户空间（0-3GB），这个空间对系统中的其他进程是不可见的。最高的1GB内核空间
	则由则由所有进程以及内核共享。可见，内核最多寻址1G的虚拟地址空间。Linux 内核采用了最简单的映射方
	式来映射物理内存，即把物理地址＋PAGE_OFFSET按照线性关系直接映射到内核空间。PAGE_OFFSET大小为0xC000000.
	但是linux内核并没有把整个1G空间用于线性映射，而只映射了最多896M物理内存，预留了最高端的128M虚拟地址空间
	给IO设备和其他用途。所以，当系统物理内存较大时，超过896M的内存区域，内核就无法直接通过线性映射直接访问了。
	这部分内存被称作high memory。相应的可以映射的低端物理内存称为Low memory.而对于2G/2G划分的arm机器，这个线
	性映射空间就可能达到1G以上，能够直接映射到这个线性空间的物理地址是DMA zone和Normal Zone，在这个范围之外的
	物理内存则划归High memory Zone。
结论：
	1）high memory针对的是物理内存，不是虚拟内存。 
	2）high memory也是被内核管理的（有对应的page结构），只是没有映射到内核虚拟地址空间。
	  当内核需要分配high memory时，通过kmap等从预留的地址空间中动态分配一个地址，然后映射
	  到high memory，从而访问这个物理页。high memory映射到内核地址空间一般是暂时性的映射，
	  不是永久映射。 
	3）high memory和low memory一样，都是参与内核的物理内存分配，都可以被映射到内核地址空间，
	   也都可以被映射到用户地址空间。 
	4）物理内存<896M时，没有high memory，因为所有的内存都被kernel直接映射了。 
	5）64位系统下不会有high memory，因为64位虚拟地址空间非常大（分给kernel的也很大），
	   完全能够直接映射全部物理内存。
*/

unsigned long high_memory = 0;

/*
 * The free_area_list arrays point to the queue heads of the free areas
 * of different sizes
 */
int nr_swap_pages = 0;
int nr_free_pages = 0;
struct mem_list free_area_list[NR_MEM_LISTS];
unsigned char * free_area_map[NR_MEM_LISTS];

#define copy_page(from,to) memcpy((void *) to, (void *) from, PAGE_SIZE)

#define USER_PTRS_PER_PGD (TASK_SIZE / PGDIR_SIZE)

mem_map_t * mem_map = NULL;

/*
 * oom() prints a message (so that the user knows why the process died),
 * and gives the process an untrappable SIGKILL.
 */
void oom(struct task_struct * task)
{
	printk("\nOut of memory for %s.\n", current->comm);
	task->sigaction[SIGKILL-1].sa_handler = NULL;
	task->blocked &= ~(1<<(SIGKILL-1));
	send_sig(SIGKILL,task,1);
}

//释放一项页表所指向的物理内存页面
static inline void free_one_pte(pte_t * page_table)
{
	pte_t page = *page_table;

	if (pte_none(page))
		return;
	pte_clear(page_table);
	//如果此物理页面不在内存中，则调用swap_free，否则调用free_page直接释放此物理页面
	if (!pte_present(page)) {
		swap_free(pte_val(page));
		return;
	}
	free_page(pte_page(page));
	return;
}

//释放一项二级页表所指向的所有物理内存页面
static inline void free_one_pmd(pmd_t * dir)
{
	int j;
	pte_t * pte;

	if (pmd_none(*dir))
		return;
	if (pmd_bad(*dir)) {
		printk("free_one_pmd: bad directory entry %08lx\n", pmd_val(*dir));
		pmd_clear(dir);
		return;
	}
	pte = pte_offset(dir, 0);	//得到三级页表所在物理页面首指针
	pmd_clear(dir);
	//如果此pte页面是被共享的，则调用pte_free释放，减少页面引用次数
	if (pte_inuse(pte)) {
		pte_free(pte);
		return;
	}
	//程序执行到这里，说明三级页表是由程序独享的，则调用free_one_pte一项一项的释放页表中每个指针所指向的物理页面
	for (j = 0; j < PTRS_PER_PTE ; j++)
		free_one_pte(pte+j);
	pte_free(pte);
}

```
