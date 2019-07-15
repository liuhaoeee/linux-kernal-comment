#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

/*
 * DON'T CHANGE THESE!! Most of them are hardcoded into some assembly language
 * as well as being defined here.
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
 * The timers are:
 *
 * BLANK_TIMER		console screen-saver timer
 *
 * BEEP_TIMER		console beep timer
 *
 * RS_TIMER		timer for the RS-232 ports
 * 
 * HD_TIMER		harddisk timer
 *
 * HD_TIMER2		(atdisk2 patches)
 *
 * FLOPPY_TIMER		floppy disk timer (not used right now)
 * 
 * SCSI_TIMER		scsi.c timeout timer
 *
 * NET_TIMER		tcp/ip timeout timer
 *
 * COPRO_TIMER		387 timeout for buggy hardware..
 *
 * QIC02_TAPE_TIMER	timer for QIC-02 tape driver (it's not hardcoded)
 *
 * MCD_TIMER		Mitsumi CD-ROM Timer
 *
 */

#define BLANK_TIMER	0
#define BEEP_TIMER	1
#define RS_TIMER	2

#define HD_TIMER	16
#define FLOPPY_TIMER	17
#define SCSI_TIMER 	18
#define NET_TIMER	19
#define SOUND_TIMER	20
#define COPRO_TIMER	21

#define QIC02_TAPE_TIMER	22	/* hhb */
#define MCD_TIMER	23

#define HD_TIMER2	24

struct timer_struct {
	unsigned long expires;
	void (*fn)(void);
};

extern unsigned long timer_active;
extern struct timer_struct timer_table[32];

/*
 * This is completely separate from the above, and is the
 * "new and improved" way of handling timers more dynamically.
 * Hopefully efficient and general enough for most things.
 *
 * The "hardcoded" timers above are still useful for well-
 * defined problems, but the timer-list is probably better
 * when you need multiple outstanding timers or similar.
 *
 * The "data" field is in case you want to use the same
 * timeout function for several timeouts. You can use this
 * to distinguish between the different invocations.
 */
//数据结构timer_list来描述一个内核定时器
struct timer_list {
	struct timer_list *next;
	struct timer_list *prev;	//形成双向链表元素用来将多个定时器连接成一条双向循环队列
	unsigned long expires;	//指定定时器到期的时间，这个时间被表示成自系统启动以来的时钟滴答计数（也即时钟节拍数）
							//当一个定时器的expires值小于或等于jiffies变量时，我们就说这个定时器已经超时或到期了。
							//在初始化一个定时器后，通常把它的expires域设置成当前expires变量的当前值加上某个
							//时间间隔值（以时钟滴答次数计）
	unsigned long data;	//用作function函数的调用参数
	void (*function)(unsigned long);	//函数指针function：指向一个可执行函数。当定时器到期时，内核就执行function所指定的函数
};

extern void add_timer(struct timer_list * timer);
extern int  del_timer(struct timer_list * timer);

//初始化一个定时器
extern inline void init_timer(struct timer_list * timer)
{
	timer->next = NULL;
	timer->prev = NULL;
}

#endif
