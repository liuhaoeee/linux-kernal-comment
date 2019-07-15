/*
 *  linux/kernel/sched.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives(原始的)
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
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

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/fdreg.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/ptrace.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/tqueue.h>
#include <linux/resource.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/pgtable.h>

#define TIMER_IRQ 0

#include <linux/timex.h>

/*
 * kernel variables
 */
 /*
	时钟滴答的时间间隔：Linux用全局变量tick来表示时钟滴答的时间间隔长度，
	long tick = (1000000 + HZ/2) / HZ; // timer interrupt period 
	tick变量的单位是微妙（μs），由于在不同平台上宏HZ的值会有所不同，
	因此方程式tick＝1000000÷HZ的结果可能会是个小数，因此将其进行四舍五入成一个整数，
	所以Linux将tick定义成（1000000＋HZ／2）／HZ，其中被除数表达式中的HZ／2的作用就是用来将tick值向上圆整成一个整型数。 
	另外，Linux还用宏TICK_SIZE来作为tick变量的引用别名（alias）
 */
long tick = 1000000 / HZ;               /* timer interrupt period */
//xtime是从cmos电路中取得的时间，一般是从某一历史时刻开始到现在的时间
//也就是为了取得我们操作系统上显示的日期。这个就是所谓的“实时时钟”，它的精确度是微秒
volatile struct timeval xtime;		/* The current time */
int tickadj = 500/HZ;			/* microsecs */

DECLARE_TASK_QUEUE(tq_timer);
DECLARE_TASK_QUEUE(tq_immediate);

/*
 * phase-lock loop variables
 */
int time_status = TIME_BAD;     /* clock synchronization status */
long time_offset = 0;           /* time adjustment (us) */
long time_constant = 0;         /* pll time constant */
long time_tolerance = MAXFREQ;  /* frequency tolerance (ppm) */
long time_precision = 1; 	/* clock precision (us) */
long time_maxerror = 0x70000000;/* maximum error */
long time_esterror = 0x70000000;/* estimated error */
long time_phase = 0;            /* phase offset (scaled us) */
long time_freq = 0;             /* frequency offset (scaled ppm) */
long time_adj = 0;              /* tick adjust (scaled 1 / HZ) */
long time_reftime = 0;          /* time at last adjustment (s) */

long time_adjust = 0;
long time_adjust_step = 0;

int need_resched = 0;
unsigned long event = 0;

extern int _setitimer(int, struct itimerval *, struct itimerval *);
unsigned long * prof_buffer = NULL;
unsigned long prof_len = 0;

#define _S(nr) (1<<((nr)-1))

extern void mem_use(void);

extern int timer_interrupt(void);

//用于系统初始化时的任务0的内核和用户态堆栈
static unsigned long init_kernel_stack[1024] = { STACK_MAGIC, };
unsigned long init_user_stack[1024] = { STACK_MAGIC, };
static struct vm_area_struct init_mmap = INIT_MMAP;
struct task_struct init_task = INIT_TASK;

/*
	全局变量jiffies：这是一个32位的无符号整数，用来表示自内核上一次启动以来的时钟滴答次数。
	每发生一次时钟滴答，内核的时钟中断处理函数timer_interrupt（）都要将该全局变量jiffies加1。 
	unsigned long volatile jiffies; 
	C语言限定符volatile表示jiffies是一个易该变的变量，因此编译器将使对该变量的访问从不通过CPU内部cache来进行。
*/
unsigned long volatile jiffies=0;

struct task_struct *current = &init_task;
struct task_struct *last_task_used_math = NULL;

struct task_struct * task[NR_TASKS] = {&init_task, };

//内核全局统计信息变量kstat
//kernel_stat定义于linux/include/linux/kernel_stat.h文件中
struct kernel_stat kstat = { 0 };

//下面这两个全局变量的意义，请参加do_timer()函数和schedule()函数中的相关注释
//总之，内核机制很经典
unsigned long itimer_ticks = 0;
unsigned long itimer_next = ~0;

/*
 *  'schedule()' is the scheduler function. It's a very simple and nice
 * scheduler: it's not perfect, but certainly works for most things.
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 *
 * The "confuse_gcc" goto is used only to get better assembly code..
 * Dijkstra probably hates me.
 */
