/*
 *  linux/arch/i386/kernel/signal.c
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
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>

#include <asm/segment.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options);

/*
	信号处理流程：
	(1)建立帧
	为了适当地建立进程的用户态堆栈，handle_signal()函数或者调用setup_frame()或者调用setup_rt_frame()
	setup_frame()函数把一个叫做帧(frame)的数据结构推进用户态堆栈中，这个帧含有处理信号所需要的信息，
	并确保正确返回到handle_signal()函数setup_frame()函数把保存在内核态堆栈的段寄存器内容重新设置成它
	们的缺省值以后才结束。现在，信号处理程序所有需的信息就在用户态堆栈的顶部。

	(2)检查信号标志
	建立了用户态堆栈以后,handle_signal()函数检查与信号相关的标志值。如果信号没有设置SA_NODEFER标志，
	在sigaction表中sa_make字段对应的信号就必须在信号处理程序执行期间被阻塞，然后，handle_signal()返回
	到do_signal()，do_signal()也立即返回

	(3)开始执行信号处理程序
	do_signal()返回时，当前进程恢复它在用户态的执行。由于如前所述setup_frame()的准备，eip寄存器指向
	信号处理程序的第一条指令，而esp指向已推进用户态堆栈顶的帧的第一个内存单元。因此，信号处理程序被执行。

	(4)终止信号处理程序
	信号处理程序结束时，返回栈顶地址，该地址指向帧的pretcode字段所引用的vsyscall页中的代码。因此，信号
	编号(即帧的sig字段)被从栈中丢弃，然后调用sigreturn()系统调用或sys_rt_sigreturn()服务例程把来自扩展帧
	的进程硬件上下文拷贝到内核态堆栈，并通过从用户态堆栈删除扩展帧以恢复用户态堆栈原来的内容。

	(5)系统调用的重新执行
	内核并不总是能立即满足系统调用发出的请求，在这种情况发生时，把发出系统调用的进程置为TASK_INTERRUPTIBLE
	或TASK_UNINTERRUPTIBLE状态如果进程处于TASK_INTERRUPTIBLE状态，并且某个进程向它发送了一个信号，那么，内
	核不完成系统调用就把进程置成TASK_RUNNING状态。当切换回用户态时信号被传递给进程。当这种情况发生时，系统
	调用服务例程没有完成它的工作，但返回EINTR,ERESTARTNOHAND,ERESTART_RESTARTBLOCK,ERESTARTSYS或ERESTARTNOINTR
	错误码。实际上，这种情况下用户态进程获得的唯一错误码是EINTR,这个错误码表示系统调用还没有执行完。内核内部使
	用剩余的错误码来指定信号处理程序结束后是否自动重新执行系统调用。与未完成的系统调用相关的出错码及这些出错码
	对信号三种可能的操作产生的影响。

	Terminate:不会自动重新执行系统调用

	Reexecut:内核强迫用户态进程把系统调用号重新装入eax寄存器，并重新执行int$0x80指令或sysenter指令。进程意识不到这种重新执行，
	因此出错码也不传递给进程。

	Depends:只有被传递信号的SA_RESTART标志被设置，才重新执行系统调用；否则系统调用-EINTER出错码结束

	当传递信号时，内核在试图重新执行一个系统调用前必须确定进程确实发出过这个系统调用。这就是regs硬件上下文的orig_eaz字段起重要作用之处 

	a、重新执行被未捕获信号中断的系统调用

	如果信号被显式地忽略，或者如果它的缺省操作已被强制执行，do_signal()就分析系统调用的出错码，并如表11-11中所说明的那样决定
	是否重新自动执行未完成的系统调用。如果必须重新开始执行系统调用，那么do_signal()就修改regs硬件上下文，以便在进程返回到用户
	态时，eip指向int$0x80指令或sysenter指令，且eax包含系统调用号

	b、为所捕获的信号重新执行系统调用

	如果信号被捕获，那么handle_signal()分析出错码，也可能分析sigaction表的SA_RESTART标志来决定是否必须重新执行未完成的系统调用

	如果系统调用必须被重新开始执行，handle_signal()就与do_signal()完全一样地继续执行；否则，它向用户态进程返回一个出错码-ENTR

	4、与信号处理相关的系统调用

	在用户态运行的进程可以发送和接收信号。这意味着必须定义一组系统调用用来完成这些操作。遗憾的是，由于历史的原因，
	已经存在几个具有相同功能的系统调用，因此，其中一些系统调用从未被调用。例如：系统调用sys_sigaction()和sys_rt_sigaciton()
	几乎是相同的，因此C库中封装函数sigaction()调用sys_rt_sigaction()而不是sys_sigaction()。

*/

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
//该函数用于在接收到某个信号之前，临时用mask替换进程的信号掩码，并暂停进程执行，直到收到信号为止。
//也就是说，sigsuspend后，进程就挂在那里，等待着开放的信号的唤醒。系统在接收到信号后，
//马上就把现在的信号集还原为原来的，然后调用处理函数。
//sigsuspend返回后将恢复调用之前的的信号掩码。信号处理函数完成后，进程将继续执行。
//restart 百度：信号中断 与 慢系统调用
asmlinkage int sys_sigsuspend(int restart, unsigned long oldmask, unsigned long set)
{
	unsigned long mask;
	struct pt_regs * regs = (struct pt_regs *) &restart;

	mask = current->blocked;	//保存当前的屏蔽码，用于进程重收到信号后调用do_signal函数的参数
	current->blocked = set & _BLOCKABLE;	//临时将进程的信号屏蔽码替换为set(并除去不可屏蔽中断)
	regs->eax = -EINTR;		//函数返回值
	while (1) {
		current->state = TASK_INTERRUPTIBLE;	//将当前进程设为可中断状态，当进程收到一个非屏蔽信号时，将会被唤醒，重新运行
		schedule();		//调度重新
		if (do_signal(mask,regs))	//如果do_signal函数没有收到任何信号，则返回0
			return -EINTR;
	}
}
/*
*sigsuspend的整个原子操作过程为：
*(1) 设置新的mask阻塞当前进程；
*(2) 收到信号，恢复原先mask；
*(3) 调用该进程设置的信号处理函数；
*(4) 待信号处理函数返回后，sigsuspend返回。
*/

