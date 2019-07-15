/*
 *  linux/kernel/time.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  This file contains the interface functions for the various
 *  time related system calls: time, stime, gettimeofday, settimeofday,
 *			       adjtime
 */
/*
 * Modification history kernel/time.c
 * 
 * 02 Sep 93    Philip Gladstone
 *      Created file with time related functions from sched.c and adjtimex() 
 * 08 Oct 93    Torsten Duwe
 *      adjtime interface update and CMOS clock write code
 * 02 Jul 94	Alan Modra
 *	fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
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

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>

#include <asm/segment.h>
#include <asm/io.h>

#include <linux/mc146818rtc.h>
#include <linux/timex.h>

/* converts date to days since 1/1/1970
 * assumes year,mon,day in normal date format
 * ie. 1/1/1970 => year=1970, mon=1, day=1
 *
 * For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.
 *
 * This algorithm was first published by Gauss (I think).
 */
 //将时间转换为自1970年1月1日以来持续时间的秒数
static inline unsigned long mktime(unsigned int year, unsigned int mon,
	unsigned int day, unsigned int hour,
	unsigned int min, unsigned int sec)
{
	//1   2  3 4 5 6 7 8 9 10 11 12
    //11  12 1 2 3 4 5 6 7 8  9  10
	if (0 >= (int) (mon -= 2)) {	/* 1..12 -> 11,12,1..10 */
		mon += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}
	return (((
	    (unsigned long)(year/4 - year/100 + year/400 + 367*mon/12 + day) +
	      year*365 - 719499
	    )*24 + hour /* now have hours */
	   )*60 + min /* now have minutes */
	  )*60 + sec; /* finally seconds */
}

