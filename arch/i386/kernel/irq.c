/*
 *	linux/arch/i386/kernel/irq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

/*
 * IRQ's are in fact implemented a bit like signal handlers for the kernel.
 * Naturally it's not a 1:1 relation, but there are similarities.
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
		中断处理
	可以让设备在产生某个事件时通知处理器的方法就是中断。一个“中断”仅是一个信号，
	当硬件需要获得处理器对它的关注时，就可以发送这个信号。 Linux 处理中断的方式
	非常类似在用户空间处理信号的方式。 大多数情况下，一个驱动只需要为它的设备的
	中断注册一个处理例程，并当中断到来时进行正确的处理。本质上来讲，中断处理例程
	和其他的代码并行运行。因此，它们不可避免地引起并发问题，并竞争数据结构和硬件。
	透彻地理解并发控制技术对中断来讲非常重要。
*/
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>

#define CR0_NE 32

//两个变量分别代表主控制器上的中断位和从中断控制器上的中断位
static unsigned char cache_21 = 0xff;
static unsigned char cache_A1 = 0xff;

//屏蔽和开启指定的中断，这里要操作硬件了

//禁止给定的中断 禁止中断的方法是置相应的中断位为1
//因为是要通过写入中断控制器的中断屏蔽位来屏蔽或开启相应的中断
//所以置1表示屏蔽，置0表示开启
void disable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char mask;

	mask = 1 << (irq_nr & 7);
	save_flags(flags);
/*
	每一个8259A芯片都有两个I/O ports，程序员可以通过它们对8259A进行编程。
	Master 8259A的端口地址是0x20，0x21；Slave 8259A的端口地址是0xA0，0xA1。

*/
	//如果irq_nr小于8,表明是Master 8259A上的中断
	if (irq_nr < 8) {
		cli();
		cache_21 |= mask;
		//Write Interrupt Mask Register (IMR)
		outb(cache_21,0x21);
		restore_flags(flags);
		return;
	}
	cli();
	cache_A1 |= mask;
	//Write Interrupt Mask Register (IMR)
	outb(cache_A1,0xA1);
	restore_flags(flags);
}

//开启给定的中断 开启中断的方法是置相应的中断位为0
void enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	unsigned char mask;

	mask = ~(1 << (irq_nr & 7));
	save_flags(flags);
	if (irq_nr < 8) {
		cli();
		cache_21 &= mask;
		outb(cache_21,0x21);
		restore_flags(flags);
		return;
	}
	cli();
	cache_A1 &= mask;
	outb(cache_A1,0xA1);
	restore_flags(flags);
}

/*
 * This builds up the IRQ handler stubs using some ugly macros in irq.h
 *
 * These macros create the low-level assembly IRQ routines that do all
 * the operations that are needed to keep the AT interrupt-controller
 * happy. They are also written to be fast - and to disable interrupts
 * as little as humanly possible.
 *
 * NOTE! These macros expand to three different handlers for each line: one
 * complete handler that does all the fancy stuff (including signal handling),
 * and one fast handler that is meant for simple IRQ's that want to be
 * atomic. The specific handler is chosen depending on the SA_INTERRUPT
 * flag when installing a handler. Finally, one "bad interrupt" handler, that
 * is used when no handler is present.
 */

/*中断处理程序入口点由BUILD_IRQ宏产生*/

BUILD_IRQ(FIRST,0,0x01)
BUILD_IRQ(FIRST,1,0x02)
BUILD_IRQ(FIRST,2,0x04)
BUILD_IRQ(FIRST,3,0x08)
BUILD_IRQ(FIRST,4,0x10)
BUILD_IRQ(FIRST,5,0x20)
BUILD_IRQ(FIRST,6,0x40)
BUILD_IRQ(FIRST,7,0x80)
BUILD_IRQ(SECOND,8,0x01)
BUILD_IRQ(SECOND,9,0x02)
BUILD_IRQ(SECOND,10,0x04)
BUILD_IRQ(SECOND,11,0x08)
BUILD_IRQ(SECOND,12,0x10)
BUILD_IRQ(SECOND,13,0x20)
BUILD_IRQ(SECOND,14,0x40)
BUILD_IRQ(SECOND,15,0x80)

