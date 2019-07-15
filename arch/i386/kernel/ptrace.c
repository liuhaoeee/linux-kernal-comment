#define THREE_LEVEL
/* ptrace.c */
/* By Ross Biro 1/23/92 */
/* edited by Linus Torvalds */

#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/debugreg.h>

#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/system.h>
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
	ptrace提供了一种使父进程得以监视和控制其它进程的方式，它还能够改变子进程中的寄存
	器和内核映像，因而可以实现断点调试和系统调用的跟踪。ptrace会在什么时候出现呢？在
	执行系统调用之前，内核会先检查当前进程是否处于被“跟踪”(traced)的状态。如果是的话，
	内核暂停当前进程并将控制权交给跟踪进程，使跟踪进程得以察看或者修改被跟踪进程的寄存器。
*/

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which flags the user has access to. */
/* 1 = access 0 = no access */
#define FLAG_MASK 0x00044dd5

/* set's the trap flag. */
#define TRAP_FLAG 0x100

/*
 * this is the number to subtract from the top of the stack. To find
 * the local frame.
 */
#define MAGICNUMBER 68

/* change a pid into a task struct. */
static inline struct task_struct * get_task(int pid)
{
	int i;

	for (i = 1; i < NR_TASKS; i++) {
		if (task[i] != NULL && (task[i]->pid == pid))
			return task[i];
	}
	return NULL;
}

//函数get_stack_long()和put_stack_long()为对子进程核心堆栈的操作。
//当进程系统调用或时钟中断处理时，系统会把所有的用户态的寄存器压入
//堆栈保存，而处理完毕之后恢复寄存器的值。这是两个函数的理论和现实基础

/*
 * this routine will get a word off of the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */   
static inline int get_stack_long(struct task_struct *task, int offset)
{
	unsigned char *stack;

	stack = (unsigned char *)task->tss.esp0;	/*  获得ESP0寄存器值  */
	stack += offset;	/*  加偏移量  */
	return (*((int *)stack));
}

/*
 * this routine will put a word on the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline int put_stack_long(struct task_struct *task, int offset,
	unsigned long data)
{
	unsigned char * stack;

	stack = (unsigned char *) task->tss.esp0;
	stack += offset;
	*(unsigned long *) stack = data;
	return 0;
}

/*
	在sys_ptrace函数中对用户空间的访问通过辅助函数write_long()和read_long()函数完成的。
	访问进程空间的内存是通过调用Linux的分页管理机制完成的。从要访问进程的task结构中读出
	对进程内存的描述mm结构，并依次按页目录、中间页目录、页表的顺序查找到物理页，并进行读
	写操作。函数put_long()和get_long()完成的是对一个页内数据的读写操作。而write_long()
	和read_long()函数考虑了，所要访问的数据在两页之间，这时则需对两页分别调用put_long()
	和get_long()函数完成其功能。
*/

//函数put_long()和get_long()完成的是对一个页内数据的读写操作

/*
 * This routine gets a long from any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 */
static unsigned long get_long(struct vm_area_struct * vma, unsigned long addr)
{
	pgd_t * pgdir;
	pmd_t * pgmiddle;
	pte_t * pgtable;
	unsigned long page;

repeat:
	pgdir = pgd_offset(vma->vm_task, addr);
	if (pgd_none(*pgdir)) {
		do_no_page(vma, addr, 0);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return 0;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle)) {
		do_no_page(vma, addr, 0);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %08lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return 0;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		do_no_page(vma, addr, 0);
		goto repeat;
	}
	page = pte_page(*pgtable);
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (page >= high_memory)
		return 0;
	//定位要读取的内存地址，page指向的是物理页的首地址，而addr & ~PAGE_MASK是页内偏移量
	page += addr & ~PAGE_MASK;
	//返回其内容
	return *(unsigned long *) page;
}

/*
 * This routine puts a long into any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 *
 * Now keeps R/W state of page so that a text page stays readonly
 * even if a debugger scribbles breakpoints into it.  -M.U-
 */
static void put_long(struct vm_area_struct * vma, unsigned long addr,
	unsigned long data)
{
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pgtable;
	unsigned long page;

repeat:
	pgdir = pgd_offset(vma->vm_task, addr);
	if (!pgd_present(*pgdir)) {
		do_no_page(vma, addr, 1);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle)) {
		do_no_page(vma, addr, 1);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %08lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		do_no_page(vma, addr, 1);
		goto repeat;
	}
	page = pte_page(*pgtable);
	if (!pte_write(*pgtable)) {
		do_wp_page(vma, addr, 1);
		goto repeat;
	}
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (page < high_memory) {
		page += addr & ~PAGE_MASK;
		*(unsigned long *) page = data;
	}