/*
	时间在操作系统中是个非常重要的概念。特别是在Linux，Unix这种多任务的操作系统中它更是
	作为主线索贯穿始终，之所以这样说，是因为无论进程调度（特别是时间片轮转算法）还是各种
	守护进程（也可以称为系统线程，如页表刷新的守护进程）都是根据时间运作的。可以说，时间
	是他们运行的基准。那么，在进程和线程没有真正启动之前，设定系统的时间就是一件理所当然
	的事情了。

	我们知道计算机中使用的时间一般情况下是与现实世界的时间一致的。当然，为了避开CIH，把时
	间跳过每月26号也是种明智的选择。不过如果你在银行或证交所工作，你恐怕就一定要让你计算机
	上的时钟与挂在墙上的钟表分秒不差了。还记得CMOS吗？计算机的时间标准也是存在那里面的。所以，
	我们首先通过get_cmos_time（）函数设定Linux的时间，不幸的是，CMOS提供的时间的最小单位是秒，
	这完全不能满足需要，否则CPU的频率1赫兹就够了。Linux要求系统中的时间精确到纳秒级，所以，我
	们把当前时间的纳秒设置为0。

	完成了当前时间的基准的设置，还要完成对8259的一号引脚上的8253（计时器）的中断响应程序的设置，
	即把它的偏移地址注册到中断向量表中去。
*/
void time_init(void)
{
	unsigned int year, mon, day, hour, min, sec;
	int i;

	/* checking for Update-In-Progress could be done more elegantly
	 * (using the "update finished"-interrupt for example), but that
	 * would require excessive testing. promise I'll do that when I find
	 * the time.			- Torsten
	 */
	/* read RTC exactly on falling edge of update flag */
/*
	在从RTC中读取时间时，由于RTC存在Update Cycle，因此软件发出读操作的时机是很重要的。
	对此，函数通过UIP标志位来解决这个问题：第一个for循环不停地读取RTC频率选择寄存器中
	的UIP标志位，并且只要读到UIP的值为1就马上退出这个for循环。第二个for循环同样不停地
	读取UIP标志位，但他只要一读到UIP的值为0就马上退出这个for循环。这两个for循环的目的
	就是要在软件逻辑上同步RTC的Update Cycle，显然第二个for循环最大可能需要2.228ms
	(TBUC+max(TUC)=244us+1984us=2.228ms)
	从第二个for循环退出后，RTC的Update Cycle已经结束。此时我们就已经把当前时间逻辑定准
	在RTC的当前一秒时间间隔内。也就是说，这是我们就可以开始从RTC寄存器中读取当前时间值。
	但是要注意，读操作应该保证在244us内完成（准确地说，读操作要在RTC的下一个更新周期开
	始之前完成，244us的限制是过分偏执的：－）。所以，函数接下来通过CMOS_READ()宏从RTC中
	依次读取秒、分钟、小时、日期、月份和年分。这里的do{}while(sec!=CMOS_READ(RTC_SECOND))
	循环就是用来确保上述6个读操作必须在下一个Update Cycle开始之前完成
*/
	//第一个for循环意在等待RTC硬件更新日历寄存器组
	//而第二个for循环意在等待RTC硬件完成更新日历寄存器组的操作
	for (i = 0 ; i < 1000000 ; i++)	/* may take up to 1 second... */
		//读取RTC的UIP标志（Update in Progress），为1表示RTC正在更新日历寄存器组中的值
		if (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP)
			break;
	for (i = 0 ; i < 1000000 ; i++)	/* must try at least 2.228 ms*/
		if (!(CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP))
			break;
	//好了，RTC硬件已经更新完毕，开始读取时间信息
	do { /* Isn't this overkill ? UIP above should guarantee consistency */
		sec = CMOS_READ(RTC_SECONDS);
		min = CMOS_READ(RTC_MINUTES);
		hour = CMOS_READ(RTC_HOURS);
		day = CMOS_READ(RTC_DAY_OF_MONTH);
		mon = CMOS_READ(RTC_MONTH);
		year = CMOS_READ(RTC_YEAR);
	//这里，while循环终止的条件可保证误差在一秒之内
	} while (sec != CMOS_READ(RTC_SECONDS));
	//读取RTC硬件的控制寄存器B的DM标志，如果寄存器保存的时间信息不是以二进制标志存储的
	//或者定义了RTC_ALWAYS_BCD宏，说明RTC硬件中的时间信息是以BCD码的形式存储的
	if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	  {
		//实时时钟RTC寄存器表示时间、日历的具体信息值都是以BCD格式来存储的
		//需要将其转换为二进制形式
	    BCD_TO_BIN(sec);
	    BCD_TO_BIN(min);
	    BCD_TO_BIN(hour);
	    BCD_TO_BIN(day);
	    BCD_TO_BIN(mon);
	    BCD_TO_BIN(year);
	  }
	if ((year += 1900) < 1970)
		year += 100;
	//xtime是从cmos电路中取得的时间，一般是从某一历史时刻开始到现在的时间
	//也就是为了取得我们操作系统上显示的日期。这个就是所谓的“实时时钟”，它的精确度是微秒
	//xtime变量在系统启动的时候,通过读rtc的值来初始化.在系统tick中断的时候,会更新
	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_usec = 0;
}
/* 
 * The timezone where the local system is located.  Used as a default by some
 * programs who obtain this value by using gettimeofday.
 */
struct timezone sys_tz = { 0, 0};

// 返回从1970 年1 月1 日00:00:00 GMT 开始计时的时间值（秒）。如果tloc不为null，则时间值
// 也存储在那里
asmlinkage int sys_time(long * tloc)
{
	int i, error;

	i = CURRENT_TIME;
	if (tloc) {
		error = verify_area(VERIFY_WRITE, tloc, 4);
		if (error)
			return error;
		put_fs_long(i,(unsigned long *)tloc);
	}
	return i;
}

//设置系统时间 参数tptr是一个指向unsigned long类型的指针，即设置的参数是系统自启动以来经过的秒数
asmlinkage int sys_stime(unsigned long * tptr)
{
	int error;
	unsigned long value;

	if (!suser())
		return -EPERM;
	//对用户空间进行读验证
	error = verify_area(VERIFY_READ, tptr, sizeof(*tptr));
	if (error)
		return error;
	//获取要设置的秒数
	value = get_fs_long(tptr);
	//关中断 因为要操作一些系统全局的共享变量
	cli();
	xtime.tv_sec = value;
	xtime.tv_usec = 0;
	time_status = TIME_BAD;
	time_maxerror = 0x70000000;
	time_esterror = 0x70000000;
	//设置完毕 开中断
	sti();
	return 0;
}