/*
 * Pointers to the low-level handlers: first the general ones, then the
 * fast ones, then the bad ones.
 */
static void (*interrupt[16])(void) = {
	IRQ0_interrupt, IRQ1_interrupt, IRQ2_interrupt, IRQ3_interrupt,
	IRQ4_interrupt, IRQ5_interrupt, IRQ6_interrupt, IRQ7_interrupt,
	IRQ8_interrupt, IRQ9_interrupt, IRQ10_interrupt, IRQ11_interrupt,
	IRQ12_interrupt, IRQ13_interrupt, IRQ14_interrupt, IRQ15_interrupt
};

static void (*fast_interrupt[16])(void) = {
	fast_IRQ0_interrupt, fast_IRQ1_interrupt,
	fast_IRQ2_interrupt, fast_IRQ3_interrupt,
	fast_IRQ4_interrupt, fast_IRQ5_interrupt,
	fast_IRQ6_interrupt, fast_IRQ7_interrupt,
	fast_IRQ8_interrupt, fast_IRQ9_interrupt,
	fast_IRQ10_interrupt, fast_IRQ11_interrupt,
	fast_IRQ12_interrupt, fast_IRQ13_interrupt,
	fast_IRQ14_interrupt, fast_IRQ15_interrupt
};

static void (*bad_interrupt[16])(void) = {
	bad_IRQ0_interrupt, bad_IRQ1_interrupt,
	bad_IRQ2_interrupt, bad_IRQ3_interrupt,
	bad_IRQ4_interrupt, bad_IRQ5_interrupt,
	bad_IRQ6_interrupt, bad_IRQ7_interrupt,
	bad_IRQ8_interrupt, bad_IRQ9_interrupt,
	bad_IRQ10_interrupt, bad_IRQ11_interrupt,
	bad_IRQ12_interrupt, bad_IRQ13_interrupt,
	bad_IRQ14_interrupt, bad_IRQ15_interrupt
};

/*
 * Initial irq handlers.
 */
//该结构体包含处理一种中断所需要的各种信息，代表了内核接收到的特定的IRQ之后应该采取的操作。
struct irqaction {
	void (*handler)(int, struct pt_regs *); //中断发生时，相应的hander指向的中断处理程序
	unsigned long flags;
	unsigned long mask;
	const char *name;
};

static struct irqaction irq_action[16] = {
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL },
	{ NULL, 0, 0, NULL }, { NULL, 0, 0, NULL }
};

/*
 * This generates the report for /proc/interrupt??
 */
//获取所有存在的中断的信息，将其格式化到buf中
int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action = irq_action;

	for (i = 0 ; i < 16 ; i++, action++) {
		if (!action->handler)
			continue;
		//kstat.interrupts[i]应该是内核统计的相应的中断发生的次数
		len += sprintf(buf+len, "%2d: %8d %c %s\n",
			i, kstat.interrupts[i],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
	}
	return len;
}

/*
 * do_IRQ handles IRQ's that have been installed without the
 * SA_INTERRUPT flag: it uses the full signal-handling return
 * and runs with other interrupts enabled. All relatively slow
 * IRQ's should use this format: notably the keyboard/timer
 * routines.
 */
//do_IRQ()函数执行与一个中断相关的所有中断服务例程。
asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqaction * action = irq + irq_action;

	//统计此中断发生的次数
	kstat.interrupts[irq]++;
	//调用相应的中断服务例程
	action->handler(irq, regs);
}

/*
 * do_fast_IRQ handles IRQ's that don't need the fancy interrupt return
 * stuff - the handler is also running with interrupts disabled unless
 * it explicitly enables them later.
 */