asmlinkage void schedule(void)
{
	int c;
	struct task_struct * p;
	struct task_struct * next;
	unsigned long ticks;

/* check alarm, wake up any interruptible tasks that have got a signal */

	if (intr_count) {
		printk("Aiee: scheduling in interrupt\n");
		intr_count = 0;
	}
	//关中断 因为时钟中断时刻都可能发生 并改变itimer_ticks的值
	cli();
	//取真实间隔定时器自上次更新以来所经过的滴答数，然后将其置0
	ticks = itimer_ticks;
	itimer_ticks = 0;
	//itimer_next应该是itimer_ticks的最大值
	itimer_next = ~0;
	sti();
	need_resched = 0;
	p = &init_task;
	for (;;) {
		if ((p = p->next_task) == &init_task)
			goto confuse_gcc1;
		//如果ticks不为0，并且当前进程的真实间隔定时器的当前值不为0
		if (ticks && p->it_real_value) {
			//如果当前进程的真实间隔定时器的当前值小于等于ticks的值
			//说明自当前进程的真实间隔定时器值上次更新后，所经历的
			//时钟滴答数足以使其超时
			if (p->it_real_value <= ticks) {
				//向此进程发送SIGALRM信号
				send_sig(SIGALRM, p, 1);
				//如果此进程的真实间隔定时器的it_real_incr为0
				if (!p->it_real_incr) {
					p->it_real_value = 0;
					goto end_itimer;
				}
				//执行到这里，说明此进程的真实间隔定时器的it_real_incr不为0
				//因为ticks的值可能很大，所以不能简单地将p->it_real_value -= ticks;
				//而是先将恢复到一定的值
				do {
					p->it_real_value += p->it_real_incr;
				} while (p->it_real_value <= ticks);
			}
			//更新it_real_value的值
			p->it_real_value -= ticks;
			//从这里以及结合在do_timer()函数中更新itimer_next的值的方法来看
			//itimer_next保存了系统中各个进程的真实间隔定时器中当前值最小的
			//间隔值，意义就是下一次要触发的真实间隔定时器。当在do_timer()函数
			//中更新itimer_ticks的值后，会比较两者之间的大小，若itimer_ticks的值
			//大于itimer_next的值，则标记系统发生进程调度，从而在这里触发某个
			//进程的真实间隔定时器
			if (p->it_real_value < itimer_next)
				itimer_next = p->it_real_value;
		}
end_itimer:
		if (p->state != TASK_INTERRUPTIBLE)
			continue;
		if (p->signal & ~p->blocked) {
			p->state = TASK_RUNNING;
			continue;
		}
		if (p->timeout && p->timeout <= jiffies) {
			p->timeout = 0;
			p->state = TASK_RUNNING;
		}
	}
confuse_gcc1:

/* this is the scheduler proper: */
#if 0
	/* give processes that go to sleep a bit higher priority.. */
	/* This depends on the values for TASK_XXX */
	/* This gives smoother scheduling for some things, but */
	/* can be very unfair under some circumstances, so.. */
 	if (TASK_UNINTERRUPTIBLE >= (unsigned) current->state &&
	    current->counter < current->priority*2) {
		++current->counter;
	}
#endif
	c = -1000;
	next = p = &init_task;
	for (;;) {
		if ((p = p->next_task) == &init_task)
			goto confuse_gcc2;
		if (p->state == TASK_RUNNING && p->counter > c)
			c = p->counter, next = p;
	}
confuse_gcc2:
	if (!c) {
		for_each_task(p)
			p->counter = (p->counter >> 1) + p->priority;
	}
	if (current == next)
		return;
	kstat.context_swtch++;
	switch_to(next);
}

asmlinkage int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

/*
 * wake_up doesn't wake up stopped processes - they have to be awakened
 * with signals or similar.
 *
 * Note that this doesn't need cli-sti pairs: interrupts may not change
 * the wait-queue structures directly, but only call wake_up() to wake
 * a process. The process itself must remove the queue once it has woken.
 */
