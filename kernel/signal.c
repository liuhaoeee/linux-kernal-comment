/*
 *  linux/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
 //Linux操作系统内核核心就是这样 曾经风云变幻 到最后却被千万行的代码淹没的无影无踪  
//幸好 他有时又会留下一点蛛丝马迹 引领我们穿越操作系统核心所有的秘密
/*
	The PDF documents about understanding Linux Kernel 1.2 is made by Liu Yihao
	这份学习Linux内核的PDF文档由刘以浩同学整理完成
	包含了作者在学习内核过程中搜集到的大量有关Linux
	内核的知识和自己的一些理解以及注释
	当然，最宝贵的还是这份原生的内核代码
	NOTE：如果在阅读过程中遇到什么问题，欢迎和我交流讨论
	Email：liuyihaolovem@163.com
*/

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/mm.h>

#include <asm/segment.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

//进程可以利用sigprocmask() 系统调用改变和检查自己的信号掩码的值
//其中，set是指向信号掩码的指针，进程的信号掩码是根据参数how的取值设置成set；
//第三个参数oset也是指向信号掩码的指针，它将包含以前的信号掩码值，使得在必要的时候，可以恢复它
//参数how的取值及含义如下：
//SIG_BOLCK set规定附加的阻塞信号；
//SIG_UNBOCK set规定一组不予阻塞的信号
//SIG_SETBLOCK set变成新进程的信号掩码
asmlinkage int sys_sigprocmask(int how, sigset_t *set, sigset_t *oset)
{
	sigset_t new_set, old_set = current->blocked;
	int error;

	if (set) {
		error = verify_area(VERIFY_READ, set, sizeof(sigset_t));
		if (error)
			return error;
		new_set = get_fs_long((unsigned long *) set) & _BLOCKABLE;
		switch (how) {
		case SIG_BLOCK:
			current->blocked |= new_set;
			break;
		case SIG_UNBLOCK:
			current->blocked &= ~new_set;
			break;
		case SIG_SETMASK:
			current->blocked = new_set;
			break;
		default:
			return -EINVAL;
		}
	}
	//若oset不为空 则将进程旧的屏蔽码保存在oset指向的内存单元中
	if (oset) {
		error = verify_area(VERIFY_WRITE, oset, sizeof(sigset_t));
		if (error)
			return error;
		put_fs_long(old_set, (unsigned long *) oset);
	}
	return 0;
}

//获取当前进程信号屏蔽码
asmlinkage int sys_sgetmask(void)
{
	return current->blocked;
}

//设置当前进程信号屏蔽码，并返回旧的屏蔽码
asmlinkage int sys_ssetmask(int newmask)
{
	int old=current->blocked;

	current->blocked = newmask & _BLOCKABLE;
	return old;
}

//进程可以用sigpending()系统调用来检查是否有挂起的阻塞信号
asmlinkage int sys_sigpending(sigset_t *set)
{
	int error;
	/* fill in "set" with signals pending but blocked. */
	error = verify_area(VERIFY_WRITE, set, 4);
	if (!error)
		put_fs_long(current->blocked & current->signal, (unsigned long *)set);
	return error;
}

/*
 * POSIX 3.3.1.3:
 *  "Setting a signal action to SIG_IGN for a signal that is pending
 *   shall cause the pending signal to be discarded, whether or not
 *   it is blocked" (but SIGCHLD is unspecified: linux leaves it alone).
 *
 *  "Setting a signal action to SIG_DFL for a signal that is pending
 *   and whose default action is to ignore the signal (for example,
 *   SIGCHLD), shall cause the pending signal to be discarded, whether
 *   or not it is blocked"
 *
 * Note the silly behaviour of SIGCHLD: SIG_IGN means that the signal
 * isn't actually ignored, but does automatic child reaping, while
 * SIG_DFL is explicitly said by POSIX to force the signal to be ignored..
 */
 //对指定的信号进行检查
 //可对照linux/arch/i386/kernel/signal.c下的signal.c文件中的do_signal函数中对信号句柄为SIG_IGN和SIG_DFL的处理
 //对于某些特定的信号，如果亲信号处理句柄是SIG_IGN或SIG_DFL，系统处理此信号中断的时候，将什么也不做，直接忽略
 //此函数就对此作出判断，若是这些特定的信号且其处理句柄是SIG_IGN或SIG_DFL，则将此信号删除
 //这样就不会对这些信号进行响应处理了，可提高系统效率
static void check_pending(int signum)
{
	struct sigaction *p;

	p = signum - 1 + current->sigaction;   //取指定信号的信号处理句柄
	if (p->sa_handler == SIG_IGN) {
		if (signum == SIGCHLD)
			return;
		//若信号处理句柄是"忽略"的且指定的信号不是SIGCHLD,则在信号位图中将此指定的信号删除，即
		//对于非SIGCHLD信号且其信号处理程序为SIG_IGN，则对此信号将不进行相应 
		current->signal &= ~_S(signum);		
		return;								
	}
	if (p->sa_handler == SIG_DFL) {
		if (signum != SIGCONT && signum != SIGCHLD && signum != SIGWINCH)
			return;
		current->signal &= ~_S(signum);		//同上的注释分析
		return;
	}	
}

//对指定的信号安装新的信号句柄
asmlinkage unsigned long sys_signal(int signum, void (*handler)(int))
{
	int err;
	struct sigaction tmp;

	if (signum<1 || signum>32)
		return -EINVAL;
	if (signum==SIGKILL || signum==SIGSTOP)
		return -EINVAL;
	if (handler != SIG_DFL && handler != SIG_IGN) {
		err = verify_area(VERIFY_READ, handler, 1);
		if (err)
			return err;
	}
	tmp.sa_handler = handler;
	tmp.sa_mask = 0;
	//SA_ONESHOT表示信号处理句柄只能调用一次，之后恢复系统默认的原处理句柄
	//SA_NOMASK表示在新的信号处理函数中，不要屏蔽此信号
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;	
	tmp.sa_restorer = NULL;					
	handler = current->sigaction[signum-1].sa_handler;
	current->sigaction[signum-1] = tmp;
	check_pending(signum);
	return (unsigned long) handler;
}

asmlinkage int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction new_sa, *p;

	if (signum<1 || signum>32)
		return -EINVAL;
	if (signum==SIGKILL || signum==SIGSTOP)
		return -EINVAL;
	p = signum - 1 + current->sigaction;
	if (action) {
		int err = verify_area(VERIFY_READ, action, sizeof(*action));
		if (err)
			return err;
		memcpy_fromfs(&new_sa, action, sizeof(struct sigaction));
		//如果信号处理句柄允许在处理过程中可以收到其他信号(包括自己)，则将新的信号处理句柄的信号屏蔽码设为0
		if (new_sa.sa_flags & SA_NOMASK)
			new_sa.sa_mask = 0;	
		else {
			new_sa.sa_mask |= _S(signum);
			new_sa.sa_mask &= _BLOCKABLE;	//否则屏蔽本信号
		}
		if (new_sa.sa_handler != SIG_DFL && new_sa.sa_handler != SIG_IGN) {
			err = verify_area(VERIFY_READ, new_sa.sa_handler, 1);
			if (err)
				return err;
		}
	}
	//将进程原信号处理句柄保存
	if (oldaction) {
		int err = verify_area(VERIFY_WRITE, oldaction, sizeof(*oldaction));
		if (err)
			return err;
		memcpy_tofs(oldaction, p, sizeof(struct sigaction));
	}
	//将进程处理句柄设为新的处理句柄 并检查此新句柄
	if (action) {
		*p = new_sa;
		check_pending(signum);
	}
	return 0;
}
