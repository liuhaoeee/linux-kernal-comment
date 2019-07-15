#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

/*
 * define DEBUG if you want the wait-queues to have some extra
 * debugging code. It's not normally used, but might catch some
 * wait-queue coding errors.
 *
 *  #define DEBUG
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

#include <asm/param.h>	/* for HZ */

extern unsigned long intr_count;
extern unsigned long event;

#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/tasks.h>
#include <linux/kernel.h>
#include <asm/system.h>

/*
 * These are the constant used to fake the fixed-point load-average
 * counting. Some notes:
 *  - 11 bit fractions expand to 22 bits by the multiplies: this gives
 *    a load-average precision of 10 bits integer + 11 bits fractional
 *  - if you want to count load-averages more often, you need more
 *    precision, or rounding will get you. With 2-second counting freq,
 *    the EXP_n values would be 1981, 2034 and 2043 if still using only
 *    11 bit fractions.
 */
extern unsigned long avenrun[];		/* Load averages */

#define FSHIFT		11		/* nr of bits of precision */
#define FIXED_1		(1<<FSHIFT)	/* 1.0 as fixed-point */
#define LOAD_FREQ	(5*HZ)		/* 5 sec intervals */
#define EXP_1		1884		/* 1/exp(5sec/1min) as fixed-point */
#define EXP_5		2014		/* 1/exp(5sec/5min) */
#define EXP_15		2037		/* 1/exp(5sec/15min) */
/*
	为 了使内核可以高效计算load average，采用了fixed-point arithmetic。
	fixed-point arithmetic是一种非常快速的模拟浮点运算的方法，特别是在
	没有FPU（float point unit）部件的处理器上，非常有用。 
	计算公式：load(t) = load(t-1) e^(-5/60) + n (1 - e^(-5/60))，迭代计算，其中n为run-queue length。 
	为什么采用这个计算公式呢？ 
	由Exponential Smoothing方程有，Y(t)＝ Y(t-1) + a*[X(t) - Y(t-1)],whereX(t) is the input raw data,
	Y(t - 1) is the value due to the previoussmoothing iteration and Y(t) is the new smoothed value.
	这个公式就当作公理吧，不要问我为什么，我也不知道。 
	令a＝1-b，b为e^(-5/60)，就可以得到load average的计算公式 
	采用此公式的好处：局部的load抖动不会对load average造成重大影响，使其平滑。
*/
#define CALC_LOAD(load,exp,n) \
	load *= exp; \
	load += n*(FIXED_1-exp); \
	load >>= FSHIFT;

#define CT_TO_SECS(x)	((x) / HZ)
#define CT_TO_USECS(x)	(((x) % HZ) * 1000000/HZ)

#define FIRST_TASK task[0]
#define LAST_TASK task[NR_TASKS-1]

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/signal.h>
#include <linux/time.h>
#include <linux/param.h>
#include <linux/resource.h>
#include <linux/vm86.h>
#include <linux/math_emu.h>
#include <linux/ptrace.h>

#include <asm/processor.h>

#define TASK_RUNNING		0
#define TASK_INTERRUPTIBLE	1
#define TASK_UNINTERRUPTIBLE	2
#define TASK_ZOMBIE		3
#define TASK_STOPPED		4
#define TASK_SWAPPING		5

#ifndef NULL
#define NULL ((void *) 0)
#endif

#ifdef __KERNEL__

extern void sched_init(void);
extern void show_state(void);
extern void trap_init(void);

asmlinkage void schedule(void);

#endif /* __KERNEL__ */

struct files_struct {
	int count;
	fd_set close_on_exec;
	struct file * fd[NR_OPEN];
};

#define INIT_FILES { \
	0, \
	{ { 0, } }, \
	{ NULL, } \
}

struct fs_struct {
	int count;
	unsigned short umask;
	struct inode * root, * pwd;
};

#define INIT_FS { \
	0, \
	0022, \
	NULL, NULL \
}