void wake_up(struct wait_queue **q)
{
	struct wait_queue *tmp;
	struct task_struct * p;

	if (!q || !(tmp = *q))
		return;
	do {
		if ((p = tmp->task) != NULL) {
			if ((p->state == TASK_UNINTERRUPTIBLE) ||
			    (p->state == TASK_INTERRUPTIBLE)) {
				p->state = TASK_RUNNING;
				if (p->counter > current->counter + 3)
					need_resched = 1;
			}
		}
		if (!tmp->next) {
			printk("wait_queue is bad (eip = %p)\n",
				__builtin_return_address(0));
			printk("        q = %p\n",q);
			printk("       *q = %p\n",*q);
			printk("      tmp = %p\n",tmp);
			break;
		}
		tmp = tmp->next;
	} while (tmp != *q);
}

void wake_up_interruptible(struct wait_queue **q)
{
	struct wait_queue *tmp;
	struct task_struct * p;

	if (!q || !(tmp = *q))
		return;
	do {
		if ((p = tmp->task) != NULL) {
			if (p->state == TASK_INTERRUPTIBLE) {
				p->state = TASK_RUNNING;
				if (p->counter > current->counter + 3)
					need_resched = 1;
			}
		}
		if (!tmp->next) {
			printk("wait_queue is bad (eip = %p)\n",
				__builtin_return_address(0));
			printk("        q = %p\n",q);
			printk("       *q = %p\n",*q);
			printk("      tmp = %p\n",tmp);
			break;
		}
		tmp = tmp->next;
	} while (tmp != *q);
}

void __down(struct semaphore * sem)
{
	struct wait_queue wait = { current, NULL };
	add_wait_queue(&sem->wait, &wait);
	current->state = TASK_UNINTERRUPTIBLE;
	while (sem->count <= 0) {
		schedule();
		current->state = TASK_UNINTERRUPTIBLE;
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&sem->wait, &wait);
}

static inline void __sleep_on(struct wait_queue **p, int state)
{
	unsigned long flags;
	struct wait_queue wait = { current, NULL };

	if (!p)
		return;
	if (current == task[0])
		panic("task[0] trying to sleep");
	current->state = state;
	add_wait_queue(p, &wait);
	save_flags(flags);
	sti();
	schedule();
	remove_wait_queue(p, &wait);
	restore_flags(flags);
}

void interruptible_sleep_on(struct wait_queue **p)
{
	__sleep_on(p,TASK_INTERRUPTIBLE);
}

void sleep_on(struct wait_queue **p)
{
	__sleep_on(p,TASK_UNINTERRUPTIBLE);
}

/*
 * The head for the timer-list has a "expires" field of MAX_UINT,
 * and the sorting routine counts on this..
 */
static struct timer_list timer_head = { &timer_head, &timer_head, ~0, 0, NULL };
#define SLOW_BUT_DEBUGGING_TIMERS 1

//函 数add_timer()用来将参数timer指针所指向的定时器插入到定时器链表中
void add_timer(struct timer_list * timer)
{
	unsigned long flags;
	struct timer_list *p;

#if SLOW_BUT_DEBUGGING_TIMERS
	//新加入的定时器的next和prev域应该为空
	if (timer->next || timer->prev) {
		printk("add_timer() called with non-zero list from %p\n",
			__builtin_return_address(0));
		return;
	}
#endif
	//p指向定时器链表头
	p = &timer_head;
	//设置超时时间
	timer->expires += jiffies;
	save_flags(flags);
	//关中断 因为要对系统全局共享链表进行操作了
	cli();
	//遍历定时器链表 找到合适的位置将定时器timer插入
	do {
		p = p->next;
	} while (timer->expires > p->expires);
	//将timer插入定时器链表
	timer->next = p;
	timer->prev = p->prev;
	p->prev = timer;
	timer->prev->next = timer;
	restore_flags(flags);
}

