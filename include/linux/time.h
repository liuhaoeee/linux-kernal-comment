#ifndef _LINUX_TIME_H
#define _LINUX_TIME_H

struct timeval {
	long	tv_sec;		/* seconds *///表示当前时间距UNIX时间基准的秒数值
	long	tv_usec;	/* microseconds *///表示一秒之内的微秒值，且1000000>tv_usec>＝0
};

struct timezone {
	int	tz_minuteswest;	/* minutes west of Greenwich *////*和格林威治时间差了多少分钟*/
	int	tz_dsttime;	/* type of dst correction *//*DST（夏令时）时间的修正方式*/
};

#define NFDBITS			__NFDBITS

#ifdef __KERNEL__
void do_gettimeofday(struct timeval *tv);
#include <asm/bitops.h>
#include <linux/string.h>
#define FD_SETSIZE		__FD_SETSIZE
#define FD_SET(fd,fdsetp)	set_bit(fd,fdsetp)
#define FD_CLR(fd,fdsetp)	clear_bit(fd,fdsetp)
#define FD_ISSET(fd,fdsetp)	(0 != test_bit(fd,fdsetp))
#define FD_ZERO(fdsetp)		memset(fdsetp, 0, sizeof(struct fd_set))
#else
#define FD_SETSIZE		__FD_SETSIZE
#define FD_SET(fd,fdsetp)	__FD_SET(fd,fdsetp)
#define FD_CLR(fd,fdsetp)	__FD_CLR(fd,fdsetp)
#define FD_ISSET(fd,fdsetp)	__FD_ISSET(fd,fdsetp)
#define FD_ZERO(fdsetp)		__FD_ZERO(fdsetp)
#endif

/*
 * Names of the interval timers, and structure
 * defining a timer setting.
 */
 //为三种进程间隔定时器定义了索引标识
#define	ITIMER_REAL	0	//真实间隔定时器（ITIMER_REAL）:这种间隔定时器在启动后，不管进程是否运行，每个时钟滴答都将其间隔计数器减1。
#define	ITIMER_VIRTUAL	1	//虚拟间隔定时器ITIMER_VIRT:也称为进程的用户态间隔定时器。当虚拟间隔定时器启动后，只有当进程在用户态下运行时，一次时钟滴答才能使间隔计数器当前值it_virt_value减1
#define	ITIMER_PROF	2	//PROF间隔定时器ITIMER_PROF:当一个进程的PROF间隔定时器启动后，则只要该进程处于运行中，而不管是在用户态或核心态下执行，每个时钟滴答都使间隔计数器it_prof_value值减1

/*
	虽然，在内核中间隔定时器的间隔计数器是以时钟滴答次数为单位，但是让用户以时钟滴答为
	单位来指定间隔定时器的间隔计数器的初值显然是不太方便的，因为用户习惯的时间单位是秒、
	毫秒或微秒等。所以Linux定义了数据结构 itimerval来让用户以秒或微秒为单位指定间隔定时
	器的时间间隔值。
	其中，it_interval成员表示间隔计数器的初始值，而it_value成员表示间隔计数器的当前值。
	这两个成员都是timeval结构类型的变量，因此其精度可以达到微秒级
*/
struct	itimerval {
	struct	timeval it_interval;	/* timer interval */
	struct	timeval it_value;	/* current value */
};

#endif