/*
 * This sets regs->esp even though we don't actually use sigstacks yet..
 */
/*
*信号处理程序执行完毕后，通过系统调用sigreturn（）重返系统空间 
*在系统调用sigreturn（）中从用户空间恢复"原始框架" 
*最后再返回到用户空间，继续执行原先的用户程序 
*/
/*
*当信号的用户态处理函数执行结束时，需要再次进入内核态，还原用户态堆栈，
*并且修改pt_regs中的pc，保证将来能够按照正常的方式返回用户态。我们知道
*进程要主动进入内核态只有通过系统调用，出发异常等方法，为此内核专门提供
*了一个系统调用sys_sigreturn()(还有一个sys_rt_sigreturn())
*/

/*调用sigreturn()或rt_sigrenturn()系统调用，相应的服务例程把正常程序的用户态堆栈
*硬件上下文拷贝到内核堆栈，并把用户态堆栈恢复到它原来的状态(通过调用restore_sigcongtext()).
*当这个系统调用结束时，普通进程就因此能恢复自己的执行
*/
asmlinkage int sys_sigreturn(unsigned long __unused)
{
#define COPY(x) regs->x = context.x
#define COPY_SEG(x) \
//若段描述符值表明其有效并且不是用户态权限 则终止进程 否则复制此值
if ((context.x & 0xfffc) && (context.x & 3) != 3) goto badframe; COPY(x);
#define COPY_SEG_STRICT(x) \
//若段描述符值表明其无效或者不是用户态权限 则终止进程 否则复制此值
if (!(context.x & 0xfffc) || (context.x & 3) != 3) goto badframe; COPY(x);
	struct sigcontext_struct context;
	struct pt_regs * regs;
	//在进入内核态时，内核态堆栈中保存了一个中断现场，也就是一个pt_regs结构，regs保存着系统调用或者中断时的现场
	//regs经过setup_frame函数的处理
	regs = (struct pt_regs *) &__unused;	//指向的是sys_sigreturn系统调用的内核堆栈(此时，内核堆栈中保存的应当还有别的信息，如中断的堆栈)
	if (verify_area(VERIFY_READ, (void *) regs->esp, sizeof(context)))
		goto badframe;
	//将保存的中断现场中的堆栈内容复制到context中去
	memcpy_fromfs(&context,(void *) regs->esp, sizeof(context));
	current->blocked = context.oldmask & _BLOCKABLE;	//复原进程信号位图
	//改变sys_sigreturn系统调用的内核堆栈，使之返回到上一层堆栈中，执行下一个信号处理句柄或返回用户空间继续执行
	COPY_SEG(ds);
	COPY_SEG(es);
	COPY_SEG(fs);
	COPY_SEG(gs);
	COPY_SEG_STRICT(ss);
	COPY_SEG_STRICT(cs);
	COPY(eip);
	COPY(ecx); COPY(edx);
	COPY(ebx);
	COPY(esp); COPY(ebp);
	COPY(edi); COPY(esi);
	regs->eflags &= ~0x40DD5;
	regs->eflags |= context.eflags & 0x40DD5;
	regs->orig_eax = -1;		/* disable syscall checks */
	return context.eax;
badframe:
	do_exit(SIGSEGV);
}