/* This function must be called with interrupts disabled 
 * It was inspired by Steve McCanne's microtime-i386 for BSD.  -- jrs
 * 
 * However, the pc-audio speaker driver changes the divisor so that
 * it gets interrupted rather more often - it loads 64 into the
 * counter rather than 11932! This has an adverse impact on
 * do_gettimeoffset() -- it stops working! What is also not
 * good is that the interval that our timer function gets called
 * is no longer 10.0002 msecs, but 9.9767 msec. To get around this
 * would require using a different timing source. Maybe someone
 * could use the RTC - I know that this can interrupt at frequencies
 * ranging from 8192Hz to 2Hz. If I had the energy, I'd somehow fix
 * it so that at startup, the timer code in sched.c would select
 * using either the RTC or the 8253 timer. The decision would be
 * based on whether there was any other device around that needed
 * to trample on the 8253. I'd set up the RTC to interrupt at 1024Hz,
 * and then do some jiggery to have a version of do_timer that 
 * advanced the clock by 1/1024 sec. Every time that reached over 1/100
 * of a second, then do all the old code. If the time was kept correct
 * then do_gettimeoffset could just return 0 - there is no low order
 * divider that can be accessed.
 *
 * Ideally, you would be able to use the RTC for the speaker driver,
 * but it appears that the speaker driver really needs interrupt more
 * often than every 120us or so.
 *
 * Anyway, this needs more thought....		pjsg (28 Aug 93)
 * 
 * If you are really that interested, you should be reading
 * comp.protocols.time.ntp!
 */
/*
	可编程间隔定时器PIT
	每个PC机中都有一个PIT，以通过IRQ0产生周期性的时钟中断信号。当前使用最普遍的是Intel 8254 PIT芯片
	它的I/O端口地址是0x40~0x43。
	Intel 8254 PIT有3个计时通道，每个通道都有其不同的用途：
	（1） 通道0用来负责更新系统时钟。每当一个时钟滴答过去时，它就会通过IRQ0向系统产生一次时钟中断。
	（2） 通道1通常用于控制DMAC对RAM的刷新。
	（3） 通道2被连接到PC机的扬声器，以产生方波信号。
	每个通道都有一个向下减小的计数器，8254 PIT的输入时钟信号的频率是1193181HZ，也即一秒钟输入
	1193181个clock-cycle。每输入一个clock-cycle其时间通道的计数器就向下减1，一直减到0值。
	因此对于通道0而言，当他的计数器减到0时，PIT就向系统产生一次时钟中断，表示一个时钟滴答已经过去了。
	当各通道的计数器减到0时，我们就说该通道处于“Terminal count”状态。
	通道计数器的最大值是10000h，所对应的时钟中断频率是1193181／（65536）＝18.2HZ，也就是说
	此时一秒钟之内将产生18.2次时钟中断。
*/
 //时钟滴答的时间间隔：Linux用全局变量tick来表示时钟滴答的时间间隔长度
#define TICK_SIZE tick

