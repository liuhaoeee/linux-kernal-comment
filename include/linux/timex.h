/*****************************************************************************
 *                                                                           *
 * Copyright (c) David L. Mills 1993                                         *
 *                                                                           *
 * Permission to use, copy, modify, and distribute this software and its     *
 * documentation for any purpose and without fee is hereby granted, provided *
 * that the above copyright notice appears in all copies and that both the   *
 * copyright notice and this permission notice appear in supporting          *
 * documentation, and that the name University of Delaware not be used in    *
 * advertising or publicity pertaining to distribution of the software       *
 * without specific, written prior permission.  The University of Delaware   *
 * makes no representations about the suitability this software for any      *
 * purpose.  It is provided "as is" without express or implied warranty.     *
 *                                                                           *
 *****************************************************************************/
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
 * Modification history timex.h
 * 
 * 17 Sep 93    David L. Mills
 *      Created file $NTP/include/sys/timex.h
 * 07 Oct 93    Torsten Duwe
 *      Derived linux/timex.h
 */
#ifndef _LINUX_TIMEX_H
#define _LINUX_TIMEX_H

/*
 * The following defines establish the engineering parameters of the PLL
 * model. The HZ variable establishes the timer interrupt frequency, 100 Hz 
 * for the SunOS kernel, 256 Hz for the Ultrix kernel and 1024 Hz for the
 * OSF/1 kernel. The SHIFT_HZ define expresses the same value as the
 * nearest power of two in order to avoid hardware multiply operations.
 */
#define SHIFT_HZ 7		/* log2(HZ) */

/*
 * The SHIFT_KG and SHIFT_KF defines establish the damping of the PLL
 * and are chosen by analysis for a slightly underdamped convergence
 * characteristic. The MAXTC define establishes the maximum time constant
 * of the PLL. With the parameters given and the default time constant of
 * zero, the PLL will converge in about 15 minutes.
 */
#define SHIFT_KG 8		/* shift for phase increment */
#define SHIFT_KF 20		/* shift for frequency increment */
#define MAXTC 6			/* maximum time constant (shift) */

/*
 * The SHIFT_SCALE define establishes the decimal point of the time_phase
 * variable which serves as a an extension to the low-order bits of the
 * system clock variable. The SHIFT_UPDATE define establishes the decimal
 * point of the time_offset variable which represents the current offset
 * with respect to standard time. The FINEUSEC define represents 1 usec in
 * scaled units.
 */
#define SHIFT_SCALE 24		/* shift for phase scale factor */
#define SHIFT_UPDATE (SHIFT_KG + MAXTC) /* shift for offset scale factor */
#define FINEUSEC (1 << SHIFT_SCALE) /* 1 us in scaled units */

#define MAXPHASE 128000         /* max phase error (us) */
#define MAXFREQ 100             /* max frequency error (ppm) */
#define MINSEC 16               /* min interval between updates (s) */
#define MAXSEC 1200             /* max interval between updates (s) */
/*
	时钟周期（clock cycle）的频率：8253／8254 PIT的本质就是对由晶体振荡器产生的时钟周期进行计数
	晶体振荡器在1秒时间内产生的时钟脉冲个数就是时钟周期的频率。Linux用宏CLOCK_TICK_RATE来表示
	8254 PIT的输入时钟脉冲的频率（在PC机中这个值通常是1193180HZ）
*/
#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */
#define CLOCK_TICK_FACTOR	20	/* Factor of both 1000000 and CLOCK_TICK_RATE */
/*
	宏LATCH：Linux用宏LATCH来定义要写到PIT通道0的计数器中的值，它表示PIT将每隔多少个时钟周期产生一次时钟中断。
	显然LATCH应该由下列公式计算： 
	LATCH＝（1秒之内的时钟周期个数）÷（1秒之内的时钟中断次数）＝（CLOCK_TICK_RATE）÷（HZ） 
	类似地，上述公式的结果可能会是个小数，应该对其进行四舍五入。所以，Linux将LATCH定义为（include/linux/timex.h）： 
	/* LATCH is used in the interval timer and ftape setup.  
	#define LATCH ((CLOCK_TICK_RATE + HZ/2) / HZ) // For divider 
	类似地，被除数表达式中的HZ／2也是用来将LATCH向上圆整成一个整数。 
*/
#define LATCH  ((CLOCK_TICK_RATE + HZ/2) / HZ)	/* For divider */