//每一个进程，用一个mm_struct结构体来定义它的虚存用户区
/*一个虚存区域是虚存空间中一个连续的区域，在这个区域中的信息具有相同的操作和访问特性。
*每个虚拟区域用一个vm_area_struct结构体进行描述.它定义在/include/linux/mm.h
*/
struct mm_struct {
	int count;
	unsigned long start_code, end_code, end_data;	//分别为代码段、数据段的首地址和终止地址
	unsigned long start_brk, brk, start_stack, start_mmap;	//进程堆栈的首地址
	unsigned long arg_start, arg_end, env_start, env_end;	// 分别为参数区、环境变量区的首地址和终止地址
	unsigned long rss;		//将进程内容驻留在物理内存的页面总数 rss:resident set(驻内页面集合)
	unsigned long min_flt, maj_flt, cmin_flt, cmaj_flt;
	int swappable:1;
	unsigned long swap_address;
	//以下三个成员，参见linux/mm/swap/swap_out()函数
	//这三个成员只为swap_out服务
	unsigned long old_maj_flt;	/* old value of maj_flt */
	unsigned long dec_flt;		/* page fault count of the last time */
	unsigned long swap_cnt;		/* number of pages to swap on next pass */
	struct vm_area_struct * mmap;	//所有vm_area_struct结构体链接成一个单向链表，vm_next指向下一个
									//vm_area_struct结构体。链表的首地址由mm_struct中成员项mmap指出
	struct vm_area_struct * mmap_avl;
};

#define INIT_MM { \
		0, \
		0, 0, 0, \
		0, 0, 0, 0, \
		0, 0, 0, 0, \
		0, \
/* ?_flt */	0, 0, 0, 0, \
		0, \
/* swap */	0, 0, 0, 0, \
		&init_mmap, &init_mmap }