//返回从上次时间中断（已经得到处理的）的产生到本函数真正执行这段时间内，一共流逝了的延时时间
//精确到了微秒 感慨自己当年大一的时候，学习获取系统时间的方法，现在终于明白为什么这个函数可以精确到微秒
//而一般的函数只能精确到毫秒的原因了
//正是因为一般的函数只能获取系统时间，而系统时间的更新来自于时钟中断，但时钟中断是有间隔的
//只有在每次产生时钟中断请求，并且得到了系统的执行，系统时间才会更新，而且仅仅在毫秒的级别更新
static inline unsigned long do_gettimeoffset(void)
{
	int count;
	unsigned long offset = 0;

	/* timer count may underflow right here */
/*
			PIT控制寄存器的取值
	（1）bit［7：6］——Select Counter，选择对那个计数器进行操作。“00”表示选择Counter 0，
		“01”表示选择Counter 1，“10”表示选择Counter 2，“11”表示Read-Back Command（仅对于8254，对于8253无效）。
	（2）bit［5：4］——Read/Write/Latch格式位。“00”表示锁存（Latch）当前计数器的值；
		“01”只读写计数器的高字节（MSB）；“10”只读写计数器的低字节（LSB）；“11”表示先读写计数器的LSB，再读写MSB。
	（3）bit［3：1］——Mode bits，控制各通道的工作模式。“000”对应Mode 0；“001”对应Mode 1；
		“010”对应Mode 2；“011”对应Mode 3；“100”对应Mode 4；“101”对应Mode 5。
	（4）bit［0］——控制计数器的存储模式。0表示以二进制格式存储，1表示计数器中的值以BCD格式存储。
*/
	//向8254的控制寄存器（端口0x43）中写入值0x00，以便对通道0的计数器进行锁存
	//通道0用来负责更新系统时钟。每当一个时钟滴答过去时，它就会通过IRQ0向系统产生一次时钟中断。
	outb_p(0x00, 0x43);	/* latch the count ASAP */
	//通过端口0x40将通道0的计数器的当前值读到局部变量count中，并解锁i8253_lock
	//由于PIT计数器寄存器是16位的，而相应的端口却是8位的，所以需要分两次来读
	//至于是读取哪个计数器以及先读取高位字节还是低位字节，取决于PIT控制寄存器的值
	//即上一条语句，对PIT控制寄存器进行了设置
/*
		锁存计数器（Latch Counter）
	当控制寄存器中的bit［5：4］设置成0时，将把当前通道的计数器值锁存。
	此时通过I/O端口可以读到一个稳定的计数器值，因为计数器表面上已经停
	止向下计数（PIT芯片内部并没有停止向下计数）。NOTE！一旦发出了锁存
	命令，就要马上读计数器的值
*/
	count = inb_p(0x40);	/* read the latched count */
	count |= inb(0x40) << 8;
	/* we know probability of underflow is always MUCH less than 1% */
	//LATCH表示PIT将每隔多少个时钟周期产生一次时钟中断
	//如果count值很接近LATCH值，说明可能刚刚产生了还未处理的时钟中断请求
	//也即，系统时间还未更新，所以需要增加一个TICK_SIZE的时间
	if (count > (LATCH - LATCH/100)) {
		/* check for pending timer interrupt */
/*
	由于端口0x20和0xA0对读指令默认的是IRR寄存器，因此要向端口0x20／0xA0写入操作命令字OCW3＝0x0a，
	以切换到IRR寄存器。然后通过inb()函数读端口0x20／0xA0，以得到IRR寄存器的当前值，如果IRR＆irqmask非0
	就表示相应的中断请求正等待CPU的服务（通过一个INTA-cycle）
*/
		//通过写入OCW3操作字指定读8259A内部寄存器（低两位为10读IRR，为01读ISR）
		outb_p(0x0a, 0x20);	// /* back to the IRR register 以读取中断请求寄存器 */ 
		//如果IRR寄存器中的某个位被置1，就表示相应的中断请求正等待CPU的服务（通过一个INTA-cycle）
		//因此该中断请求也就处于pending状态。 
		//IRR中断寄存器第0位应该是时钟中断 若其置位，说明产生了还未进行处理的时钟中断请求
		if (inb(0x20) & 1)
			//TICK_SIZE表示时钟滴答的时间间隔长度,即tick
			offset = TICK_SIZE;
	}
/*
	从时间中断的产生到本函数真正执行这段时间内，一共流逝了（（LATCH-1）-count）个时钟周期，因此这个延时长度可以用如下公式计算： 
	delay_at_last_interrupt＝（（（LATCH-1）－count）÷LATCH）﹡TICK_SIZE 
	显然，上述公式的结果是个小数，应对其进行四舍五入，为此，Linux用下述表达式来计算delay_at_last_interrupt变量的值： 
	（（（LATCH-1）-count）＊TICK_SIZE＋LATCH/2）／LATCH 
	上述被除数表达式中的LATCH／2就是用来将结果向上圆整成整数的
*/
	count = ((LATCH-1) - count) * TICK_SIZE;
	count = (count + LATCH/2) / LATCH;
	return offset + count;
}

/*
 * This version of gettimeofday has near microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	*tv = xtime;
#if defined (__i386__) || defined (__mips__)
	tv->tv_usec += do_gettimeoffset();
	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
#endif /* !defined (__i386__) && !defined (__mips__) */
	restore_flags(flags);
}
//其参数tv是保存获取时间结果的结构体，参数tz用于保存时区结果
asmlinkage int sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	int error;

	if (tv) {
		struct timeval ktv;
		error = verify_area(VERIFY_WRITE, tv, sizeof *tv);
		if (error)
			return error;
		do_gettimeofday(&ktv);
		put_fs_long(ktv.tv_sec, (unsigned long *) &tv->tv_sec);
		put_fs_long(ktv.tv_usec, (unsigned long *) &tv->tv_usec);
	}
	if (tz) {
		error = verify_area(VERIFY_WRITE, tz, sizeof *tz);
		if (error)
			return error;
		put_fs_long(sys_tz.tz_minuteswest, (unsigned long *) tz);
		put_fs_long(sys_tz.tz_dsttime, ((unsigned long *) tz)+1);
	}
	return 0;
}