//函数del_timer()用来将一个定时器从核定时器链表中删除
int del_timer(struct timer_list * timer)
{
	unsigned long flags;
#if SLOW_BUT_DEBUGGING_TIMERS
	struct timer_list * p;

	//p指向内核定时器链表
	p = &timer_head;
	save_flags(flags);
	//关中断
	cli();
	//遍历内核定时器链表 找到指定的要删除的定时器
	while ((p = p->next) != &timer_head) {
		//找到了指定的定时器
		if (p == timer) {
			//将此定时器从内核定时器链表中移除
			timer->next->prev = timer->prev;
			timer->prev->next = timer->next;
			timer->next = timer->prev = NULL;
			//恢复标志寄存器的值 这里应该是开中断了 因为对内核定时器链表的操作结束了
			restore_flags(flags);
			timer->expires -= jiffies;
			return 1;
		}
	}
	if (timer->next || timer->prev)
		printk("del_timer() called from %p with timer not initialized\n",
			__builtin_return_address(0));
	restore_flags(flags);
	return 0;
#else	
	save_flags(flags);
	cli();
	if (timer->next) {
		timer->next->prev = timer->prev;
		timer->prev->next = timer->next;
		timer->next = timer->prev = NULL;
		restore_flags(flags);
		timer->expires -= jiffies;
		return 1;
	}
	restore_flags(flags);
	return 0;
#endif
}

unsigned long timer_active = 0;
struct timer_struct timer_table[32];

/*
 * Hmm.. Changed this, as the GNU make sources (load.c) seems to
 * imply that avenrun[] is the standard name for this kind of thing.
 * Nothing else seems to be standardized: the fractional size etc
 * all seem to differ on different machines.
 */
unsigned long avenrun[3] = { 0,0,0 };

/*
 * Nr of active tasks - counted in fixed-point numbers
 */
 /*
	在Linux系统中，uptime、w、top等命令都会有系统平均负载load average的输出，那么什么是系统平均负载呢？
　　系统平均负载被定义为在特定时间间隔内运行队列中的平均进程树。如果一个进程满足以下条件则其就会位于运行队列中：
　　- 它没有在等待I/O操作的结果
　　- 它没有主动进入等待状态(也就是没有调用'wait')
　　- 没有被停止(例如：等待终止)
　　例如：
	　　[root@opendigest root]# uptime
	　　7:51pm up 2 days, 5:43, 2 users, load average: 8.13, 5.90, 4.94
	命令输出的最后内容表示在过去的1、5、15分钟内运行队列中的平均进程数量。
	一般来说只要每个CPU的当前活动进程数不大于3那么系统的性能就是良好的，如果每个CPU的任务数大于5，
	那么就表示这台机器的性能有严重问题。对于上面的例子来说，假设系统有两个CPU，那么其每个CPU的当前任
	务数为：8.13/2=4.065。这表示该系统的性能是可以接受的。
 */
 //统计当前系统中所有的活动进程数
static unsigned long count_active_tasks(void)
{
	struct task_struct **p;
	unsigned long nr = 0;

	//遍历任务数组
	for(p = &LAST_TASK; p > &FIRST_TASK; --p)
		//如果当前任务处于运行状态或者不可中断的睡眠状态或者换入换出状态
		if (*p && ((*p)->state == TASK_RUNNING ||
			   (*p)->state == TASK_UNINTERRUPTIBLE ||
			   (*p)->state == TASK_SWAPPING))
			nr += FIXED_1;
	return nr;
}

static inline void calc_load(void)
{
	unsigned long active_tasks; /* fixed-point */
	static int count = LOAD_FREQ;   //LOAD_FREQ当前为5秒的时间间隔

	if (count-- > 0)
		return;
	count = LOAD_FREQ;
	//获取当前系统中所有的活动进程数
	active_tasks = count_active_tasks();
	//过去的1、5、15分钟内运行队列中的平均进程数量 放在avenrun数组中
	CALC_LOAD(avenrun[0], EXP_1, active_tasks);
	CALC_LOAD(avenrun[1], EXP_5, active_tasks);
	CALC_LOAD(avenrun[2], EXP_15, active_tasks);
}

/*
 * this routine handles the overflow of the microsecond field
 *
 * The tricky bits of code to handle the accurate clock support
 * were provided by Dave Mills (Mills@UDEL.EDU) of NTP fame.
 * They were originally developed for SUN and DEC kernels.
 * All the kudos should go to Dave for this stuff.
 *
 * These were ported to Linux by Philip Gladstone.
 */
 //函数来处理微秒数成员溢出的情况
