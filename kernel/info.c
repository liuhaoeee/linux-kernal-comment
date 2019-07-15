/*
 * linux/kernel/info.c
 *
 * Copyright (C) 1992 Darren Senn
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

/* This implements the sysinfo() system call */

#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/types.h>
#include <linux/mm.h>
/*
	struct sysinfo结构定义于linux/include/linux/kernel.h
*/
//获取系统总体统计信息
asmlinkage int sys_sysinfo(struct sysinfo *info)
{
	int error;
	struct sysinfo val;
	struct task_struct **p;

	error = verify_area(VERIFY_WRITE, info, sizeof(struct sysinfo));
	if (error)
		return error;
	memset((char *)&val, 0, sizeof(struct sysinfo));

	val.uptime = jiffies / HZ;

	//获取系统负载信息
	val.loads[0] = avenrun[0] << (SI_LOAD_SHIFT - FSHIFT);
	val.loads[1] = avenrun[1] << (SI_LOAD_SHIFT - FSHIFT);
	val.loads[2] = avenrun[2] << (SI_LOAD_SHIFT - FSHIFT);

	//统计当前进程数
	for (p = &LAST_TASK; p > &FIRST_TASK; p--)
		if (*p) val.procs++;

	//统计当前系统内存和交换页面信息
	si_meminfo(&val);
	si_swapinfo(&val);

	memcpy_tofs(info, &val, sizeof(struct sysinfo));
	return 0;
}