/*
 * Adjust the time obtained from the CMOS to be GMT time instead of
 * local time.
 * 
 * This is ugly, but preferable to the alternatives.  Otherwise we
 * would either need to write a program to do it in /etc/rc (and risk
 * confusion if the program gets run more than once; it would also be 
 * hard to make the program warp the clock precisely n hours)  or
 * compile in the timezone information into the kernel.  Bad, bad....
 *
 * XXX Currently does not adjust for daylight savings time.  May not
 * need to do anything, depending on how smart (dumb?) the BIOS
 * is.  Blast it all.... the best thing to do not depend on the CMOS
 * clock at all, but get the time via NTP or timed if you're on a 
 * network....				- TYT, 1/1/92
 */
inline static void warp_clock(void)
{
	cli();
	xtime.tv_sec += sys_tz.tz_minuteswest * 60;
	sti();
}

/*
 * The first time we set the timezone, we will warp the clock so that
 * it is ticking GMT time instead of local time.  Presumably, 
 * if someone is setting the timezone then we are running in an
 * environment where the programs understand about timezones.
 * This should be done at boot time in the /etc/rc script, as
 * soon as possible, so that the clock can be set right.  Otherwise,
 * various programs will get confused when the clock gets warped.
 */
 /*
	这个系统调用与gettimeofday（）刚好相反，它供用户设置当前时间以及当前时间信息。
	它也有两个参数：（1）参数指针tv，指向含有待设置时间信息的用户空间缓冲区；
	（2）参数指针tz，指向含有待设置时区信息的用户空间缓冲区
 */
asmlinkage int sys_settimeofday(struct timeval *tv, struct timezone *tz)
{
	static int	firsttime = 1;
	struct timeval	new_tv;
	struct timezone new_tz;

	if (!suser())
		return -EPERM;
	if (tv) {
		int error = verify_area(VERIFY_READ, tv, sizeof(*tv));
		if (error)
			return error;
		memcpy_fromfs(&new_tv, tv, sizeof(*tv));
	}
	if (tz) {
		int error = verify_area(VERIFY_READ, tz, sizeof(*tz));
		if (error)
			return error;
		memcpy_fromfs(&new_tz, tz, sizeof(*tz));
	}
	if (tz) {
		//如果tz有效，则用tz所指向的新时区信息更新全局变量sys_tz
		sys_tz = new_tz;
		//如果是第一次设置时区信息，则在tv指针为空的情况下调用wrap_clock()函数来调整xtime中的秒数值
		if (firsttime) {
			firsttime = 0;
			if (!tv)
				warp_clock();
		}
	}
	//如果参数tv指针有效，则根据tv所指向的新时间信息调用do_settimeofday()函数来更新内核的当前时间xtime
	if (tv) {
		cli();
		/* This is revolting. We need to set the xtime.tv_usec
		 * correctly. However, the value in this location is
		 * is value at the last tick.
		 * Discover what correction gettimeofday
		 * would have done, and then undo it!
		 */
		//
		new_tv.tv_usec -= do_gettimeoffset();

		//为了保证0<=tv_usec<1000000
		if (new_tv.tv_usec < 0) {
			new_tv.tv_usec += 1000000;
			new_tv.tv_sec--;
		}

		xtime = new_tv;
		time_status = TIME_BAD;
		time_maxerror = 0x70000000;
		time_esterror = 0x70000000;
		sti();
	}
	return 0;
}