struct task_struct {
/* these are hardcoded - don't touch */
	volatile long state;	/* -1 unrunnable, 0 runnable, >0 stopped */
	long counter;
	long priority;
	unsigned long signal;
	unsigned long blocked;	/* bitmap of masked signals */
	unsigned long flags;	/* per process flags, defined below */
	//最后一次出错的系统调用的错误号，0表示无错误。系统调用返回时，全程量也拥有该错误号。
	int errno;
	//保存INTEL CPU调试寄存器的值，在ptrace系统调用中使用。
	int debugreg[8];  /* Hardware debugging registers */
	//定义于linux/include/linux/personality.h中
	struct exec_domain *exec_domain;	//Linux可以运行由80386平台其它UNIX操作系统生成的符合iBCS2标准的程序。
						//关于此类程序与Linux程序差异的消息就由exec_domain结构保存
/* various fields */
/*
	指向进程所属的全局执行文件格式结构，共有a.out、script、elf和java等四种。
	结构定义在include/linux/binfmts.h中(core_dump、load_shlib(fd)、load_binary、use_count)。
*/
	struct linux_binfmt *binfmt;
	struct task_struct *next_task, *prev_task;
	struct sigaction sigaction[32];
	//为MS-DOS的仿真程序(或叫系统调用vm86)保存的堆栈指针
	unsigned long saved_kernel_stack;
	//在内核态运行时，每个进程都有一个内核堆栈，其基地址就保存在kernel_stack_page中
	unsigned long kernel_stack_page;
	//引起进程退出的返回代码exit_code，引起错误的信号名exit_signal。
	int exit_code, exit_signal;
/*
	Linux 可以运行由80386平台其它UNIX操作系统生成的符合iBCS2标准的程序。 
	Personality进一步描述进程执行的程序属于何种UNIX平台的“个性”信息。通常有
	PER_Linux（标准执行字段）、PER_Linux_32BIT、 PER_Linux_EM86、PER_SVR3、PER_SCOSVR3、
	PER_WYSEV386、PER_ISCR4、PER_BSD、 PER_XENIX和PER_MASK等，
	参见include/linux/personality.h
*/
	unsigned long personality;
	//布尔量，表示出错时是否可以进行memory dump。
	int dumpable:1;	//位域技术
	//按POSIX要求设计的布尔量，区分进程是正在执行父进程的程序代码，还是在执行execve装入的新代码。
	int did_exec:1; //位域技术
	//tty_old_pgrp:进程显示终端所在的组标识。
	//leader:表示进程是否为会话主管
	int pid,pgrp,tty_old_pgrp,session,leader;
	int	groups[NGROUPS];
	/* 
	 * pointers to (original) parent process, youngest child, younger sibling,
	 * older sibling, respectively.  (p->father can be replaced with 
	 * p->p_pptr->pid)
	 */
	struct task_struct *p_opptr, *p_pptr, *p_cptr, *p_ysptr, *p_osptr;
	//在进程结束时，或发出系统调用wait4后，为了等待子进程的结束，而将自己(父进程)睡眠在该队列上。结构定义在include/linux /wait.h中。
	struct wait_queue *wait_chldexit;	/* for wait4() */
	unsigned short uid,euid,suid,fsuid;
	unsigned short gid,egid,sgid,fsgid;
	unsigned long timeout;
	unsigned long it_real_value, it_prof_value, it_virt_value;
	unsigned long it_real_incr, it_prof_incr, it_virt_incr;
	long utime, stime, cutime, cstime, start_time;
/*
	结构rlimit用于资源管理，定义在linux/include/linux/resource.h中，成员共有两项:
	rlim_cur是资源的当前最大 数目;rlim_max是资源可有的最大数目。在i386环境中，受控
	资源共有RLIM_NLIMITS项，即10项，定义在 linux/include/asm/resource.h中
*/
	struct rlimit rlim[RLIM_NLIMITS];
	unsigned short used_math;
	char comm[16];	//comm是保存该进程名字的字符数组
/* file system info */
	int link_count;
	//指向进程所在的显示终端的信息。如果进程不需要显示终端，如0号进程，则该指针为空。结构定义在include/linux/tty.h中。
	struct tty_struct *tty; /* NULL if no tty */
/* ipc stuff */
	struct sem_undo *semundo;	//进程在信号灯上的所有undo操作 
	struct sem_queue *semsleeping;	//当进程因为信号灯操作而挂起时，他在该队列中记录等待的操作 
/* ldt for this task - used by Wine.  If NULL, default_ldt is used */
	//进程关于CPU段式存储管理的局部描述符表的指针，用于仿真WINE Windows的程序。其他情况下取值NULL，
	//进程的ldt就是arch/i386/traps.c定义的default_ldt。
	struct desc_struct *ldt;
/* tss for this task */
	//任务状态段，其内容与INTEL CPU的TSS对应，如各种通用寄存器.CPU调度时，当前运行进程的TSS保存到PCB的tss，
	//新选中进程的tss内容复制到CPU的TSS
	struct thread_struct tss;
/* filesystem information */
	//fs 保存了进程本身与VFS的关系消息，其中root指向根目录结点，pwd指向当前目录结点，
	//umask给出新建文件的访问模式(可由系统调用umask更 改)，count是Linux保留的属性
	struct fs_struct fs[1];
/* open file information */
	//files包含了进程当前所打开的文件(struct file *fd[NR_OPEN])。在Linux中，一个
	//进程最多只能同时打开NR_OPEN个文件。而且，前三项分别预先设置为标准输入、标准输出和出错消息输出文件。
	struct files_struct files[1];
/* memory management info */
/*
	在linux 中，采用按需分页的策略解决进程的内存需求。task_struct的数据成员mm指向关于存储管理的mm_struct结构。
	其中包含了一个虚存队列 mmap，指向由若干vm_area_struct描述的虚存块。同时，为了加快访问速度，mm中的mmap_avl
	维护了一个AVL树。在树中，所有的 vm_area_struct虚存块均由左指针指向相邻的低虚存块，右指针指向相邻的高虚存块。
*/
	struct mm_struct mm[1];
};

/*
 * Per process flags
 */
#define PF_ALIGNWARN	0x00000001	/* Print alignment warning msgs */
					/* Not implemented yet, only for 486*/
#define PF_PTRACED	0x00000010	/* set if ptrace (0) has been called. */
#define PF_TRACESYS	0x00000020	/* tracing system calls */

#define PF_STARTING	0x00000100	/* being created */
#define PF_EXITING	0x00000200	/* getting shut down */

/*
 * cloning flags:
 */
#define CSIGNAL		0x000000ff	/* signal mask to be sent at exit */
#define COPYVM		0x00000100	/* set if VM copy desired (like normal fork()) */
#define COPYFD		0x00000200	/* set if fd's should be copied, not shared (NI) */

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x1fffff (=2MB)
 */
