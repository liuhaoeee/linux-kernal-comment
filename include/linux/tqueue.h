/*
 * tqueue.h --- task queue handling for Linux.
 *
 * Mostly based on a proposed bottom-half replacement code written by
 * Kai Petzke, wpp@marie.physik.tu-berlin.de.
 *
 * Modified for use in the Linux kernel by Theodore Ts'o,
 * tytso@mit.edu.  Any bugs are my fault, not Kai's.
 *
 * The original comment follows below.
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
#ifndef _LINUX_TQUEUE_H
#define _LINUX_TQUEUE_H

#include <asm/bitops.h>
#include <asm/system.h>

#ifdef INCLUDE_INLINE_FUNCS
#define _INLINE_ extern
#else
#define _INLINE_ extern __inline__
#endif

/*
 * New proposed "bottom half" handlers:
 * (C) 1994 Kai Petzke, wpp@marie.physik.tu-berlin.de
 *
 * Advantages:
 * - Bottom halfs are implemented as a linked list.  You can have as many
 *   of them, as you want.
 * - No more scanning of a bit field is required upon call of a bottom half.
 * - Support for chained bottom half lists.  The run_task_queue() function can be
 *   used as a bottom half handler.  This is for example useful for bottom
 *   halfs, which want to be delayed until the next clock tick.
 *
 * Problems:
 * - The queue_task_irq() inline function is only atomic with respect to itself.
 *   Problems can occur, when queue_task_irq() is called from a normal system
 *   call, and an interrupt comes in.  No problems occur, when queue_task_irq()
 *   is called from an interrupt or bottom half, and interrupted, as run_task_queue()
 *   will not be executed/continued before the last interrupt returns.  If in
 *   doubt, use queue_task(), not queue_task_irq().
 * - Bottom halfs are called in the reverse order that they were linked into
 *   the list.
 */

struct tq_struct {
	struct tq_struct *next;		/* linked list of active bh's *//* 链表结构 */
	int sync;			/* must be initialized to zero */ /* 初值为0，入队时原子的置1，以避免重复入队 */
	void (*routine)(void *);	/* function to call *//* 激活时调用的函数 */
	void *data;			/* argument to function */
};

typedef struct tq_struct * task_queue;

/* 定义一个task_queue，实际上就是一个以q为元素的task_queue队列 */ 
//tq_last是一个指向自身的struct tq_struct结构
#define DECLARE_TASK_QUEUE(q)  task_queue q = &tq_last

extern struct tq_struct tq_last;
//tq_timer，由时钟中断服务程序启动
//tq_immediate，在中断返回前以及schedule()函数中启动
extern task_queue tq_timer, tq_immediate;

#ifdef INCLUDE_INLINE_FUNCS
struct tq_struct tq_last = {
	&tq_last, 0, 0, 0
};
#endif

/*
 * To implement your own list of active bottom halfs, use the following
 * two definitions:
 *
 * struct tq_struct *my_bh = &tq_last;
 * struct tq_struct run_my_bh = {
 *	0, 0, (void *)(void *) run_task_queue, &my_bh
 * };
 *
 * To activate a bottom half on your list, use:
 *
 *     queue_task(tq_pointer, &my_bh);
 *
 * To run the bottom halfs on your list put them on the immediate list by:
 *
 *     queue_task(&run_my_bh, &tq_immediate);
 *
 * This allows you to do deferred procession.  For example, you could
 * have a bottom half list tq_timer, which is marked active by the timer
 * interrupt.
 */

/*
 * queue_task_irq: put the bottom half handler "bh_pointer" on the list
 * "bh_list".  You may call this function only from an interrupt
 * handler or a bottom half handler.
 */
_INLINE_ void queue_task_irq(struct tq_struct *bh_pointer,
			       task_queue *bh_list)
{
	if (!set_bit(0,&bh_pointer->sync)) {
		bh_pointer->next = *bh_list;
		*bh_list = bh_pointer;
	}
}

/*
 * queue_task_irq_off: put the bottom half handler "bh_pointer" on the list
 * "bh_list".  You may call this function only when interrupts are off.
 */
_INLINE_ void queue_task_irq_off(struct tq_struct *bh_pointer,
				 task_queue *bh_list)
{
	if (!(bh_pointer->sync & 1)) {
		bh_pointer->sync = 1;
		bh_pointer->next = *bh_list;
		*bh_list = bh_pointer;
	}
}


/*
 * queue_task: as queue_task_irq, but can be called from anywhere.
 */
 //将bh_pointer加入bh_list队列
_INLINE_ void queue_task(struct tq_struct *bh_pointer,
			   task_queue *bh_list)
{
	/* sync 初值为0，入队时原子的置1，以避免重复入队 */
	if (!set_bit(0,&bh_pointer->sync)) {
		unsigned long flags;
		save_flags(flags);
		//关中断 入队
		cli();
		bh_pointer->next = *bh_list;
		*bh_list = bh_pointer;
		restore_flags(flags);
	}
}

/*
 * Call all "bottom halfs" on a given list.
 */
 /*
	run_task_queue(task_queue *list)函数可用于启动list中挂接的所有task，可以手动调用，
	也可以挂接在bottom half向量表中启动。以run_task_queue()作为bh_base[nr]
	的函数指针，实际上就是扩充了每个bottom half的函数句柄数，而对于系统预定义的tq_timer
	和tq_immediate的确是分别挂接在TQUEUE_BH和IMMEDIATE_BH上（注意，TIMER_BH没有如此使用
	但TQUEUE_BH也是在do_timer()中启动的），从而可以用于扩充bottom half的个数。此时，不需
	要手工调用run_task_queue()（这原本就不合适），而只需调用mark_bh(IMMEDIATE_BH)，
	让bottom half机制在合适的时候调度它。
*/
// /* 在适当的时候手工启动list */ 
_INLINE_ void run_task_queue(task_queue *list)
{
	register struct tq_struct *save_p;
	register struct tq_struct *p;
	void *arg;
	void (*f) (void *);

	while(1) {
		//交换list和&tq_last的值，并返回list的值。这里置空了list
		//为什么下面遍历链表不需要关中断？因为这里交换指针的操作可能是原子性的
		//也就是说，将连表单独移了出来，单独在下面操作，系统中别的地方不会操作到p链表
		//但仍然可以操作list链表
		//这里之所以可以这样处理的原因是，这个链表可以一次性全部遍历完并销毁 并不会在遍历操作过程中
		//再添加删除元素
		p = xchg_ptr(list,&tq_last);
		//如果list为空
		if(p == &tq_last)
			break;

		//遍历并调用
		do {
			arg = p -> data;
			f = p -> routine;
			save_p = p -> next;
			p -> sync = 0;
			(*f)(arg);
			p = save_p;
		} while(p != &tq_last);
	}
}

#undef _INLINE_

#endif /* _LINUX_TQUEUE_H */
