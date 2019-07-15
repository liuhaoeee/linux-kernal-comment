#ifndef _LINUX_SIGNAL_H
#define _LINUX_SIGNAL_H

typedef unsigned long sigset_t;		/* at least 32 bits */

#define _NSIG             32
#define NSIG		_NSIG
//参见PDF资料积累中的《Linux各种信号类型的意义》http://blog.csdn.net/trojanpizza/article/details/6656321
#define SIGHUP		 1	//进程组长退出时向所有会议成员发出的
#define SIGINT		 2	//由Interrupt Key产生，通常是CTRL+C或者DELETE。发送给所有ForeGround Group的进程
#define SIGQUIT		 3	//输入Quit Key的时候（CTRL+\）发送给所有Foreground Group的进程
#define SIGILL		 4	//非法指令异常
#define SIGTRAP		 5	//实现相关的硬件异常。一般是调试异常
#define SIGABRT		 6	//由调用abort函数产生，进程非正常退出
#define SIGIOT		 6	//实现相关的硬件异常，一般对应SIGABRT
#define SIGBUS		 7	//某种特定的硬件异常，通常由内存访问引起
#define SIGFPE		 8	//数学相关的异常，如被0除，浮点溢出，等等
#define SIGKILL		 9	//无法处理和忽略。中止某个进程
#define SIGUSR1		10	//用户自定义signal 1
#define SIGSEGV		11	//非法内存访问
#define SIGUSR2		12	//用户自定义signal 2
#define SIGPIPE		13	//在reader中止之后写Pipe的时候发送
#define SIGALRM		14	//用alarm函数设置的timer超时或setitimer函数设置的interval timer超时
#define SIGTERM		15	//请求中止进程，kill命令缺省发送
#define SIGSTKFLT	16	//Linux专用，数学协处理器的栈异常
#define SIGCHLD		17	//进程Terminate或Stop的时候，SIGCHLD会发送给它的父进程。缺省情况下该Signal会被忽略
#define SIGCONT		18	//当被stop的进程恢复运行的时候，自动发送
#define SIGSTOP		19	//中止进程。无法处理和忽略。
#define SIGTSTP		20	//表示终端挂起 一般是Ctrl+Z。发送给所有Foreground Group的进程
#define SIGTTIN		21	//表示后台进程读控制终端
#define SIGTTOU		22	//表示后台进程写控制终端
#define SIGURG		23	//当out-of-band data接收的时候可能发送
#define SIGXCPU		24	//当CPU时间限制超时的时候
#define SIGXFSZ		25	//进程超过文件大小限制
#define SIGVTALRM	26	//setitimer函数设置的Virtual Interval Timer超时的时候
#define SIGPROF		27	//Setitimer指定的Profiling Interval Timer所产生
#define SIGWINCH	28	//当Terminal的窗口大小改变的时候，发送给Foreground Group的所有进程
#define SIGIO		29	//异步IO事件
#define SIGPOLL		SIGIO	//当某个事件发送给Pollable Device的时候发送
/*
#define SIGLOST		29
*/
#define SIGPWR		30
#define	SIGUNUSED	31

/*
 * sa_flags values: SA_STACK is not currently supported, but will allow the
 * usage of signal stacks by using the (now obsolete) sa_restorer field in
 * the sigaction structure as a stack pointer. This is now possible due to
 * the changes in signal handling. LBT 010493.
 * SA_INTERRUPT is a no-op, but left due to historical reasons. Use the
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 */
//进程独立子进程产生的任何SIGSTOP,SIGTSIO,SIGTTIN和SIGTTOU信号
#define SA_NOCLDSTOP	1
#define SA_STACK	0x08000000
//让可重启的系统调用起作用
#define SA_RESTART	0x10000000
#define SA_INTERRUPT	0x20000000
//SA_NOMASK或SA_NODEFER---不避免在信号自己的处理器中接收信号本身
//忽略sa_restorrer元素:它已被废弃不再使用
#define SA_NOMASK	0x40000000
#define SA_ONESHOT	0x80000000

//包含其他要阻塞的信号
#define SIG_BLOCK          0	/* for blocking signals */
//包含要解除阻塞的信号
#define SIG_UNBLOCK        1	/* for unblocking signals */
//包含新的信号掩码
#define SIG_SETMASK        2	/* for setting the signal mask */

/* Type of a signal handler.  */
typedef void (*__sighandler_t)(int);

#define SIG_DFL	((__sighandler_t)0)	/* default signal handling */
#define SIG_IGN	((__sighandler_t)1)	/* ignore signal */
#define SIG_ERR	((__sighandler_t)-1)	/* error return from signal */

struct sigaction {
	__sighandler_t sa_handler;
	sigset_t sa_mask;
	unsigned long sa_flags;
	void (*sa_restorer)(void);
};

#ifdef __KERNEL__

#include <asm/signal.h>

#endif

#endif