/* we're bypassing pagetables, so we have to set the dirty bit ourselves */
/* this should also re-instate whatever read-only mode there was before */
	*pgtable = pte_mkdirty(mk_pte(page, vma->vm_page_prot));
	invalidate();
}

static struct vm_area_struct * find_extend_vma(struct task_struct * tsk, unsigned long addr)
{
	struct vm_area_struct * vma;

	addr &= PAGE_MASK;
	vma = find_vma(tsk,addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		return NULL;
	if (vma->vm_end - addr > tsk->rlim[RLIMIT_STACK].rlim_cur)
		return NULL;
	vma->vm_offset -= vma->vm_start - addr;
	vma->vm_start = addr;
	return vma;
}

//函数put_long()和get_long()完成的是对一个页内数据的读写操作。而write_long()和read_long()函数考虑了，
//所要访问的数据在两页之间，这时则需对两页分别调用put_long()和get_long()函数完成其功能。

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls get_long() to read a long.
 */
static int read_long(struct task_struct * tsk, unsigned long addr,
	unsigned long * result)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	//如果要读取的long字节要跨越两个物理页面
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_high = vma;

		//如果还跨越了两个虚拟地址vma
		if (addr + sizeof(long) >= vma->vm_end) {
			//找到下一个相邻的虚拟地址空间
			vma_high = vma->vm_next;
			if (!vma_high || vma_high->vm_start != vma->vm_end)
				return -EIO;
		}
		//读取第一个页面的字节
		low = get_long(vma, addr & ~(sizeof(long)-1));
		//读取第二个页面的字节
		high = get_long(vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1));
		//将俩个页面读取的字节组合成一个long
		switch (addr & (sizeof(long)-1)) {
			case 1:
				low >>= 8;
				low |= high << 24;
				break;
			case 2:
				low >>= 16;
				low |= high << 16;
				break;
			case 3:
				low >>= 24;
				low |= high << 8;
				break;
		}
		*result = low;
	//否则，读取的long在同一个物理页面内
	} else
		*result = get_long(vma, addr);
	return 0;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls put_long() to write a long.
 */
static int write_long(struct task_struct * tsk, unsigned long addr,
	unsigned long data)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_high = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_high = vma->vm_next;
			if (!vma_high || vma_high->vm_start != vma->vm_end)
				return -EIO;
		}
		low = get_long(vma, addr & ~(sizeof(long)-1));
		high = get_long(vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 0: /* shouldn't happen, but safety first */
				low = data;
				break;
			case 1:
				low &= 0x000000ff;
				low |= data << 8;
				high &= ~0xff;
				high |= data >> 24;
				break;
			case 2:
				low &= 0x0000ffff;
				low |= data << 16;
				high &= ~0xffff;
				high |= data >> 16;
				break;
			case 3:
				low &= 0x00ffffff;
				low |= data << 24;
				high &= ~0xffffff;
				high |= data >> 8;
				break;
		}
		put_long(vma, addr & ~(sizeof(long)-1),low);
		put_long(vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1),high);
	} else
		put_long(vma, addr, data);
	return 0;
}