/* adjtimex mainly allows reading (and writing, if superuser) of
 * kernel time-keeping variables. used by xntpd.
 */
 /*
	实际上，linux系统有两个时钟：一个是由主板电池驱动的“Real 
	Time Clock”也叫做RTC或者叫CMOS时钟，硬件时钟。当操作系统
	关机的时候，用这个来记录时间，但是对于运行的系统是不用这
	个时间的。另一个时间是 “System clock”也叫内核时钟或者软件
	时钟，是由软件根据时间中断来进行计数的，内核时钟在系统关机
	的情况下是不存在的，所以，当操作系统启动的时候，内核时钟是
	要读取RTC时间来进行时间同步（有些情况下，内核时钟也可以通过
	ntp服务器来读取时间）
	
	这两个时钟通常会有一些误差，所以长时间可以导致这两个时钟偏离
	的比较多，最简单的保持两个时间同步的方法是用软件测出他们之间的
	误差率，然后用软件进行修正。在每次重新启动系统的时候，系统都会
	用hwclock命令对时间进行同步。如果内核时钟在每一个时间中断都快或
	者慢的话，可以用adjtimex命令进行调整，使得RTC和内核时间走的快慢一致
	
	因此应该已经清楚，计算机是有两个时钟的，RTC(Real Time Clock)和系统时钟，
	RTC由电池驱动，始终工作，系统时钟只存在于系统启动后，系统时钟通常比较精确，
	可以精确到微妙(usec)，而RTC时间是始终存在的，可以长期稳定的运行
	
	adjtimex是用来显示或者修改linux内核的时间变量的工具，他提供了对与内核时间
	变量的直接访问功能，可以实现对于系统时间的漂移进行修正
	
	Linux 使用 David L. Mill 的时钟调整算法系统调用 adjtimex() 
	读取和可选地设置该算法的调整参数。它以一个指向结构体 timex 
	指针为参数，更新内核参数相应的值，并且通过相同的结构体来返
	回内核当前的值
 */
asmlinkage int sys_adjtimex(struct timex *txc_p)
{
    long ltemp, mtemp, save_adjust;
	int error;

	/* Local copy of parameter */
	struct timex txc;

	error = verify_area(VERIFY_WRITE, txc_p, sizeof(struct timex));
	if (error)
	  return error;

	/* Copy the user data space into the kernel copy
	 * structure. But bear in mind that the structures
	 * may change
	 */
	memcpy_fromfs(&txc, txc_p, sizeof(struct timex));

	/* In order to modify anything, you gotta be super-user! */
	if (txc.mode && !suser())
		return -EPERM;

	/* Now we validate the data before disabling interrupts
	 */
	//如果指定了要调整时间偏移值 则要验证新的偏移值的合法性
	if (txc.mode != ADJ_OFFSET_SINGLESHOT && (txc.mode & ADJ_OFFSET))
	  /* Microsec field limited to -131000 .. 131000 usecs */
	  if (txc.offset <= -(1 << (31 - SHIFT_UPDATE))
	      || txc.offset >= (1 << (31 - SHIFT_UPDATE)))
	    return -EINVAL;

	/* time_status must be in a fairly small range */
	//如果指定了要调整时钟的状态
	if (txc.mode & ADJ_STATUS)
	  //如果指定的新的时钟状态值不在有效范围内，则返回-EINVAL
	  if (txc.status < TIME_OK || txc.status > TIME_BAD)
	    return -EINVAL;

	/* if the quartz is off by more than 10% something is VERY wrong ! */
	//如果指定了要调整时钟滴答值
	if (txc.mode & ADJ_TICK)
	  //验证新的时钟滴答值的合法性
	  if (txc.tick < 900000/HZ || txc.tick > 1100000/HZ)
	    return -EINVAL;

	//关中断
	cli();

	/* Save for later - semantics of adjtime is to return old value */
	save_adjust = time_adjust;

	/* If there are input parameters, then process them */
	if (txc.mode)
	{
		//修正时钟状态
	    if (time_status == TIME_BAD)
			time_status = TIME_OK;

		//如果指定了要调整时钟的状态
	    if (txc.mode & ADJ_STATUS)
			time_status = txc.status;

		//如果指定了要调整时钟频率偏移
	    if (txc.mode & ADJ_FREQUENCY)
			time_freq = txc.frequency << (SHIFT_KF - 16);

		//最大错误值
	    if (txc.mode & ADJ_MAXERROR)
			time_maxerror = txc.maxerror;

		//估计的错误值 
	    if (txc.mode & ADJ_ESTERROR)
			time_esterror = txc.esterror;

	    if (txc.mode & ADJ_TIMECONST)
			time_constant = txc.time_constant;

		//时间偏移 
	    if (txc.mode & ADJ_OFFSET)
		  /* old-fashioned adjtime */
	      if (txc.mode == ADJ_OFFSET_SINGLESHOT)
		{
		  time_adjust = txc.offset;
		}
	      else /* XXX should give an error if other bits set */
		{
		  time_offset = txc.offset << SHIFT_UPDATE;
		  mtemp = xtime.tv_sec - time_reftime;
		  time_reftime = xtime.tv_sec;
		  if (mtemp > (MAXSEC+2) || mtemp < 0)
		    mtemp = 0;

		  if (txc.offset < 0)
		    time_freq -= (-txc.offset * mtemp) >>
		      (time_constant + time_constant);
		  else
		    time_freq += (txc.offset * mtemp) >>
		      (time_constant + time_constant);

		  ltemp = time_tolerance << SHIFT_KF;

		  if (time_freq > ltemp)
		    time_freq = ltemp;
		  else if (time_freq < -ltemp)
		    time_freq = -ltemp;
		}
		//时钟滴答值
	    if (txc.mode & ADJ_TICK)
	      tick = txc.tick;

	}
	txc.offset = save_adjust;
	txc.frequency = ((time_freq+1) >> (SHIFT_KF - 16));
	txc.maxerror = time_maxerror;
	txc.esterror = time_esterror;
	txc.status = time_status;
	txc.time_constant  = time_constant;
	txc.precision  = time_precision;
	txc.tolerance = time_tolerance;
	txc.time = xtime;
	txc.tick = tick;

	sti();

	memcpy_tofs(txc_p, &txc, sizeof(struct timex));
	return time_status;
}