/*
		快速和慢速中断处理
	快速中断处理程序在处理时设置了处理器标志位(IF)，表示不允许被中断，这保证了中断的原子处理，
	而调用慢速中断处理时，其它中断仍可以得到服务。但在中断处理前，不管是快速还是慢速中断处理
	程序，内核都要关闭刚才发出报告的那个中断信号线。当处理程序还在处理上一个中断时，如果设备
	又发 出新的中断，新的中断将会丢失。中断控制器并不缓存被屏蔽的中断，但是处理器会进行缓存，
	快速中断处理程序运行时会关闭微处理器的中断报告，中断控制器禁 止了被服务这个中断。中断处
	理程序在处理后可以通过调用sti来启动处理器的中断报告，微处理器就会处理被缓存的中断。sti
	函数是“置中断标志位”处理器指令。慢速处理程序运行时是启动了处理器的中断报告的，但中断控制
	器也禁止了正在被服务这个中断。两种中断处理给内核带来的额外开销也不同。慢速中断处理程序会
	给内核带来的一些管理开销。因此此较频繁（每秒大于100次）的中断最好由快速中断处理程序为之
	提供服务。

	ldd中这样写道：当慢速中断正在执行时，慢速中断要求处理器可以再次启用中断。在现在内核中，区
	别已不大，只剩一个：快速中断执行时，当前处理器其他所有中断都被禁止。

	快速中断是那些能够很快处理的中断，而处理慢速中断会花费更长的时间。在处理慢速中断时处理器重
	新使能中断，避免快速中断被延时过长。在现代内核 中，快速和慢速中断的区别已经消失，剩下的只有
	一个：快速中断(使用 SA_INTERRUPT )执行时禁止所有在当前处理器上的其他中断。注意：其他的处理
	器仍然能够处理中断。

		x86平台上中断处理的内幕
	最底层的中断处理是在头文件irq.h中的声明为宏的一些汇编代码，这些宏在文件irq.h中被扩展。为每个
	中断声明了三种处理函数：慢速，快速和伪处理函数。“伪”处理程序最小，是在没有为中断安装C语言的处
	理程序时的汇编入口点。它将中断转交给PIC(可编程的中断控制器)设备的同时禁止它。在驱动程序处理完
	中断信号后调用free_irq时又会重新安装伪处理程序。伪处理程序不会将/proc/stat中的计数器加1。
	在x86上的自动探测依赖于伪处理程序的这种行为。probe_irq_on启动所有的伪中断，而不安装处理程序；
	probe_irq_off只是简单地检查自调用probe_irq_on以来那些中断被禁止了。
	慢速中断的汇编入口点会将所有寄存器保存到堆栈中，并将数据段(DS和ES处理器寄存器)指向核心地址空间
	(处理器已经设置了CS寄存器)。然 后代码将将中断转交给PIC，禁止在相同的中断信号线上触发新的中断，并
	发出一条sti指令(set interrupt flag，置中断标志位)。处理器在对中断进行服务时会自动清除该标志位。
	接着慢速中断处理程序就将中断号和指向处理器寄存器的一个指针传递给do_IRQ，这是一个C函数，由它来调用
	相应的C语言处理程序。驱动程序传递给中断处理程序参数struct pt_regs *是一个指向存放着各个寄存器的堆
	栈的指针。do_IRQ结束后，会发出cli指令，打开PIC中指定的中断，并调用ret_from_sys_call。
	最后这个入口点(arch/i386/kernel/entry.S)从堆栈中恢复所有的寄存器，处理所有待处理的下半部处理程序，
	如果需要的话，重新调度处理器。快入口点不同的是，在跳转到C代码之前并不调用sti指令，并且在调用do_fast_IRQ
	前并不保存所有的机器寄存器。当驱动程序中的处理程序被调用时，regs参数是NULL(空指针，因为寄存器没有保存到
	堆栈中)并且中断仍被屏蔽。最后，快速中断处理程序会重新打开8259芯片上的所有中断，恢复先前保存的所有寄存器，
	并且不经过ret_from_sys_call就返回了。待处理的下半部处理程序也不运行。
*/
asmlinkage void do_fast_IRQ(int irq)
{
	struct irqaction * action = irq + irq_action;

	kstat.interrupts[irq]++;
	action->handler(irq, NULL);
}