/*
 * Set up a signal frame... Make the stack look the way iBCS2 expects
 * it to look.
 */
 /*
 *iBCS2--Inter二进制兼容规范标准的版本2
 *FreeBSD也可以奉行SCO Unix的施行文件，这需要应用内核的ibcs2（ Intel binary compatibility system 2）选项。
 *这需要载入一个内核可加载模块，这需要应用root身份履行ibcs2号令以载入ibcs2模块。
 */

/*
用户提供的信号处理程序是在用户空间执行的，而且执行完毕以后还还要回到系统空间
*LINUX实现的机制如下：
*用户空间堆栈中为信号处理程序的执行预先创建一个框架，框架中包括一个作为局部量
*的数据结构，并把系统空间的"原始框架"保存在这个数据结构中 
*在信号处理程序中插入对系统调用sigreturn（）的调用 
*将系统空间堆栈中"原始框架"修改成为执行信号处理程序所需的框架 
*"返回"到用户空间，但是却执行信号处理程序 
*信号处理程序执行完毕后，通过系统调用sigreturn（）重返系统空间 
*在系统调用sigreturn（）中从用户空间恢复"原始框架" 
*最后再返回到用户空间，继续执行原先的用户程序 
*/

/*
*一个非阻塞的信号发送给一个进程。当中断或异常发生时，进程切换到内核态。
*正要返回到用户态前，内核执行do_signal()函数，
*这个函数又依次处理信号（通过调用handle_signal()）和建立用户态堆栈(通过调用setup_frame()或setup_rt_frame())
*当进程又切换到用户态时，因为信号处理程序的起始地址被强制放进程序计数器中，因此开始执行信号处理程序。
*当处理程序终止时，setup_frame()或setup_rt_frame()函数放在用户态堆栈中的返回代码就被执行。
*这个代码调用sigreturn()或rt_sigrenturn()系统调用，相应的服务例程把正常程序的用户态堆栈
*硬件上下文拷贝到内核堆栈，并把用户态堆栈恢复到它原来的状态(通过调用restore_sigcongtext()).
*当这个系统调用结束时，普通进程就因此能恢复自己的执行
*
*setup_frame()函数把一个叫做帧(frame)的数据结构推进用户态堆栈中，这个帧含有处理信号所需要的信息，
*并确保正确返回到handle_signal()函数,setup_frame()函数把保存在内核态堆栈的段寄存器内容重新设置成
*它们的缺省值以后才结束。现在，信号处理程序所有需的信息就在用户态堆栈的顶部。
*/
//struct pt_regs一种寄存器帧结构 该帧结构定义了各寄存器在系统调用时保存现场的堆栈结构。
//do_signal(unsigned long oldmask, struct pt_regs * regs)
//==>>setup_frame(sa,&frame,eip,regs,signr,oldmask);->frame = (unsigned long *) regs->esp;
//参数：fp--指向原用户态堆栈的指针  eip--函数返回后要执行的函数地址，这里指向下一个处理句柄或信号处理结束后返回用户态后所要执行的下一个地址
//regs--指向内核堆栈
void setup_frame(struct sigaction * sa, unsigned long ** fp, unsigned long eip,
	struct pt_regs * regs, int signr, unsigned long oldmask)
{
	unsigned long * frame;

#define __CODE ((unsigned long)(frame+24))
#define CODE(x) ((unsigned long *) ((x)+__CODE))
	frame = *fp;	//指向用户态堆栈指针
	if (regs->ss != USER_DS)
		frame = (unsigned long *) sa->sa_restorer;
	frame -= 32;	//堆栈向下扩展
	if (verify_area(VERIFY_WRITE,frame,32*4))
		do_exit(SIGSEGV);
/*  set up the "normal" stack seen by the signal handler (iBCS2) */
	//开始构建用户态堆栈(用户自定义信号处理程序所需的信息堆栈) 对应内核数据结构struct sigcontext_struct
	//参见linux\include\asm-i386\Signal.h
	put_fs_long(__CODE,frame);
	if (current->exec_domain && current->exec_domain->signal_invmap)
		put_fs_long(current->exec_domain->signal_invmap[signr], frame+1);
	else
		put_fs_long(signr, frame+1);
	put_fs_long(regs->gs, frame+2);
	put_fs_long(regs->fs, frame+3);
	put_fs_long(regs->es, frame+4);
	put_fs_long(regs->ds, frame+5);
	put_fs_long(regs->edi, frame+6);
	put_fs_long(regs->esi, frame+7);
	put_fs_long(regs->ebp, frame+8);
	put_fs_long((long)*fp, frame+9);	//上一层堆栈指针或原用户态堆栈指针
	put_fs_long(regs->ebx, frame+10);
	put_fs_long(regs->edx, frame+11);
	put_fs_long(regs->ecx, frame+12);
	put_fs_long(regs->eax, frame+13);
	put_fs_long(current->tss.trap_no, frame+14);
	put_fs_long(current->tss.error_code, frame+15);
	put_fs_long(eip, frame+16);		//下一个所要处理的信号处理句柄,或处理完信号后要返回的用户空间地址
	put_fs_long(regs->cs, frame+17);
	put_fs_long(regs->eflags, frame+18);
	put_fs_long(regs->esp, frame+19);
	put_fs_long(regs->ss, frame+20);
	put_fs_long(0,frame+21);		/* 387 state pointer - not implemented*/
/* non-iBCS2 extensions.. */
	put_fs_long(oldmask, frame+22);
	put_fs_long(current->tss.cr2, frame+23);
/* set up the return code... */
	//返回码放入栈中，待用户信号句柄返回时调用sigreturn进入内核态
	put_fs_long(0x0000b858, CODE(0));	/* popl %eax ; movl $,%eax */
	put_fs_long(0x80cd0000, CODE(4));	/* int $0x80 */
	put_fs_long(__NR_sigreturn, CODE(2));
	*fp = frame;
#undef __CODE
#undef CODE
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */
 //我们对进程收到的所有信号进行了两次遍历，一次是为了检查内核能处理的所有信号，另一次是当我们建立所有的用户态信号处理函数堆栈的时候
 /*
*系统调用的重新执行 
*在系统调用结束的时候，do_signal函数得以执行，它会与系统调用的退出标志一起来决定是否重新执行系统调用。
*do_sigal函数需要对两种情况进行处理，一种是进程对收到的信号设置了自己的信号处理程序，而另一种是进程没
*有设置收到的信号的信号处理程序，而且在处理完所有信号后，进程没有被停止或者结束掉。在后一种情况中，
*当系统调用退出的标志为ERESTARTNOHAND，ERESTARTSYS，ERESTARTNOINTR中的任意一个时，do_signal会使系统调用重新启动。
*而对前一种情况，只有系统调用的退出标志为ERESTARTNOINTR或者ERESTARTSYS并且信号处理标志也满足条件的时候系统调用才
*能重新启动。结合fs/pipe.c文件中的pipe_read函数（系统调用read内调用的函数）来理解
*/
asmlinkage int do_signal(unsigned long oldmask, struct pt_regs * regs)
{
	unsigned long mask = ~current->blocked;
	unsigned long handler_signal = 0;
	unsigned long *frame = NULL;
	unsigned long eip = 0;
	unsigned long signr;
	struct sigaction * sa;

	//依次遍历并复位当前进程中收到的非屏蔽信号，将当前进程收到的所有非屏蔽信号保存在handler_signal中
	while ((signr = current->signal & mask)) {
		__asm__("bsf %2,%1\n\t"		//汇编指令BSF(Bit Scan Forward )--扫描源操作数中的第一个被设置(就是1,1就叫被设置)的位,如果发现某一位被设置了,则设置ZF位并将第一个被设置位的索引装载到目的操作数中;如果没有发现被设置的位,则清除ZF.BSF正向扫描各个位(从第0位到第N位),BSR相反(从第N位到第0位)
			"btrl %1,%0"		//btrl(Bit Reset)，将current->signal中的第singal位复位 即将此次处理的信号位复位
			:"=m" (current->signal),"=r" (signr)
			:"1" (signr));
		sa = current->sigaction + signr;
		signr++;
		if ((current->flags & PF_PTRACED) && signr != SIGKILL) {
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current);
			schedule();
			if (!(signr = current->exit_code))
				continue;
			current->exit_code = 0;
			if (signr == SIGSTOP)
				continue;
			if (_S(signr) & current->blocked) {
				current->signal |= _S(signr);
				continue;
			}
			sa = current->sigaction + signr - 1;
		}
		if (sa->sa_handler == SIG_IGN) {
			if (signr != SIGCHLD)
				continue;
			/* check for SIGCHLD: it's special */
			while (sys_waitpid(-1,NULL,WNOHANG) > 0)
				/* nothing */;
			continue;
		}
		if (sa->sa_handler == SIG_DFL) {
			if (current->pid == 1)
				continue;
			switch (signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH:
				continue;

			case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (current->flags & PF_PTRACED)
					continue;
				current->state = TASK_STOPPED;
				current->exit_code = signr;
				if (!(current->p_pptr->sigaction[SIGCHLD-1].sa_flags & 
						SA_NOCLDSTOP))
					notify_parent(current);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGIOT: case SIGFPE: case SIGSEGV:
				if (current->binfmt && current->binfmt->core_dump) {
					if (current->binfmt->core_dump(signr, regs))
						signr |= 0x80;
				}
				/* fall through */
			default:
				current->signal |= _S(signr & 0x7f);
				do_exit(signr);
			}
		}
		/*
		 * OK, we're invoking a handler 执行到这里，说明信号处理句柄不是系统提供的。而是用户自定义的
		 */
		 //关于regs：regs是一个指向ret_from_systemcall过程中的堆栈的指针，此堆栈保存了内核态返回到用户态所需的参数以及系统调用的返回值(存在eax中)
		 //那么，regs->eax即为为信号中断的系统调用的返回值，这个返回值是怎么设置而来的呢？
		 //当regs->orig_eax >= 0时，说明是在某个系统调用的最后调用了本函数。参看linux\kernal\exit.c中的waitpid函数，
		 //以及在读管道函数fs\pipe.c（内含对被信号中断的系统调用的详细介绍）
		 //中，管道当前读数据但没有读到任何数据等情况下，进程收到了任何一个非阻塞的信号，则会以ERESTARTSYS返回值返回(存在eax中)
		 //它表示进程可以被中断，但是在继续执行之后会重新启动之前被中断的系统调用
		if (regs->orig_eax >= 0) {
			//若被中断的系统调用返回值为ERESTARTNOHAND或者返回值为ERESTARTSYS并且sa->sa_flags中的SA_RESTART没有置位，
			//则将其返回值修改为-EINTR,表示被中断的系统调用不会自动重新执行
			//ERESTARTNOINTR：要求系统调用被无条件重新执行。 
		//ERESTARTSYS： 要求系统调用重新执行，但允许信号处理程序根据自己的标志来做选择。 
		//ERESTARTNOHAND：在信号没有设置自己的信号处理函数时系统调用重新执行。 
			if (regs->eax == -ERESTARTNOHAND ||
			   (regs->eax == -ERESTARTSYS && !(sa->sa_flags & SA_RESTART)))
				regs->eax = -EINTR;
		}
		handler_signal |= 1 << (signr-1);
		mask &= ~sa->sa_mask;
	}
	//若不是系统调用而是其他中断执行过程调用本函数时，orig_eax为-1，因此，当orig_eax>=0时
	//说明是在某个系统调用中调用了本函数，即系统调用被信号机制中断
	//regs->orig_eax->进入系统调用之前的原eax值(系统调用号?)regs->eax->函数返回值
	//-ERESTARTNOINTR表示在处理完信号后要求返回到原系统调用中继续运行，即系统调用不会被中断
	if (regs->orig_eax >= 0 &&
	    (regs->eax == -ERESTARTNOHAND ||
	     regs->eax == -ERESTARTSYS ||
	     regs->eax == -ERESTARTNOINTR)) {
		regs->eax = regs->orig_eax;		//恢复进入系统调用之前的原eax值(系统调用号?)
		regs->eip -= 2;		//若系统调用被信号中断 且中断可被重新执行 则eip回调两个字节
	}
	if (!handler_signal)		/* no handler will be called - return 0 */
		return 0;
	eip = regs->eip;	//eip为返回用户态后要执行的代码指针，将此eip压入用户态信号处理堆栈的最底层的信号处理堆栈中，
						//当通过最后一次sys_sigreturn返回内核构建的内核返回堆栈中eip即为此eip，则系统处理信号完毕了，返回用户空间继续执行
	frame = (unsigned long *) regs->esp;
	signr = 1;
	sa = current->sigaction;
	//循环遍历所收到的每个信号，并在用户态堆栈中一次性建立处理信号所需要的信息堆栈
	for (mask = 1 ; mask ; sa++,signr++,mask += mask) {
		if (mask > handler_signal)
			break;
		if (!(mask & handler_signal))
			continue;
		//在用户态堆栈中建立当前被遍历的信号的处理函数所需信息的堆栈，若信号有N个，则建立连续的N层堆栈，
		//第N层需要通过sys_sigreturn系统调用返回内核态并改变sys_sigreturn系统调用的内核堆栈，使之返回到第N-1层(用户态)中
		//执行第N-1个信号处理函数
		setup_frame(sa,&frame,eip,regs,signr,oldmask);
		eip = (unsigned long) sa->sa_handler;
		if (sa->sa_flags & SA_ONESHOT)
			sa->sa_handler = NULL;
/* force a supervisor-mode page-in of the signal handler to reduce races */
		__asm__("testb $0,%%fs:%0": :"m" (*(char *) eip));
		regs->cs = USER_CS; regs->ss = USER_DS;
		regs->ds = USER_DS; regs->es = USER_DS;
		regs->gs = USER_DS; regs->fs = USER_DS;
		current->blocked |= sa->sa_mask;
		oldmask |= sa->sa_mask;
	}
	regs->esp = (unsigned long) frame;
	regs->eip = eip;		/* "return" to the first handler */
	regs->eflags &= ~TF_MASK;
	current->tss.trap_no = current->tss.error_code = 0;
	return 1;
}