#define INIT_TASK \
/* state etc */	{ 0,15,15,0,0,0,0, \
/* debugregs */ { 0, },            \
/* exec domain */&default_exec_domain, \
/* binfmt */	NULL, \
/* schedlink */	&init_task,&init_task, \
/* signals */	{{ 0, },}, \
/* stack */	0,(unsigned long) &init_kernel_stack, \
/* ec,brk... */	0,0,0,0,0, \
/* pid etc.. */	0,0,0,0,0, \
/* suppl grps*/ {NOGROUP,}, \
/* proc links*/ &init_task,&init_task,NULL,NULL,NULL,NULL, \
/* uid etc */	0,0,0,0,0,0,0,0, \
/* timeout */	0,0,0,0,0,0,0,0,0,0,0,0, \
/* rlimits */   { {LONG_MAX, LONG_MAX}, {LONG_MAX, LONG_MAX},  \
		  {LONG_MAX, LONG_MAX}, {LONG_MAX, LONG_MAX},  \
		  {       0, LONG_MAX}, {LONG_MAX, LONG_MAX}, \
		  {MAX_TASKS_PER_USER, MAX_TASKS_PER_USER}, {NR_OPEN, NR_OPEN}}, \
/* math */	0, \
/* comm */	"swapper", \
/* fs info */	0,NULL, \
/* ipc */	NULL, NULL, \
/* ldt */	NULL, \
/* tss */	INIT_TSS, \
/* fs */	{ INIT_FS }, \
/* files */	{ INIT_FILES }, \
/* mm */	{ INIT_MM } \
}

#ifdef __KERNEL__

extern struct task_struct init_task;
extern struct task_struct *task[NR_TASKS];
extern struct task_struct *last_task_used_math;
extern struct task_struct *current;
extern unsigned long volatile jiffies;
extern unsigned long itimer_ticks;
extern unsigned long itimer_next;
extern struct timeval xtime;
extern int need_resched;

#define CURRENT_TIME (xtime.tv_sec)

extern void sleep_on(struct wait_queue ** p);
extern void interruptible_sleep_on(struct wait_queue ** p);
extern void wake_up(struct wait_queue ** p);
extern void wake_up_interruptible(struct wait_queue ** p);

extern void notify_parent(struct task_struct * tsk);
extern int send_sig(unsigned long sig,struct task_struct * p,int priv);
extern int in_group_p(gid_t grp);

extern int request_irq(unsigned int irq,void (*handler)(int, struct pt_regs *),
	unsigned long flags, const char *device);
extern void free_irq(unsigned int irq);

extern void copy_thread(int, unsigned long, unsigned long, struct task_struct *, struct pt_regs *);
extern void flush_thread(void);
extern void exit_thread(void);

extern int do_execve(char *, char **, char **, struct pt_regs *);
extern int do_fork(unsigned long, unsigned long, struct pt_regs *);
asmlinkage int do_signal(unsigned long, struct pt_regs *);

/*
 * The wait-queues are circular lists, and you have to be *very* sure
 * to keep them correct. Use only these two functions to add/remove
 * entries in the queues.
 */
 //将一个wait节点加入到*p所指向的等待队列中
extern inline void add_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
	unsigned long flags;

#ifdef DEBUG
	if (wait->next) {
		unsigned long pc;
		__asm__ __volatile__("call 1f\n"
			"1:\tpopl %0":"=r" (pc));
		printk("add_wait_queue (%08x): wait->next = %08x\n",pc,(unsigned long) wait->next);
	}
#endif
	//保存标志寄存器
	save_flags(flags);
	//关中断
	cli();
	//如果当前*p所指向的等待队列为空
	if (!*p) {
		wait->next = wait;	//wait->next指向wait自身（以形成循环队列）
		*p = wait;	//p指向此wait
	} else {	//当前队列不为空
		//将wait->next指向队列尾wait节点
		wait->next = (*p)->next;
		//将wait作为新的队列尾节点
		//即，*p永远指向第一个加入到队列中的wait节点，
		//(*p)->next永远指向最后一个加入到队列中的wait节点
		//wait->next永远指比其先加入到队列中的前一个wait节点
		(*p)->next = wait;
	}
	//恢复标志寄存器（隐含了开中断）
	restore_flags(flags);
}