#define SA_PROBE SA_ONESHOT

/*
	中断处理程序可以在驱动程序初始化时或者在设备第一次打开时安装。在init_module函数中申请了
	一个中断、安装了中断处理程序，会阻碍其它驱动程序使用这个中断，可能形成浪费。所以应该在打
	开设备时调用request_irq申请中断，在关闭设备时调用free_irq释放中断将允许资源有限的共享。
*/

//通常从request_irq函数返回给请求函数的值为0时表示申请成功，为负时表示错误码。
//函数返回-EBUSY表示已经有另外一个驱动程序占用了该中断号信号线。
//irq是要申请的硬件中断号。irqflags是中断处理的属性
//handler是向系统注册的中断处理函数，是一个回调函数，中断发生时，系统调用这个函数，dev_id参数将被传递给它。
int request_irq(unsigned int irq, void (*handler)(int, struct pt_regs *),
	unsigned long irqflags, const char * devname)
{
	struct irqaction * action;
	unsigned long flags;

	if (irq > 15)
		return -EINVAL;
	action = irq + irq_action;
	//如果action->handler不为空表示另外一个驱动程序占用了该中断号信号线
	if (action->handler)
		return -EBUSY;
	if (!handler)
		return -EINVAL;
	save_flags(flags);
	cli();
	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	//这里并不会真正的为SA_PROBE标记的、用于探测设备中断号的请求注册中断处理函数
	//这或许是硬件设备可以自己寻求一个可用的中断号的根据
	if (!(action->flags & SA_PROBE)) { /* SA_ONESHOT is used by probing */
		//SA_INTERRUPT ：快速中断标志。快速中断处理例程运行在当前处理器禁止中断的状态下。
		//除非你充足的理由在禁止其他中断情况下来运行中断处理例程，否则不应当使用SA_INTERRUPT.
		if (action->flags & SA_INTERRUPT)
			set_intr_gate(0x20+irq,fast_interrupt[irq]);
		else
			set_intr_gate(0x20+irq,interrupt[irq]);
	}
	//这里是开启相应的中断 因为置0是开启中断
	if (irq < 8) {
		cache_21 &= ~(1<<irq);
		outb(cache_21,0x21);
	} else {
		cache_21 &= ~(1<<2);
		cache_A1 &= ~(1<<(irq-8));
		outb(cache_21,0x21);
		outb(cache_A1,0xA1);
	}
	restore_flags(flags);
	return 0;
}

//释放指定的中断	
void free_irq(unsigned int irq)
{
	struct irqaction * action = irq + irq_action;
	unsigned long flags;

	if (irq > 15) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	if (!action->handler) {
		printk("Trying to free free IRQ%d\n",irq);
		return;
	}
	save_flags(flags);
	cli();
	if (irq < 8) {
		cache_21 |= 1 << irq;
		outb(cache_21,0x21);
	} else {
		cache_A1 |= 1 << (irq-8);
		outb(cache_A1,0xA1);
	}
	set_intr_gate(0x20+irq,bad_interrupt[irq]);
	action->handler = NULL;
	action->flags = 0;
	action->mask = 0;
	action->name = NULL;
	restore_flags(flags);
}

/*
 * Note that on a 486, we don't want to do a SIGFPE on a irq13
 * as the irq is unreliable, and exception 16 works correctly
 * (ie as explained in the intel literature). On a 386, you
 * can't use exception 16 due to bad IBM design, so we have to
 * rely on the less exact irq13.
 *
 * Careful.. Not only is IRQ13 unreliable, but it is also
 * leads to races. IBM designers who came up with it should
 * be shot.
 */
