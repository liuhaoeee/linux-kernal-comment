#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

/*
 * 'kernel.h' contains some often-used function prototypes etc
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

#ifdef __KERNEL__

#include <stdarg.h>
#include <linux/linkage.h>

#define INT_MAX		((int)(~0U>>1))
#define UINT_MAX	(~0U)
#define LONG_MAX	((long)(~0UL>>1))
#define ULONG_MAX	(~0UL)

#define STACK_MAGIC	0xdeadbeef

#define	KERN_EMERG	"<0>"	/* system is unusable			*/
#define	KERN_ALERT	"<1>"	/* action must be taken immediately	*/
#define	KERN_CRIT	"<2>"	/* critical conditions			*/
#define	KERN_ERR	"<3>"	/* error conditions			*/
#define	KERN_WARNING	"<4>"	/* warning conditions			*/
#define	KERN_NOTICE	"<5>"	/* normal but significant condition	*/
#define	KERN_INFO	"<6>"	/* informational			*/
#define	KERN_DEBUG	"<7>"	/* debug-level messages			*/

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 5)
# define NORET_TYPE    __volatile__
# define ATTRIB_NORET  /**/
# define NORET_AND     /**/
#else
# define NORET_TYPE    /**/
# define ATTRIB_NORET  __attribute__((noreturn))
# define NORET_AND     noreturn,
#endif

extern void math_error(void);
NORET_TYPE void panic(const char * fmt, ...)
	__attribute__ ((NORET_AND format (printf, 1, 2)));
NORET_TYPE void do_exit(long error_code)
	ATTRIB_NORET;
extern unsigned long simple_strtoul(const char *,char **,unsigned int);
extern int sprintf(char * buf, const char * fmt, ...);
extern int vsprintf(char *buf, const char *, va_list);

extern int session_of_pgrp(int pgrp);

extern int kill_proc(int pid, int sig, int priv);
extern int kill_pg(int pgrp, int sig, int priv);
extern int kill_sl(int sess, int sig, int priv);

asmlinkage int printk(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

/*
 * This is defined as a macro, but at some point this might become a
 * real subroutine that sets a flag if it returns true (to do
 * BSD-style accounting where the process is flagged if it uses root
 * privs).  The implication of this is that you should do normal
 * permissions checks first, and check suser() last.
 *
 * "suser()" checks against the effective user id, while "fsuser()"
 * is used for file permission checking and checks against the fsuid..
 */
#define suser() (current->euid == 0)
#define fsuser() (current->fsuid == 0)

#endif /* __KERNEL__ */

#define SI_LOAD_SHIFT	16
struct sysinfo {
	long uptime;			/* Seconds since boot */ /* 启动到现在经过的时间 */
	unsigned long loads[3];		/* 1, 5, and 15 minute load averages */
	unsigned long totalram;		/* Total usable main memory size *//* 总的可用的内存大小 */
	unsigned long freeram;		/* Available memory size *//* 还未被使用的内存大小 */
	unsigned long sharedram;	/* Amount of shared memory *//* 共享的存储器的大小*/
	unsigned long bufferram;	/* Memory used by buffers *//* 缓冲区大小 */  
	unsigned long totalswap;	/* Total swap space size */ /* 交换区大小 */
	unsigned long freeswap;		/* swap space still available */ /* 还可用的交换区大小 */
	unsigned short procs;		/* Number of current processes *//* 当前进程数目 */
	char _f[22];			/* Pads structure to 64 bytes */ /* 64字节的补丁结构 */  
};

#endif