static void second_overflow(void)
{
	long ltemp;
	/* last time the cmos clock got updated */
	static long last_rtc_update=0;
	extern int set_rtc_mmss(unsigned long);

	/* Bump the maxerror field */
	time_maxerror = (0x70000000-time_maxerror < time_tolerance) ?
	  0x70000000 : (time_maxerror + time_tolerance);

	/* Run the PLL */
	//模拟了PLL锁相环？？？
	if (time_offset < 0) {
		ltemp = (-(time_offset+1) >> (SHIFT_KG + time_constant)) + 1;
		time_adj = ltemp << (SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE);
		time_offset += (time_adj * HZ) >> (SHIFT_SCALE - SHIFT_UPDATE);
		time_adj = - time_adj;
	} else if (time_offset > 0) {
		ltemp = ((time_offset-1) >> (SHIFT_KG + time_constant)) + 1;
		time_adj = ltemp << (SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE);
		time_offset -= (time_adj * HZ) >> (SHIFT_SCALE - SHIFT_UPDATE);
	} else {
		time_adj = 0;
	}

	time_adj += (time_freq >> (SHIFT_KF + SHIFT_HZ - SHIFT_SCALE))
	    + FINETUNE;

	/* Handle the leap second stuff */
	switch (time_status) {
		case TIME_INS:
		/* ugly divide should be replaced */
		if (xtime.tv_sec % 86400 == 0) {
			xtime.tv_sec--; /* !! */
			time_status = TIME_OOP;
			printk("Clock: inserting leap second 23:59:60 GMT\n");
		}
		break;

		case TIME_DEL:
		/* ugly divide should be replaced */
		if (xtime.tv_sec % 86400 == 86399) {
			xtime.tv_sec++;
			time_status = TIME_OK;
			printk("Clock: deleting leap second 23:59:59 GMT\n");
		}
		break;

		case TIME_OOP:
		time_status = TIME_OK;
		break;
	}
/*
	判断是否需要更新CMOS时钟（即RTC）中的时间。Linux仅在下列三个条件同时成立时才更新CMOS时钟：
	①系统全局时间状态变量time_status中没有设置STA_UNSYNC标志，也即说明Linux有一个外部同步时钟。
	实际上全局时间状态变量time_status仅在一种情况下会被清除STA_SYNC标志，那就是执行adjtimex()
	系统调用时（这个syscall与NTP有关）。②自从上次CMOS时钟更新已经过去了11分钟。last_rtc_update
	保存着上次更新CMOS时钟的时间。③由于RTC存在Update Cycle，因此最好在一秒时间间隔的中间位置
	500ms左右调用set_rtc_mmss()函数来更新CMOS时钟。因此Linux规定仅当全局变量xtime的微秒数
	tv_usec在500000±（tick/2）微秒范围范围之内时，才调用set_rtc_mmss()函数。如果上述条件均成立，
	那就调用set_rtc_mmss()将当前时间xtime.tv_sec更新回写到RTC中。
	如果上面是的set_tc_mmss()函数返回0值，则表明更新成功。于是就将“最近一次RTC更新时间”变量
	last_rtc_update更新为当前时间xtime.tv_sec。如果返回非0值，说明更新失败，于是就让
	last_rtc_update=xtime.tv_sec-600（相当于 last_rtc_update+=60），以便在在 60 秒之后再次对 RTC 进行更新。

*/
	if (time_status != TIME_BAD && xtime.tv_sec > last_rtc_update + 660)
	  if (set_rtc_mmss(xtime.tv_sec) == 0)
	    last_rtc_update = xtime.tv_sec;
	  else
	    last_rtc_update = xtime.tv_sec - 600; /* do it again in one min */
}

/*
 * disregard lost ticks for now.. We don't care enough.
 */