static void math_error_irq(int cpl, struct pt_regs *regs)
{
	outb(0,0xF0);
	if (ignore_irq13 || !hard_math)
		return;
	math_error();
}

static void no_action(int cpl, struct pt_regs * regs) { }

/*
	linux内核提供了一个底层设施来探测中断号，它只能在非共享中断的模式下工作，
	但是大多数硬件有能力工作在共享中断的模式下，并提供更好的找到配置中断号的
	方法。内核的这一设施由以下两个函数组成probe_irq_on()和probe_irq_off()。
	他们工作的方式如下：驱动程序首先调用probe_irq_on()函数获取还未被分配使用
	的中断号irqs，然后引发硬件设备的一个中断，硬件设备可能会自己寻求一个可用的
	中断号并占据之，然后驱动程序关闭设备使用的中断号，即屏蔽其使用的中断号,置相
	应的IMR位为1。因为在probe_irq_on()函数中，函数获取所有未被使用的中断号并开启
	他们，所以调用probe_irq_on()函数之后，IMR的所有位都为0。若之后设备驱动程序
	引发中断，寻找到一个合适的中断号之后，就会关闭相应的中断IMR（置1），然后立即
	调用probe_irq_off()函数，此时IMR中只有硬件自动探测到的中断位为1。
	你也许会问，硬件设备在IMR全部为0的状态下，如何识别哪些是已经被使用的中断，哪些
	是未被使用的中断位呢？答案就是，probe_irq_on()函数是以SA_PROBE标志注册未被使用的
	中断的中断处理函数的。而request_irq()对此标志的处理是，不会真正的注册。参见相关函数。
*/

/*
		关于自动检测中断号
	驱动程序初始化时需要确设备要使用哪条中断信号线。驱动程序需要以此来安装正确的处理程序。
	有时自动检测依赖于设备使用的缺省值。
	有时驱动程序可以通过读设备的某个I/O端口的一个状态字节来获得中断号。这时自动检测中断号
	就是探测设备，不需要额外工作来探测中断。PCI标准就要求外围设备声明要使用的中断信号线。
	有些设备需要自动检测：驱动程序使用设备产生中断，然后观察哪一条中断信号线被激活了。

	在内核帮助下自动检测irq号过程：
	调用probe_irq_on函数
	安排设备产生一次中断
	关闭相应的中断
	调用probe_irq_off函数 根据probe_irq_off函数的返回值来判断设备所使用的中断号。
	当然，除了这种在内核帮助下自动检测irq号，我们还可以手动检测设备的irq。
	其原理为：
	1.启用所有未被使用的中断号，同时循环的调用request_irq申请所有的这些中断号
	2.在中断处理程序里做好标志
	3.设法安排设备产生一次中断
	4.根据中断处理程序里的标志来判断中断处理程序是否运行，从而确定中断号
	5.释放中断号
*/

//该函数返回一个未分配中断的位掩码。驱动程序必须保存返回的位掩码，
//并且将它传递给后面的probe_irq_off函数，调用该函数之后，驱动程序
//要安排设备至少产生一次中断。
unsigned int probe_irq_on (void)
{
	unsigned int i, irqs = 0, irqmask;
	unsigned long delay;

	/* first, snaffle up any unassigned irqs */
	//搜集所有的未分配的中断的位掩码
	for (i = 15; i > 0; i--) {
		//请求分配此中断号，如果申请成功，则开启此中断，并将其中断位合并到irqs中
		if (!request_irq(i, no_action, SA_PROBE, "probe")) {
			enable_irq(i);
			irqs |= (1 << i);
		}
	}

	/* wait for spurious interrupts(伪中断) to mask themselves out again */
	for (delay = jiffies + 2; delay > jiffies; );	/* min 10ms delay */

	/* now filter out any obviously spurious interrupts */
	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
	for (i = 15; i > 0; i--) {
		if (irqs & (1 << i) & irqmask) {
			//异或，相同为0 即0^0=0,1^1=0,0^1=1
			//这里是要去除伪中断
			irqs ^= (1 << i);
			free_irq(i);
		}
	}
#ifdef DEBUG
	printk("probe_irq_on:  irqs=0x%04x irqmask=0x%04x\n", irqs, irqmask);
#endif
	return irqs;
}