//将一个wait节点从*p所指向的等待队列中移除
extern inline void remove_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
	unsigned long flags;
	struct wait_queue * tmp;
#ifdef DEBUG
	unsigned long ok = 0;
#endif

	save_flags(flags);
	cli();
	//如果wait指向的是队列头
	if ((*p == wait) &&
#ifdef DEBUG
	    (ok = 1) &&
#endif
		//并且队列中只有此wait一个节点
	    ((*p = wait->next) == wait)) {
		//将队列指针指向NULL
		*p = NULL;
	} else {	//否则，遍历队列中的元素节点，找到要移除的节点的前一个节点
		tmp = wait;
		while (tmp->next != wait) {
			tmp = tmp->next;
#ifdef DEBUG
			if (tmp == *p)
				ok = 1;
#endif
		}
		//将要移除的节点的前一个节点的next值指向要移除的节点的next值，
		//即可以将此节点从队列中移除
		tmp->next = wait->next;
	}
	wait->next = NULL;
	restore_flags(flags);
#ifdef DEBUG
	if (!ok) {
		printk("removed wait_queue not on list.\n");
		printk("list = %08x, queue = %08x\n",(unsigned long) p, (unsigned long) wait);
		__asm__("call 1f\n1:\tpopl %0":"=r" (ok));
		printk("eip = %08x\n",ok);
	}
#endif
}

//将调用进程加到相应select_wait队列中
extern inline void select_wait(struct wait_queue ** wait_address, select_table * p)
{
	//select_table_entry结构中包含一个wait_queue类型的变量
	//和此wait_queue变量所属的睡眠队列指针的指针
	struct select_table_entry * entry;	

	//p指向一个select_table，一个select_table表示一个物理页面
	//此物理页面被select_table_entry结构所填充
	//如果p为空，或者wait_address为空（没有指出睡眠队列）
	if (!p || !wait_address)
		return;
	//p->nr代表了当前select_table页面中有效的select_table_entry数
	if (p->nr >= __MAX_SELECT_TABLE_ENTRIES)
		return;
 	entry = p->entry + p->nr;
	entry->wait_address = wait_address;	//entry中wait节点变量所属的等待队列
	entry->wait.task = current;	//entry中wait节点变量所代表的睡眠进程
	entry->wait.next = NULL;
	//将entry->wait睡眠节点加入到wait_address睡眠队列
	add_wait_queue(wait_address,&entry->wait);
	//增加p所指向的select_table中有效select_table_entry数
	p->nr++;
}

extern void __down(struct semaphore * sem);

/*
 * These are not yet interrupt-safe
 */
extern inline void down(struct semaphore * sem)
{
	if (sem->count <= 0)
		__down(sem);
	sem->count--;
}

extern inline void up(struct semaphore * sem)
{
	sem->count++;
	wake_up(&sem->wait);
}	

#define REMOVE_LINKS(p) do { unsigned long flags; \
	save_flags(flags) ; cli(); \
	(p)->next_task->prev_task = (p)->prev_task; \
	(p)->prev_task->next_task = (p)->next_task; \
	restore_flags(flags); \
	if ((p)->p_osptr) \
		(p)->p_osptr->p_ysptr = (p)->p_ysptr; \
	if ((p)->p_ysptr) \
		(p)->p_ysptr->p_osptr = (p)->p_osptr; \
	else \
		(p)->p_pptr->p_cptr = (p)->p_osptr; \
	} while (0)

#define SET_LINKS(p) do { unsigned long flags; \
	save_flags(flags); cli(); \
	(p)->next_task = &init_task; \
	(p)->prev_task = init_task.prev_task; \
	init_task.prev_task->next_task = (p); \
	init_task.prev_task = (p); \
	restore_flags(flags); \
	(p)->p_ysptr = NULL; \
	if (((p)->p_osptr = (p)->p_pptr->p_cptr) != NULL) \
		(p)->p_osptr->p_ysptr = p; \
	(p)->p_pptr->p_cptr = p; \
	} while (0)

#define for_each_task(p) \
	for (p = &init_task ; (p = p->next_task) != &init_task ; )

#endif /* __KERNEL__ */

#endif