/*
	当一个进程调用了 ptrace( PTRACE_TRACEME, …)之后，内核为该进程设置了一个标记，注明该进程将被跟踪。
	一次系统调用完成之后，内核察看那个标记，然后执行trace系统调用（如果这个进程正处于被跟踪状态的话）。
	其汇编的细节可以在 arh/i386/kernel/entry.S中找到。
	现在让我们来看看这个sys_trace()函数（位于 arch/i386/kernel/ptrace.c ）。它停止子进程，然后发送一
	个信号给父进程，告诉它子进程已经停滞，这个信号会激活正处于等待状态的父进程，让父进程进行相关处理。
	父进程在完成相关操作以后就调用ptrace( PTRACE_CONT, …)或者 ptrace( PTRACE_SYSCALL, …), 这将唤醒子
	进程，内核此时所作的是调用一个叫wake_up_process() 的进程调度函数。其他的一些系统架构可能会通过发送
	SIGCHLD给子进程来达到这个目的。
*/

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	struct user * dummy;
	int i;

	dummy = NULL;

	//如果调用进程请求被跟踪
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		//如果其已经处于被跟踪状态
		if (current->flags & PF_PTRACED)
			return -EPERM;
		/* set the ptrace bit in the process flags. */
		//将其设置为被跟踪状态
		current->flags |= PF_PTRACED;
		return 0;
	}
	//1号进程，及init进程不能被跟踪
	if (pid == 1)		/* you may not mess with init */
		return -EPERM;
	//获取被跟踪进程的task结构
	if (!(child = get_task(pid)))
		return -ESRCH;
	//如果请求跟踪指定pid进程
	if (request == PTRACE_ATTACH) {
		//如果请求被跟踪的是进程是自己
		if (child == current)
			return -EPERM;
		//对进程pid进程跟踪所需要满足的条件
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
	 	    (current->gid != child->gid)) && !suser())
			return -EPERM;
		/* the same process cannot be attached many times */
		//如果被跟踪的进程没有要求被跟踪，则不能对其进行跟踪
		if (child->flags & PF_PTRACED)
			return -EPERM;
		child->flags |= PF_PTRACED;
		//如果被跟踪进程不是当前进程的子进程
		if (child->p_pptr != current) {
			//将其置为本进程的子进程
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		//向子进程发送SIGSTOP信号，让其暂停执行
		send_sig(SIGSTOP, child, 1);
		return 0;
	}

	//执行到这里，说明当前进程已经跟踪了某个进程，而希望通过本系统调用
	//对被跟踪进程做一些操作（此时被跟踪进程应该处于暂停状态）

	//检验，只有通过了检验，才可以对被跟踪进程进行操作
	if (!(child->flags & PF_PTRACED))
		return -ESRCH;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			return -ESRCH;
	}
	if (child->p_pptr != current)
		return -ESRCH;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			//从内存地址中读取一个字节
			unsigned long tmp;
			int res;

			res = read_long(child, addr, &tmp);
			if (res < 0)
				return res;
			res = verify_area(VERIFY_WRITE, (void *) data, sizeof(long));
			if (!res)
				put_fs_long(tmp,(unsigned long *) data);
			return res;
		}

	/* read the word at location addr in the USER area. */
		//通过将PTRACE_PEEKUSER作为ptrace 的第一个参数进行调用，可以取得与子进程相关的寄存器值。
		case PTRACE_PEEKUSR: {
			unsigned long tmp;
			int res;

			//struct user定义于linux/include/linux/user.h
			if ((addr & 3) || addr < 0 || 
			    addr > sizeof(struct user) - 3)
				return -EIO;

			res = verify_area(VERIFY_WRITE, (void *) data, sizeof(long));
			if (res)
				return res;
			tmp = 0;  /* Default return condition */
			if(addr < 17*sizeof(long)) {
			 //读取17个寄存器之一 因为一个寄存器占4个字节，所以需要除4
			  addr = addr >> 2; /* temporary hack. */

			  //从进程系统堆栈中读取指定的寄存器值
			  tmp = get_stack_long(child, sizeof(long)*addr - MAGICNUMBER);
			  if (addr == DS || addr == ES ||
			      addr == FS || addr == GS ||
			      addr == CS || addr == SS)
			    tmp &= 0xffff;
			};
/*
	断点
	设置断点是调试器中的一个重要功能。80386提供了两种方式，INT3和利用调试寄存器（详见前面80386的调试设施）。
	如果使用INT3方式设置断点，则调试器通过ptrace的PTRACE_POKETEXT功能在断点处插入INT3单字节指令。当进程运行
	到断点时（INT3处），则系统进入异常3的处理。
	若使用调试寄存器，则调试器通过调用ptrace(PTRACE_POKEUSR，pid，0，data)在DR0-DR3寄存器设置与四个断点条件
	的每一个相联系的线性地址在DR7中设置断点条件。被跟踪进程运行到断点处时，CPU产生异常 1，从而转至函数do_debug
	处理。由于子进程在调试状态下属于正常调试异常，所以do_debug函数处理中产生SIGTRAP信号，为处理这个信号，进入
	do_signal，使被调试进程停止，并通知调试器（父进程），此时得到子进程终止原因为SIGTRAP。

*/
			if(addr >= (long) &dummy->u_debugreg[0] &&
			   addr <= (long) &dummy->u_debugreg[7]){
				addr -= (long) &dummy->u_debugreg[0];
				addr = addr >> 2;
				//取相应的调试寄存器的值
				tmp = child->debugreg[addr];
			};
			put_fs_long(tmp,(unsigned long *) data);
			return 0;
		}

      /* when I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			return write_long(child,addr,data);

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			if ((addr & 3) || addr < 0 || 
			    addr > sizeof(struct user) - 3)
				return -EIO;

			addr = addr >> 2; /* temporary hack. */

			//ORIG_EAX不可写
			if (addr == ORIG_EAX)
				return -EIO;
			if (addr == DS || addr == ES ||
			    addr == FS || addr == GS ||
			    addr == CS || addr == SS) {
			    	data &= 0xffff;
			    	if (data && (data & 3) != 3)
					return -EIO;
			}
			if (addr == EFL) {   /* flags. */
				data &= FLAG_MASK;
				data |= get_stack_long(child, EFL*sizeof(long)-MAGICNUMBER)  & ~FLAG_MASK;
			}
		  /* Do not allow the user to set the debug register for kernel
		     address space */
		  if(addr < 17){
			  if (put_stack_long(child, sizeof(long)*addr-MAGICNUMBER, data))
				return -EIO;
			return 0;
			};

		  /* We need to be very careful here.  We implicitly
		     want to modify a portion of the task_struct, and we
		     have to be selective about what portions we allow someone
		     to modify. */

		  addr = addr << 2;  /* Convert back again */
		  if(addr >= (long) &dummy->u_debugreg[0] &&
		     addr <= (long) &dummy->u_debugreg[7]){

			  if(addr == (long) &dummy->u_debugreg[4]) return -EIO;
			  if(addr == (long) &dummy->u_debugreg[5]) return -EIO;
			  if(addr < (long) &dummy->u_debugreg[4] &&
			     ((unsigned long) data) >= 0xbffffffd) return -EIO;
			  
			  if(addr == (long) &dummy->u_debugreg[7]) {
				  data &= ~DR_CONTROL_RESERVED;
				  for(i=0; i<4; i++)
					  if ((0x5f54 >> ((data >> (16 + 4*i)) & 0xf)) & 1)
						  return -EIO;
			  };

			  addr -= (long) &dummy->u_debugreg;
			  addr = addr >> 2;
			  child->debugreg[addr] = data;
			  return 0;
		  };
		  return -EIO;

		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			long tmp;

			if ((unsigned long) data > NSIG)
				return -EIO;
			if (request == PTRACE_SYSCALL)
				child->flags |= PF_TRACESYS;
			else
				child->flags &= ~PF_TRACESYS;
			child->exit_code = data;
			child->state = TASK_RUNNING;
	/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, sizeof(long)*EFL-MAGICNUMBER) & ~TRAP_FLAG;
			put_stack_long(child, sizeof(long)*EFL-MAGICNUMBER,tmp);
			return 0;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL: {
			long tmp;

			child->state = TASK_RUNNING;
			child->exit_code = SIGKILL;
	/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, sizeof(long)*EFL-MAGICNUMBER) & ~TRAP_FLAG;
			put_stack_long(child, sizeof(long)*EFL-MAGICNUMBER,tmp);
			return 0;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			long tmp;

			if ((unsigned long) data > NSIG)
				return -EIO;
			child->flags &= ~PF_TRACESYS;
			tmp = get_stack_long(child, sizeof(long)*EFL-MAGICNUMBER) | TRAP_FLAG;
			put_stack_long(child, sizeof(long)*EFL-MAGICNUMBER,tmp);
			child->state = TASK_RUNNING;
			child->exit_code = data;
	/* give it a chance to run. */
			return 0;
		}

		case PTRACE_DETACH: { /* detach a process that was attached. */
			long tmp;

			if ((unsigned long) data > NSIG)
				return -EIO;
			child->flags &= ~(PF_PTRACED|PF_TRACESYS);
			child->state = TASK_RUNNING;
			child->exit_code = data;
			REMOVE_LINKS(child);
			child->p_pptr = child->p_opptr;
			SET_LINKS(child);
			/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, sizeof(long)*EFL-MAGICNUMBER) & ~TRAP_FLAG;
			put_stack_long(child, sizeof(long)*EFL-MAGICNUMBER,tmp);
			return 0;
		}

		default:
			return -EIO;
	}
}

asmlinkage void syscall_trace(void)
{
	if ((current->flags & (PF_PTRACED|PF_TRACESYS))
			!= (PF_PTRACED|PF_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
	notify_parent(current);
	schedule();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code)
		current->signal |= (1 << (current->exit_code - 1));
	current->exit_code = 0;
}