/*
	在请求设备产生中断之后，驱动程序调用这个函数，并将前面probe_irq_on返回的位
	掩码作为参数传递给它。probe_irq_off返回"probe_irq_on"之后发生的中断编号，
	如果没有中断发生，就返回0（因此，IRQ 0不能被探测到，但在任何已支持的体系结
	构上，没有任何设备能够使用IRQ 0)。如果产生多次中断，probe_irq_off就返回一个
	负值（出现了二义性）。
	要注意在调用probe_irq_on之后启用设备上的中断，并在调用probe_irq_off之前禁用中断。
	在probe_irq_off之后，需要处理设备上代处理的中断。
*/
int probe_irq_off (unsigned int irqs)
{
	unsigned int i, irqmask;

	//取中断控制器的中断屏蔽位IMR
	irqmask = (((unsigned int)cache_A1)<<8) | (unsigned int)cache_21;
	for (i = 15; i > 0; i--) {
		//释放掉之前probe_irq_off()函数检测到的未被分配使用的中断号
		if (irqs & (1 << i)) {
			//free_irq()函数还会屏蔽(置IMR相应位为0)被释放的中断号
			//但这里并不会改变irqmask的值
			free_irq(i);
		}
	}
#ifdef DEBUG
	printk("probe_irq_off: irqs=0x%04x irqmask=0x%04x\n", irqs, irqmask);
#endif
	irqs &= irqmask;
	//如果irqs为0,表示之前irqs为1的的位对应irqmsak的相应位都为0
	//如果irqs为1,表示之前irqs为1的的位对应irqmsak的相应位有为1的
	/*irqs为1的位表示未被使用的中断号，irqmsak为0的位表示未被屏蔽的中断号*/
	//如果irqs为0，表示相应的未被分配使用的中断号都处于开启状态，因为在调用
	//本函数之前，设备驱动程序必须将其使用的中断屏蔽掉（置IMR相应位为1）
	//所以irqs为0表示设备驱动程序没有得到一个可用的中断号
	if (!irqs)
		/* 没有探测到中断*/
		return 0;
	//ffz = Find First Zero in word.
	//在这里因为将irqs进行取反操作了，所以找到的是第一个非0位
	i = ffz(~irqs);
	//如果激活了一个以上的中断(现在的irqs值中存在不止一个位为1)，返回结果就是负的。
	if (irqs != (irqs & (1 << i)))
		i = -i;
	//返回探测到的设备可用的唯一的一个中断号
	return i;
}

void init_IRQ(void)
{
	int i;

	/* set the clock to 100 Hz */
	//向8254的控制寄存器（端口0x43）中写入值0x34（0011 0100），以便对通道0的计数器进行读写
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	//宏LATCH：Linux用宏LATCH来定义要写到PIT通道0的计数器中的值，它表示PIT将每隔多少个时钟周期产生一次时钟中断
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	//初始化两级PIC的16位中断号，占用了系统的32-47号中断
	for (i = 0; i < 16 ; i++)
		set_intr_gate(0x20+i,bad_interrupt[i]);
	//中断线2用于级联
	if (request_irq(2, no_action, SA_INTERRUPT, "cascade"))
		printk("Unable to get IRQ2 for cascade\n"); //无法得到irq2级联
	if (request_irq(13,math_error_irq, 0, "math error"))
		printk("Unable to get IRQ13 for math-error handler\n");
	//注册主PIC的端口号
	request_region(0x20,0x20,"pic1");
	//注册次PIC的端口号
	request_region(0xa0,0x20,"pic2");
} 