static void timer_bh(void * unused)
{
	unsigned long mask;
	struct timer_struct *tp;
	struct timer_list * timer;

	//关中断 因为要操作内核定时器链表了
	cli();
	//遍历内核定时器链表 找到已经到时的定时器
	while ((timer = timer_head.next) != &timer_head && timer->expires < jiffies) {
		//fn指向此定时器中指定的函数
		void (*fn)(unsigned long) = timer->function;
		//data是此定时器执行函数的参数
		unsigned long data = timer->data;
		//将此定时器从内核定时器链表中移除
		timer->next->prev = timer->prev;
		timer->prev->next = timer->next;
		timer->next = timer->prev = NULL;
		//开中断
		sti();
		//执行定时器函数
		fn(data);
		//关中断 为下一个可能到时的定时器的遍历做准备
		cli();
	}
	//开中断
	sti();
	
	for (mask = 1, tp = timer_table+0 ; mask ; tp++,mask += mask) {
		if (mask > timer_active)
			break;
		if (!(mask & timer_active))
			continue;
		if (tp->expires > jiffies)
			continue;
		timer_active &= ~mask;
		tp->fn();
		sti();
	}
}

void tqueue_bh(void * unused)
{
	run_task_queue(&tq_timer);
}

void immediate_bh(void * unused)
{
	run_task_queue(&tq_immediate);
}

/*
 * The int argument is really a (struct pt_regs *), in case the
 * interrupt wants to know from where it was called. The timer
 * irq uses this to decide if it should update the user or system
 * times.
 */
