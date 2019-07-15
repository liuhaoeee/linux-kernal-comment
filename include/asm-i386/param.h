#ifndef _ASMi386_PARAM_H
#define _ASMi386_PARAM_H
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
	时钟滴答的频率（HZ）：也即1秒时间内PIT所产生的时钟滴答次数。类似地
	这个值也是由PIT通道0的计数器初值决定的（反过来说，确定了时钟滴答的
	频率值后也就可以确定8254 PIT通道0的计数器初值）。Linux内核用宏HZ来
	表示时钟滴答的频率，而且在不同的平台上HZ有不同的定义值。对于ALPHA和
	IA62平台HZ的值是1024，对于SPARC、MIPS、ARM和i386等平台HZ的值都是100。
	该宏在i386平台上的定义如下（include/asm-i386/param.h）： 
	#ifndef HZ 
	#define HZ 100 
	#endif 
	根据HZ的值，我们也可以知道一次时钟滴答的具体时间间隔应该是（1000ms／HZ）＝10ms。 
*/
#ifndef HZ
#define HZ 100
#endif

#define EXEC_PAGESIZE	4096

#ifndef NGROUPS
#define NGROUPS		32
#endif

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#endif