//内核在需要时将时间与日期回写到RTC中
//该函数用来更新RTC中的时间，它仅有一个参数nowtime，是以秒数表示的当前时
int set_rtc_mmss(unsigned long nowtime)
{
  int retval = 0;
  int real_seconds, real_minutes, cmos_minutes;
  unsigned char save_control, save_freq_select;

  /*在RTC控制寄存器中设置SET标志位，以便通知RTC软件程序随后马上将要更新它的时间与日期*/
  
  //先把RTC_CONTROL寄存器的当前值读到变量save_control中
  save_control = CMOS_READ(RTC_CONTROL); /* tell the clock it's being set */
  //然后再把值（save_control | RTC_SET）回写到寄存器RTC_CONTROL中。
  CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);

  /*通过RTC_FREQ_SELECT寄存器中bit［6：4］重启RTC芯片内部的除法器*/
  
  //先把RTC_FREQ_SELECT寄存器的当前值读到变量save_freq_select中
  save_freq_select = CMOS_READ(RTC_FREQ_SELECT); /* stop and reset prescaler */
  //然后再把值（save_freq_select ｜ RTC_DIV_RESET2）回写到RTC_FREQ_SELECT寄存器中
  CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

  //将RTC_MINUTES寄存器的当前值读到变量cmos_minutes中
  cmos_minutes = CMOS_READ(RTC_MINUTES);
  //并根据需要将它从BCD格式转化为二进制格式
  if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
    BCD_TO_BIN(cmos_minutes);

  /* since we're only adjusting minutes and seconds,
   * don't interfere with hour overflow. This avoids
   * messing with unknown time zones but requires your
   * RTC not to be off by more than 15 minutes
   */
   /*
   *从nowtime参数中得到当前时间的秒数和分钟数。分别保存到real_seconds和real_minutes变量。
   *注意，这里对于半小时区的情况要修正分钟数real_minutes的值
   */
  real_seconds = nowtime % 60;
  real_minutes = nowtime / 60;
  if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
    real_minutes += 30;		/* correct for half hour time zone */
  real_minutes %= 60;

  /*
  *在real_minutes与RTC_MINUTES寄存器的原值cmos_minutes二者相差不超过30分钟的情况下
  *将real_seconds和real_minutes所表示的时间值写到RTC的秒寄存器和分钟寄存器中。当然
  *在回写之前要记得把二进制转换为BCD格式
  */
  if (abs(real_minutes - cmos_minutes) < 30)
    {
      if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	{
	  BIN_TO_BCD(real_seconds);
	  BIN_TO_BCD(real_minutes);
	}
      CMOS_WRITE(real_seconds,RTC_SECONDS);
      CMOS_WRITE(real_minutes,RTC_MINUTES);
    }
  else
    retval = -1;

  /*
  *最后，恢复RTC_CONTROL寄存器和RTC_FREQ_SELECT寄存器原来的值。
  *这二者的先后次序是：先恢复RTC_CONTROL寄存器，再恢复RTC_FREQ_SELECT寄存器。
  */
  CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);
  CMOS_WRITE(save_control, RTC_CONTROL);
  return retval;
}