static void do_timer(int irq, struct pt_regs * regs)
{
	unsigned long mask;
	struct timer_struct *tp;

	long ltemp, psecs;

	/* Advance the phase, once it gets to one microsecond, then
	 * advance the tick more.
	 */
	time_phase += time_adj;
	if (time_phase < -FINEUSEC) {
		ltemp = -time_phase >> SHIFT_SCALE;
		time_phase += ltemp << SHIFT_SCALE;
		xtime.tv_usec += tick + time_adjust_step - ltemp;
	}
	else if (time_phase > FINEUSEC) {
		ltemp = time_phase >> SHIFT_SCALE;
		time_phase -= ltemp << SHIFT_SCALE;
		xtime.tv_usec += tick + time_adjust_step + ltemp;
	} else
		xtime.tv_usec += tick + time_adjust_step;

	if (time_adjust)
	{
	    /* We are doing an adjtime thing. 
	     *
	     * Modify the value of the tick for next time.
	     * Note that a positive delta means we want the clock
	     * to run fast. This means that the tick should be bigger
	     *
	     * Limit the amount of the step for *next* tick to be
	     * in the range -tickadj .. +tickadj
	     */
	     if (time_adjust > tickadj)
	       time_adjust_step = tickadj;
	     else if (time_adjust < -tickadj)
	       time_adjust_step = -tickadj;
	     else
	       time_adjust_step = time_adjust;
	     
	    /* Reduce by this step the amount of time left  */
	    time_adjust -= time_adjust_step;
	}
	else
	    time_adjust_step = 0;

	if (xtime.tv_usec >= 1000000) {
	    xtime.tv_usec -= 1000000;
	    xtime.tv_sec++;
	    second_overflow();
	}

	//将表示自系统启动以来的时钟滴答计数变量jiffies加1
	//为什么不关中断？因为jiffies的值只在这里发生改变吗？
	jiffies++;
	//计算系统负载
	calc_load();
	//如果时钟中断中的的是用户态程序
	if (user_mode(regs)) {
		//当前进程的用户态时间自增
		current->utime++;
		if (current != task[0]) {
			if (current->priority < 15)
				kstat.cpu_nice++;
			else
				kstat.cpu_user++;
		}
/*
		每个进程都有一个用户态执行时间的itimer软件定时器。进程任务结构task_struct中的
		it_virt_value成员是这个软件定时器的时间计数器。当进程在用户态下执行时，每一次
		时钟滴答都使计数器it_virt_value减1，当减到0时内核向进程发送SIGVTALRM信号，并重
		置初值。初值保存在进程的 task_struct 结构的it_virt_incr成员中。
*/
		/* Update ITIMER_VIRT for current task if not in a system call */
		if (current->it_virt_value && !(--current->it_virt_value)) {
			//重置间隔定时器的倒计时数，以形成“间隔”定时器
			current->it_virt_value = current->it_virt_incr;
			send_sig(SIGVTALRM,current,1);
		}
		//中断的是内核空间
	} else {
		//当前进程的内核态时间自增
		current->stime++;
		if(current != task[0])
			kstat.cpu_system++;
#ifdef CONFIG_PROFILE
		if (prof_buffer && current != task[0]) {
			unsigned long eip = regs->eip;
			eip >>= CONFIG_PROFILE_SHIFT;
			if (eip < prof_len)
				prof_buffer[eip]++;
		}
#endif
	}
	/*
	 * check the cpu time limit on the process.
	 */
	//运行时间长度超过了进程资源限额的最大值，那就发送一个 SIGKILL 信号杀死该进程
	if ((current->rlim[RLIMIT_CPU].rlim_max != RLIM_INFINITY) &&
	    (((current->stime + current->utime) / HZ) >= current->rlim[RLIMIT_CPU].rlim_max))
		send_sig(SIGKILL, current, 1);
	if ((current->rlim[RLIMIT_CPU].rlim_cur != RLIM_INFINITY) &&
	    (((current->stime + current->utime) % HZ) == 0)) {
		//psecs表示了指定进程p到目前为止已经运行的总时间长度
		psecs = (current->stime + current->utime) / HZ;
		/* send when equal */
		if (psecs == current->rlim[RLIMIT_CPU].rlim_cur)
			send_sig(SIGXCPU, current, 1);
		/* and every five seconds thereafter. */
		//如果这一总运行时间长超过进程P的资源限额，那就每隔5秒给进程发送一个SIGXCPU信号
		else if ((psecs > current->rlim[RLIMIT_CPU].rlim_cur) &&
		        ((psecs - current->rlim[RLIMIT_CPU].rlim_cur) % 5) == 0)
			send_sig(SIGXCPU, current, 1);
	}

	//如果当前进程不是任务0，并且分配的运行滴答数到期
	if (current != task[0] && 0 > --current->counter) {
		//将当前进程的运行滴答值置为0
		current->counter = 0;
		//置调度标识 当从内核态返回用户态时会导致进程切换
		need_resched = 1;
	}
/*
	每个进程也都有一个itimer软件定时器ITIMER_PROF。进程task_struct中的it_prof_value成员
	就是这个定时器的时间计数器。不管进程是在用户态下还是在内核态下运行，每个时钟滴答都
	使it_prof_value减1。当减到0时内核就向进程发送 SIGPROF 信号，并重置初值。初值保存在
	进程 task_struct结构中的it_prof_incr成员中
*/
	/* Update ITIMER_PROF for the current task */
	if (current->it_prof_value && !(--current->it_prof_value)) {
		//重置间隔定时器的倒计时数，以形成“间隔”定时器
		current->it_prof_value = current->it_prof_incr;
		send_sig(SIGPROF,current,1);
	}
	for (mask = 1, tp = timer_table+0 ; mask ; tp++,mask += mask) {
		if (mask > timer_active)
			break;
		if (!(mask & timer_active))
			continue;
		if (tp->expires > jiffies)
			continue;
		//调用mark_bh()函数激活时钟中断的Bottom Half向量TIMER_BH
		mark_bh(TIMER_BH);
	}
	cli();
/*
	itimer_ticks是系统中真实间隔定时器的一个“计数器”，记录着自上次某进程的
	真实间隔定时器的值更新之后所经历的滴答数。实际上，itimer_next全局变量
	记录着系统中所有进程的所有真实间隔定时器中当前值最小的那一个定时器的
	值。若更新itimer_ticks的值后，其值大于itimer_next，则说明系统中某个进程
	的真实间隔定时器到时了，则需要触发此定时器。于是标记系统需要进行进程调度，
	在进程调度函数中遍历所有的进程并检查器真实间隔定时器。
*/	
	itimer_ticks++;
	if (itimer_ticks > itimer_next)
		need_resched = 1;
/*
	上半部在屏蔽中断的上下文中运行，用于完成关键性的处理动作；而下半部则相对来
	说并不是非常紧急的，通常还是比较耗时的，因此由系统自行安排运行时机，不在中
	断服务上下文中执行。这里，关键性的处理动作就是标记
*/
	if (timer_head.next->expires < jiffies)
		mark_bh(TIMER_BH);
	if (tq_timer != &tq_last)
		//调用mark_bh()函数激活时钟中断的Bottom Half向量TQUEUE_BH
		mark_bh(TQUEUE_BH);
	sti();
}