#define FINETUNE (((((LATCH * HZ - CLOCK_TICK_RATE) << SHIFT_HZ) * \
	(1000000/CLOCK_TICK_FACTOR) / (CLOCK_TICK_RATE/CLOCK_TICK_FACTOR)) \
		<< (SHIFT_SCALE-SHIFT_HZ)) / HZ)

/*
 * syscall interface - used (mainly by NTP daemon)
 * to discipline kernel clock oscillator
 */
struct timex {
	int mode;		/* mode selector */ /* 模式选择符 */
	long offset;		/* time offset (usec) */ /* 时间偏移 (微秒) */
	long frequency;		/* frequency offset (scaled ppm) *//* 频率偏移 (由 ppm 缩放) */
	long maxerror;		/* maximum error (usec) */ /* 最大错误 (微秒) */
	long esterror;		/* estimated error (usec) */ /* 估计的错误 (微秒) */
	int status;		/* clock command/status *//* 时钟 命令/状态 */
	long time_constant;	/* pll time constant */
	long precision;		/* clock precision (usec) (read only) */
	long tolerance;		/* clock frequency tolerance (ppm)
				 * (read only)
				 */
	struct timeval time;	/* (read only) */
	long tick;		/* (modified) usecs between clock ticks *//* 时钟滴答之间的微秒 */
};

/*
 * Mode codes (timex.mode) 
 */
 //modes 域用来指定哪个参数用于设置，如果需要的话。它可能包含下面值的位-或的组合：
#define ADJ_OFFSET		0x0001	/* time offset *///时间偏移 
#define ADJ_FREQUENCY		0x0002	/* frequency offset *///频率偏移 
#define ADJ_MAXERROR		0x0004	/* maximum time error *///最大错误值 
#define ADJ_ESTERROR		0x0008	/* estimated time error *///估计的错误值 
#define ADJ_STATUS		0x0010	/* clock status *///时钟状态 
#define ADJ_TIMECONST		0x0020	/* pll time constant */
#define ADJ_TICK		0x4000	/* tick value *///时钟滴答值
#define ADJ_OFFSET_SINGLESHOT	0x8001	/* old-fashioned adjtime */

/*
 * Clock command/status codes (timex.status)
 */
 //成功时，adjtimex() 返回时钟状态：
#define TIME_OK		0	/* clock synchronized */// 时钟已同步
#define TIME_INS	1	/* insert leap second */// 插入调整值
#define TIME_DEL	2	/* delete leap second */// 删除调整值 
#define TIME_OOP	3	/* leap second in progress *///调整正进行
#define TIME_BAD	4	/* clock not synchronized *///时钟没有同步

#ifdef __KERNEL__
/*
 * kernel variables
 */
extern long tick;                      /* timer interrupt period */
extern int tickadj;			/* amount of adjustment per tick */

/*
 * phase-lock loop variables
 */
extern int time_status;		/* clock synchronization status */
extern long time_offset;	/* time adjustment (us) */
extern long time_constant;	/* pll time constant */
extern long time_tolerance;	/* frequency tolerance (ppm) */
extern long time_precision;	/* clock precision (us) */
extern long time_maxerror;	/* maximum error */
extern long time_esterror;	/* estimated error */
extern long time_phase;		/* phase offset (scaled us) */
extern long time_freq;		/* frequency offset (scaled ppm) */
extern long time_adj;		/* tick adjust (scaled 1 / HZ) */
extern long time_reftime;	/* time at last adjustment (s) */

extern long time_adjust;	/* The amount of adjtime left */
#endif /* KERNEL */

#endif /* LINUX_TIMEX_H */