//闹钟函数，它可以在进程中设置一个定时器，当定时器指定的时间到时，
//它向进程发送SIGALRM信号。如果忽略或者不捕获此信号，则其默认动
//作是终止调用该alarm函数的进程
asmlinkage int sys_alarm(long seconds)
{
	struct itimerval it_new, it_old;

	//设置一个当前值为seconds，初始值为0的真实间隔定时器
	//其效果就是seconds秒过后，触发此定时器，然后此定时器消失
	it_new.it_interval.tv_sec = it_new.it_interval.tv_usec = 0;
	it_new.it_value.tv_sec = seconds;
	it_new.it_value.tv_usec = 0;
	_setitimer(ITIMER_REAL, &it_new, &it_old);
	return(it_old.it_value.tv_sec + (it_old.it_value.tv_usec / 1000000));
}

asmlinkage int sys_getpid(void)
{
	return current->pid;
}

asmlinkage int sys_getppid(void)
{
	return current->p_opptr->pid;
}

asmlinkage int sys_getuid(void)
{
	return current->uid;
}

asmlinkage int sys_geteuid(void)
{
	return current->euid;
}

asmlinkage int sys_getgid(void)
{
	return current->gid;
}

asmlinkage int sys_getegid(void)
{
	return current->egid;
}

asmlinkage int sys_nice(long increment)
{
	int newprio;

	if (increment < 0 && !suser())
		return -EPERM;
	newprio = current->priority - increment;
	if (newprio < 1)
		newprio = 1;
	if (newprio > 35)
		newprio = 35;
	current->priority = newprio;
	return 0;
}

static void show_task(int nr,struct task_struct * p)
{
	unsigned long free;
	static char * stat_nam[] = { "R", "S", "D", "Z", "T", "W" };

	printk("%-8s %3d ", p->comm, (p == current) ? -nr : nr);
	if (((unsigned) p->state) < sizeof(stat_nam)/sizeof(char *))
		printk(stat_nam[p->state]);
	else
		printk(" ");
#ifdef __i386__
	if (p == current)
		printk(" current  ");
	else
		printk(" %08lX ", ((unsigned long *)p->tss.esp)[3]);
#endif
	for (free = 1; free < 1024 ; free++) {
		if (((unsigned long *)p->kernel_stack_page)[free])
			break;
	}
	printk("%5lu %5d %6d ", free << 2, p->pid, p->p_pptr->pid);
	if (p->p_cptr)
		printk("%5d ", p->p_cptr->pid);
	else
		printk("      ");
	if (p->p_ysptr)
		printk("%7d", p->p_ysptr->pid);
	else
		printk("       ");
	if (p->p_osptr)
		printk(" %5d\n", p->p_osptr->pid);
	else
		printk("\n");
}

void show_state(void)
{
	int i;

	printk("                         free                        sibling\n");
	printk("  task             PC    stack   pid father child younger older\n");
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i])
			show_task(i,task[i]);
}

/*
	这个程序是名副其实的初始化程序：仅仅为进程调度程序的执行做准备。它
	所做的具体工作是调用init_bh函数（在kernel/softirq.c中）把timer
	tqueue，immediate三个任务队列加入下半部分的数组
*/
void sched_init(void)
{
	bh_base[TIMER_BH].routine = timer_bh;
	bh_base[TQUEUE_BH].routine = tqueue_bh;
	bh_base[IMMEDIATE_BH].routine = immediate_bh;
	//TIMER_IRQ = 0
	if (request_irq(TIMER_IRQ, do_timer, 0, "timer") != 0)
		panic("Could not allocate timer IRQ!");
	enable_bh(TIMER_BH);
	enable_bh(TQUEUE_BH);
	enable_bh(IMMEDIATE_BH);
}
