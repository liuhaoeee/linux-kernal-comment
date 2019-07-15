/*
 *  linux/fs/exec.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * #!-checking implemented by tytso.
 */

/*
 * Demand-loading implemented 01.12.91 - no need to read anything but
 * the header into memory. The inode of the executable is put into
 * "current->executable", and page faults do the actual loading. Clean.
 *
 * Once more I can proudly say that linux stood up to being changed: it
 * was less than 2 hours work to get demand-loading completely implemented.
 *
 * Demand loading changed July 1993 by Eric Youngdale.   Use mmap instead,
 * current->executable is only used by the procfs.  This allows a dispatch
 * table to check for several different types  of binary formats.  We keep
 * trying until we recognize the file or we run out of supported binary
 * formats. 
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
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/malloc.h>
#include <linux/binfmts.h>
#include <linux/personality.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgtable.h>

#include <linux/config.h>

asmlinkage int sys_exit(int exit_code);
asmlinkage int sys_brk(unsigned long);

static int load_aout_binary(struct linux_binprm *, struct pt_regs * regs);
static int load_aout_library(int fd);
static int aout_core_dump(long signr, struct pt_regs * regs);

extern void dump_thread(struct pt_regs *, struct user *);

/*
 * Here are the actual binaries that will be accepted:
 * add more with "register_binfmt()"..
 */
extern struct linux_binfmt elf_format;

static struct linux_binfmt aout_format = {
#ifndef CONFIG_BINFMT_ELF
 	NULL, NULL, load_aout_binary, load_aout_library, aout_core_dump
#else
 	&elf_format, NULL, load_aout_binary, load_aout_library, aout_core_dump
#endif
};

static struct linux_binfmt *formats = &aout_format;

//每一种可执行文件类型被添加进内核时，都通过函数register_binfmt
//将该类可执行文件对应的linux_binfmt结构件注册到内核
//可执行文件的注册就是将其对应的linux_binfmt结构链接到全局链表formats中
int register_binfmt(struct linux_binfmt * fmt)
{
	//tmp指向可执行文件类型链表表头
	struct linux_binfmt ** tmp = &formats;

	//如果fmt为空
	if (!fmt)
		return -EINVAL;
	//如果fmt->next不为空
	if (fmt->next)
		return -EBUSY;
	//遍历当前系统支持的可执行文件链表
	while (*tmp) {
		//若要添加到链表的可执行文件类型已经存在于链表中
		if (fmt == *tmp)
			return -EBUSY;
		tmp = &(*tmp)->next;
	}
	//将fmt加入到可执行文件链表中
	*tmp = fmt;
	return 0;	
}

int unregister_binfmt(struct linux_binfmt * fmt)
{
	//tmp指向可执行文件类型链表表头
	struct linux_binfmt ** tmp = &formats;

	//遍历可执行文件类型链表，找到要注销的可执行文件类型节点
	while (*tmp) {
		if (fmt == *tmp) {
			*tmp = fmt->next;
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	return -EINVAL;
}

int open_inode(struct inode * inode, int mode)
{
	int error, fd;
	struct file *f, **fpp;

	if (!inode->i_op || !inode->i_op->default_file_ops)
		return -EINVAL;
	//获取一个空的file结构
	f = get_empty_filp();
	if (!f)
		return -ENFILE;
	fd = 0;
	//fpp指向当前进程文件描述符数组头
	fpp = current->files->fd;
	//遍历当前进程的文件描述符数组，寻找一个空闲项
	for (;;) {
		//如果此项为空
		if (!*fpp)
			break;
		//如果fd大于进程所能打开的最多的文件数（即文件描述符数组容量）
		//说明寻找空项失败，返回-EMFILE
		if (++fd >= NR_OPEN) {
			f->f_count--;
			return -EMFILE;
		}
		//遍历数组下一项
		fpp++;
	}
	//执行到这里，说明寻找到一项空闲的文件描述符
	//则将f填充到此空闲的文件描述符中
	*fpp = f;
	//初始化此file对象
	f->f_flags = mode;
	f->f_mode = (mode+1) & O_ACCMODE;
/*
	将file对象关联到实际的具体的文件系统的i节点上
	从这里可略俯瞰Linux虚拟文件系统
	在VFS顶层，用户直接操作的都是file文件对象
	无论实际的文件系统是什么类型的，用户直接操作的都是一样的
	file结构，没有什么不同
	那么，file文件对象是如何隐藏实际的各种文件系统之间的细节差异
	做到文件系统的统一的呢？
	答案是通过inode节点
	在VFS顶层，系统定义了统一的file结构和file结构的操作函数集合
	而不同的具体的文件系统定义了不同的inode结构以及相应的inode的操作函数集合
	在file结构中包含了一个其对应的inode节点的i节点变量
	在打开文件的时候，系统通过路径名找到文件i节点，此过程不涉及file对象
	file结构中包含自身的操作函数集f_op，通过具体文件系统的i节点中保存的、
	具体文件系统定义的file操作函数集来初始化
	
	总的来说，就是file对象其实是通过具体文件系统的i节点来初始化的
	所以通过统一的file对象可操作不同的文件系统，体现了面向对象的思想
	
	我想，VFS提供的文件系统的抽象其实抽象的是Linux环境下的文件系统应该表现的行为
	是将各种不同的文件系统归并到一个统一的模型之中。
	比如，msdos的文件系统可能没有所属组的概念，但是Linux的VFS接管msdos文件系统中
	的文件，并在VFS层实现文件“组”的概念，并实施一个统一的安全机制
	由于用户操作的是Linux操作系统环境下的文件，所以看到的是其VFS层提供的一个抽象
	其操作也受VFS的直接限制。简洁地说，VFS就是一个悬空的、真实的文件系统，但其必须依靠具体的文件系统
	才能存在，离开了具体的文件系统，VFS是不能独立存在的，VFS只是让用户以linux的方式打开和访问文件
	比如，msdos的文件系统，我们在linux下打开和访问，看到的都不是原来的msdos文件的访问形式
	VFS应该是文件系统最外层表面和最直接的操作接口，而具体的文件系统负责实现物理磁盘的管理方式
	
	VFS提供了统一的机制、框架
*/
	f->f_inode = inode;	//将VFS层的file结构关联到一个具体的文件系统i节点上
	f->f_pos = 0;	//和具体文件系统无关 也可以说是所有文件系统的一个共性
	f->f_reada = 0;	//和具体文件系统无关 也可以说是所有文件系统的一个共性
	f->f_op = inode->i_op->default_file_ops;	//file的操作函数集，通过具体文件系统i节点中保存的
												//具体文件系统定义的file操作函数集来初始化
	if (f->f_op->open) {
		error = f->f_op->open(inode,f);	//file对象可在具体文件系统中再进行进一步的初始化
		if (error) {
			*fpp = NULL;
			f->f_count--;	//减少file对象的引用计数
			return error;
		}
	}
	inode->i_count++;	//增加i节点的引用计数
	return fd;
}

/*
 * These are the only things you should do on a core-file: use only these
 * macros to write out all the necessary info.
 */
#define DUMP_WRITE(addr,nr) \
while (file.f_op->write(inode,&file,(char *)(addr),(nr)) != (nr)) goto close_coredump

#define DUMP_SEEK(offset) \
if (file.f_op->lseek) { \
	if (file.f_op->lseek(inode,&file,(offset),0) != (offset)) \
 		goto close_coredump; \
} else file.f_pos = (offset)		

/*
 * Routine writes a core dump image in the current directory.
 * Currently only a stub-function.
 *
 * Note that setuid/setgid files won't make a core-dump if the uid/gid
 * changed due to the set[u|g]id. It's enforced by the "current->dumpable"
 * field, which also makes sure the core-dumps won't be recursive（递归） if the
 * dumping of the process results in another error..
 */
static int aout_core_dump(long signr, struct pt_regs * regs)
{
	struct inode * inode = NULL;
	struct file file;
	unsigned short fs;
	int has_dumped = 0;
	char corefile[6+sizeof(current->comm)];
	unsigned long dump_start, dump_size;
	struct user dump;

/*
	在task_struct中定义一个dumpable变量，当dumpable==1时表示进程可以清空core文件
	（即将core文件放入回收站），等于0时表示该进程不能清空core文件（即core文件以放
	在回收站中，不可再放到回收站中），此变量初值为1。
	例如在调用do_aout_core_dump()时判断current->;dumpable是否等于1（即判断该进程是
	否能将core文件放入回收站），如果等于1则将该变量置为0，在当前目录下建立一个core
	dump image ,在清空用户结构前，由gdb算出数据段和堆栈段的位置和使用的虚地址，用户
	数据区和堆栈区在清空前将相应内容写入core dump，将PF_DUMPCORE置位，清空数据区和堆栈区。
*/
	if (!current->dumpable)
		return 0;
	current->dumpable = 0;

/* See if we have enough room to write the upage.  */
	if (current->rlim[RLIMIT_CORE].rlim_cur < PAGE_SIZE)
		return 0;
	fs = get_fs();
	set_fs(KERNEL_DS);
	memcpy(corefile,"core.",5);
#if 0
	//comm是保存该进程名字的字符数组，长度为16
	memcpy(corefile+5,current->comm,sizeof(current->comm));
#else
	corefile[4] = '\0';
#endif
	//创建此Core文件
	if (open_namei(corefile,O_CREAT | 2 | O_TRUNC,0600,&inode,NULL)) {
		//如果创建失败
		inode = NULL;
		goto end_coredump;
	}
	//如果创建的不是普通文件
	if (!S_ISREG(inode->i_mode))
		goto end_coredump;
	//如果创建的Core文件没有相应的i节点操作或者没有相应的文件操作函数
	if (!inode->i_op || !inode->i_op->default_file_ops)
		goto end_coredump;
	//获取对此文件的写操作
	if (get_write_access(inode))
		goto end_coredump;
	file.f_mode = 3;
	file.f_flags = 0;
	file.f_count = 1;
	file.f_inode = inode;
	file.f_pos = 0;
	file.f_reada = 0;
	file.f_op = inode->i_op->default_file_ops;
	if (file.f_op->open)
		if (file.f_op->open(inode,&file))
			goto done_coredump;
	//如果file对象对应的i节点没有对应的写操作
	if (!file.f_op->write)
		goto close_coredump;
	has_dumped = 1;
	//复制当前进程的名字到dump中
    strncpy(dump.u_comm, current->comm, sizeof(current->comm));
	dump.u_ar0 = (struct pt_regs *)(((unsigned long)(&dump.regs)) - ((unsigned long)(&dump)));
	dump.signal = signr;	/* Signal that caused the core dump. */
	dump_thread(regs, &dump);

/* If the size of the dump file exceeds the rlimit, then see what would happen
   if we wrote the stack, but not the data area.  */
	if ((dump.u_dsize+dump.u_ssize+1) * PAGE_SIZE >
	    current->rlim[RLIMIT_CORE].rlim_cur)
		dump.u_dsize = 0;

/* Make sure we have enough room to write the stack and data areas. */
	if ((dump.u_ssize+1) * PAGE_SIZE >
	    current->rlim[RLIMIT_CORE].rlim_cur)
		dump.u_ssize = 0;

	set_fs(KERNEL_DS);
/* struct user */
	DUMP_WRITE(&dump,sizeof(dump));
/* Now dump all of the user data.  Include malloced stuff as well */
	DUMP_SEEK(PAGE_SIZE);
/* now we start writing out the user space info */
	set_fs(USER_DS);
/* Dump the data area */
	if (dump.u_dsize != 0) {
		dump_start = dump.u_tsize << 12;
		dump_size = dump.u_dsize << 12;
		DUMP_WRITE(dump_start,dump_size);
	}
/* Now prepare to dump the stack area */
	if (dump.u_ssize != 0) {
		dump_start = dump.start_stack;
		dump_size = dump.u_ssize << 12;
		DUMP_WRITE(dump_start,dump_size);
	}
/* Finally dump the task struct.  Not be used by gdb, but could be useful */
	set_fs(KERNEL_DS);
	DUMP_WRITE(current,sizeof(*current));
close_coredump:
	if (file.f_op->release)
		file.f_op->release(inode,&file);
done_coredump:
	put_write_access(inode);
end_coredump:
	set_fs(fs);
	iput(inode);
	return has_dumped;
}

/*
 * Note that a shared library must be both readable and executable due to
 * security reasons.
 *
 * Also note that we take the address to load from from the file itself.
 */
asmlinkage int sys_uselib(const char * library)
{
	int fd, retval;
	struct file * file;
	struct linux_binfmt * fmt;

	fd = sys_open(library, 0, 0);
	if (fd < 0)
		return fd;
	file = current->files->fd[fd];
	retval = -ENOEXEC;
	if (file && file->f_inode && file->f_op && file->f_op->read) {
		for (fmt = formats ; fmt ; fmt = fmt->next) {
			int (*fn)(int) = fmt->load_shlib;
			if (!fn)
				break;
			retval = fn(fd);
			if (retval != -ENOEXEC)
				break;
		}
	}
	sys_close(fd);
  	return retval;
}

/*
 * create_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
 //函数构造用户堆栈中的参数表。即将int main(int argc,char *argv[],char *envp[])
 //中的argc压栈，并构造argc和envp数组，令数组的内容指向copy_strings()复制到堆栈
 //中的环境字符串参数
unsigned long * create_tables(char * p,int argc,int envc,int ibcs)
{
	unsigned long *argv,*envp;
	unsigned long * sp;
	struct vm_area_struct *mpnt;

	//申请一个新的虚拟地址空间结构vm_area_struct，代表当前进程堆栈空间
	mpnt = (struct vm_area_struct *)kmalloc(sizeof(*mpnt), GFP_KERNEL);
	if (mpnt) {
		mpnt->vm_task = current;	//和当前进程相关联起来
		mpnt->vm_start = PAGE_MASK & (unsigned long) p;
		mpnt->vm_end = TASK_SIZE;
		mpnt->vm_page_prot = PAGE_COPY;
		mpnt->vm_flags = VM_STACK_FLAGS;
		mpnt->vm_ops = NULL;	//这就保证了堆栈空间在发生缺页中断时可以由内核分配一页空的物理内存
		mpnt->vm_offset = 0;
		mpnt->vm_inode = NULL;
		mpnt->vm_pte = 0;
		insert_vm_struct(current, mpnt);
	}
	//堆栈指针 四字节对齐
	sp = (unsigned long *) (0xfffffffc & (unsigned long) p);
	//为压入envp数组中的字符串指针参数预留栈空间 +1是因为要压入0，即NULL
	sp -= envc+1;
	envp = sp;
	//为压入argv数组中的字符串指针参数预留栈空间
	sp -= argc+1;
	argv = sp;
	//以下的“压入”都是逆向压入
	//若是标准Linux执行格式
	if (!ibcs) {
		put_fs_long((unsigned long)envp,--sp);	//将envp，及&envp[0]“压入”用户堆栈
		put_fs_long((unsigned long)argv,--sp);	//将argv，及&argv[0]“压入”用户堆栈
	}
	put_fs_long((unsigned long)argc,--sp);	//将argc“压入”用户堆栈
	current->mm->arg_start = (unsigned long) p;
	//“填充”argv数组
	while (argc-->0) {
		put_fs_long((unsigned long) p,argv++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,argv);
	current->mm->arg_end = current->mm->env_start = (unsigned long) p;
	//“填充”envp数组
	while (envc-->0) {
		put_fs_long((unsigned long) p,envp++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,envp);
	current->mm->env_end = (unsigned long) p;
	//至此，为int main(int argc,char *argv[],char *envp[])建立了堆栈内容
	//参见《深入理解Linux内核》中有关命令行参数和shell环境的章节（第二十章）
	return sp;
}

/*
 * count() counts the number of arguments/envelopes
 *
 * We also do some limited EFAULT checking: this isn't complete, but
 * it does cover most cases. I'll have to do this correctly some day..
 */
static int count(char ** argv)
{
	int error, i = 0;
	char ** tmp, *p;

	if ((tmp = argv) != NULL) {
		error = verify_area(VERIFY_READ, tmp, sizeof(char *));
		if (error)
			return error;
		while ((p = (char *) get_fs_long((unsigned long *) (tmp++))) != NULL) {
			i++;
			error = verify_area(VERIFY_READ, p, 1);
			if (error)
				return error;
		}
	}
	return i;
}

/*
 * 'copy_strings()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 * Modified by TYT, 11/24/91 to add the from_kmem argument, which specifies
 * whether the string and the string array are from user or kernel segments:
 * 
 *参考main(int argc,char *argv[])的参数：argv是char**类型，指向字符串数组，数组项都为char*类型
 *指向实际的字符串
 * from_kmem     argv *        argv **
 *    0          user space    user space
 *    1          kernel space  user space
 *    2          kernel space  kernel space
 * 
 * We do this by playing games with the fs segment register.  Since it
 * is expensive to load a segment register, we try to avoid calling
 * set_fs() unless we absolutely have to.
 */
//argc:参数个数 argv:参数数组
//from_kmem参数的意义在于根据要复制的参数字符串所处的段不同来选择合适的段来进行正确的寻址复制
//注：即使进程由内核进程去创建，但由于ds=fs=内核数据段，所以无论from_kmem为何值本函数照样可以正常进行
unsigned long copy_strings(int argc,char ** argv,unsigned long *page,
		unsigned long p, int from_kmem)
{
	char *tmp, *pag = NULL;
	int len, offset = 0;
	unsigned long old_fs, new_fs;

	if (!p)
		return 0;	/* bullet-proofing */ 
	new_fs = get_ds();	//获取内核数据段？
	old_fs = get_fs();	//获取用户态数据段？
	//如果要复制的argv *和argv **参数都来自于内核空间 则需要将fs设置为内核数据段
	//因为当系统调用进入内核时，默认将fs指向用户数据段
	if (from_kmem==2)
		set_fs(new_fs);
	while (argc-- > 0) {
		//如果要复制的argv *参数来自于内核数据段态而argv **来自于用户数据段 则将fs设置为内核数据段
		if (from_kmem == 1)
			set_fs(new_fs);
		//tmp指向本次循环要复制的参数字符串(复制到内核空间) 比如对于"abc cde fgh"
		//第一次循环时，tmp指向字符串abc 实际上char *a="abc";char *b="cde";
		//char *c="fgh"; char *argv[3]={a,b,c} argv指向的是一个字符串数组
		if (!(tmp = (char *)get_fs_long(((unsigned long *)argv)+argc)))
			panic("VFS: argc is wrong");
		//如果要复制的argv *参数来自于内核数据段态而argv **来自于用户数据段 则将fs恢复到用户态数据段
		if (from_kmem == 1)
			set_fs(old_fs);
		len=0;		/* remember zero-padding */
		//记录参数字符串的长度
		do {
			len++;
		} while (get_fs_byte(tmp++));
		//如果字符串长度过长，将恢复原fs+值，并返回0
		//p是记录参数大小的，它被初始化为：PAGE_SIZE*MAX_ARG_PAGES-4
		if (p < len) {	/* this shouldn't happen - 128kB */
			set_fs(old_fs);
			return 0;
		}
		//开始一个一个地复制字符串中的字符
		while (len) {
			//p可以看作是物理页面的指针，offset可看作是p当前所指向的物理页面内的偏移量
			--p; --tmp; --len;
			if (--offset < 0) {
				offset = p % PAGE_SIZE;	//对PAGE_SIZE取模，以使p自减到一定值后，指向下一个页面
				//如果要复制的argv *和argv **参数都来自于内核空间 则需要将fs恢复到用户态数据段
				if (from_kmem==2)
					set_fs(old_fs);
			/*
				struct linux_binprm结构的page数组模拟或者说实现了此时的用户参数堆栈
				将argv和envp参数字符串复制到进程空间最上方的堆栈的时候，是以入栈的方式
				压入堆栈的，page数组是一个最大具有128KB空间，32个物理页面的空间
				而局部变量offset则模拟了堆栈顶部的指针。每复制一个参数字符串，都将其放入
				offset指向的page数组中的某一页物理内存中，并且offser自减，而且由于page数组
				中的物理页面是从数组末尾向数组首项分配的，所以此过程完全模拟了压栈的过程
				分配物理页面的过程也模拟了栈的自动增长过程，而且利用连续的数组，屏蔽了不连续的
				物理地址，感觉此函数技巧多，颇为精湛
							
			*/
				if (!(pag = (char *) page[p/PAGE_SIZE]) &&	//如果当前数组项为空（物理页面指针为空）
				    !(pag = (char *) page[p/PAGE_SIZE] =	//则需要为此项分配一夜物理内存
				      (unsigned long *) get_free_page(GFP_USER))) //GFP_USER是将fs恢复到用户态数据段的原因？
					return 0;	//分配失败，则返回0
				//如果要复制的argv *和argv **参数都来自于内核空间 则需要将fs设置为内核数据段
				if (from_kmem==2)
					set_fs(new_fs);

			}
			*(pag + offset) = get_fs_byte(tmp);
		}
	}
	//如果要复制的argv *和argv **参数都来自于内核空间 则需要将fs恢复到用户态数据段
	if (from_kmem==2)
		set_fs(old_fs);
	return p;
}

unsigned long setup_arg_pages(unsigned long text_size,unsigned long * page)
{
	unsigned long code_limit,data_limit,code_base,data_base;
	int i;

	code_limit = TASK_SIZE;
	data_limit = TASK_SIZE;
	code_base = data_base = 0;
	current->mm->start_code = code_base;
	data_base += data_limit;	//data_base指向进程用户地址空间末尾

	//将进程的用户态堆栈映射到进程相应的地址空间（进程用户地址空间末尾）
	for (i=MAX_ARG_PAGES-1 ; i>=0 ; i--) {
		data_base -= PAGE_SIZE;
		if (page[i]) {
			current->mm->rss++;	//增加当前进程拥有的物理页面数
			put_dirty_page(current,page[i],data_base);
		}
	}
	return data_limit;
}

/*
 * Read in the complete executable. This is used for "-N" files
 * that aren't on a block boundary, and for files on filesystems
 * without bmap support.
 */
/*
	不支持bmap()的文件系统需要此函数来将可执行文件读入
	因为bmap()可以将文件的逻辑块号转换为实际的磁盘块号
	从而可以利用底层磁盘读写函数将数据块读入
	但如果文件系统不支持此函数，就需要下面的函数来利用实际的
	文件系统的i节点的读写函数来读数据了

	注意：通用的文件映射函数中要求文件支持bmap()函数，否则，如何“通用”呢？
	参见mm/filemap.c中generic_map()函数，minix文件系统的file操作函数
	使用了generic_map()函数函数，因为其支持bmap()函数，参见fs/minix/file.c
*/
int read_exec(struct inode *inode, unsigned long offset,
	char * addr, unsigned long count)
{
	struct file file;
	int result = -ENOEXEC;

	if (!inode->i_op || !inode->i_op->default_file_ops)
		goto end_readexec;
	file.f_mode = 1;
	file.f_flags = 0;
	file.f_count = 1;
	file.f_inode = inode;
	file.f_pos = 0;
	file.f_reada = 0;
	file.f_op = inode->i_op->default_file_ops;
	if (file.f_op->open)
		if (file.f_op->open(inode,&file))
			goto end_readexec;
	if (!file.f_op || !file.f_op->read)
		goto close_readexec;
	if (file.f_op->lseek) {
		if (file.f_op->lseek(inode,&file,offset,0) != offset)
 			goto close_readexec;
	} else
		file.f_pos = offset;
	if (get_fs() == USER_DS) {
		//此函数用的甚是精悍！若可执行文件所在文件系统不支持mmap
		//则在加载可执行文件的时候，会先调用do_mmap()函数，进行匿名
		//映射，目的在于申请可执行文件所占的虚拟地址空间，然而，其相应的进程
		//表项全部映射到系统内核“零页”，并设置写保护
		//这样，进程可执行文件有了其虚拟地址空间，然后调用read_exec()
		//从磁盘读入可执行文件并写入从进程用户地址空间0开始的地址空间
		//这里的verify_area()函数在写入前先检查相应的用户地址空间是否可以
		//写入数据。如果设置了写保护，由于映射的是内核“零页”
		//所以申请新的物理页面，替换掉原来的“零页”，进程由此获得了自己独立
		//的一份物理内存地址，此后，调用文件系统的读函数，将可执行文件读入到
		//相应的物理页面（覆盖从“零页”复制的数据）
		//参见memory.c中的verify_area()函数的实现，以及本文件中的do_execve()
		//load_aout_binary()函数，memory.c中的zeromap_page_range()、mm/mmap.c中的do_mmap()
		result = verify_area(VERIFY_WRITE, addr, count);
		if (result)
			goto close_readexec;
	}
	result = file.f_op->read(inode, &file, addr, count);
close_readexec:
	if (file.f_op->release)
		file.f_op->release(inode,&file);
end_readexec:
	return result;
}


/*
 * This function flushes out all traces of the currently running executable so
 * that a new one can be started
 */
//函数完成放弃从父进程“继承”下来的全部用户空间
void flush_old_exec(struct linux_binprm * bprm)
{
	int i;
	int ch;
	char * name;
/*
	在task_struct中定义一个dumpable变量，当dumpable==1时表示进程可以清空core文件
	（即将core文件放入回收站），等于0时表示该进程不能清空core文件（即core文件以放
	在回收站中，不可再放到回收站中），此变量初值为1。
*/
	current->dumpable = 1;
	name = bprm->filename;
	//将可执行文件名保存在进程的comm数组中
	for (i=0; (ch = *(name++)) != '\0';) {
		if (ch == '/')
			i = 0;
		else
			if (i < 15)
				current->comm[i++] = ch;
	}
	current->comm[i] = '\0';

	/* Release all of the old mmap stuff. */
	exit_mmap(current);

	//About the ldt and gdt processing
	flush_thread();

	if (bprm->e_uid != current->euid || bprm->e_gid != current->egid || 
	    permission(bprm->inode,MAY_READ))
		current->dumpable = 0;
	current->signal = 0;
	//清除信号
	for (i=0 ; i<32 ; i++) {
		current->sigaction[i].sa_mask = 0;
		current->sigaction[i].sa_flags = 0;
		if (current->sigaction[i].sa_handler != SIG_IGN)
			current->sigaction[i].sa_handler = NULL;
	}
	//关闭该关闭的文件句柄
	for (i=0 ; i<NR_OPEN ; i++)
		if (FD_ISSET(i,&current->files->close_on_exec))
			sys_close(i);
	FD_ZERO(&current->files->close_on_exec);
	//清除当前进程的用户级页表，内核级页表依然保留
	clear_page_tables(current);
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	current->used_math = 0;
}

/*
 * sys_execve() executes a new program.
 */
//注意：argv* argv** envp* envp** 一般都是来自于用户空间
//而从内核的角度来看，execve()其实是通过binfmt驱动来实现的，最后通过调用map()来实现。
int do_execve(char * filename, char ** argv, char ** envp, struct pt_regs * regs)
{
	struct linux_binprm bprm;
	struct linux_binfmt * fmt;
	unsigned long old_fs;
	int i;
	int retval;
	//表明可执行文件的性质 当其为1时，说明是shell过程（由shell解释器执行），为0表示是二进制程序
	//这里先假设是二进制文件
	int sh_bang = 0;

	//局部变量bprm将存储运行可执行文件时所需要的信息

	//以下过程将根据打开的可执行文件及一些传入的信息对bprm变量进行初始化

/*	
	与可执行文件路径名的处理办法一样，每个参数的最大长度也定为一个物理页面
	所以bmpr中有一个页面指针数组，数组的大小为允许的最大参数个数MAX_ARG_PAGES
*/	
/*
	这里的-4很有技巧，需要浏览完几乎整个文件才能深入理解。简单来讲，此处是为了
	给用户堆栈中保存的argc参数预留一个空间位置。在本函数中，copy_strings()函数
	只是将进程运行时的命令行参数和环境变量字符串复制到bprm.page[]数组中。
	而在加载可执行文件的时候，对应的可执行文件的加载函数中最终调用setup_arg_pages()
	函数，建立用户堆栈，在用户堆栈中压入int main(int argc,char *argv[],char *envp[])
	中指定的执行环境--argc、argv数组、envp数组。但是在本函数中只复制了字符串参数，即
	argv数组、envp数组内容，但也为argc预留了空间位置（-4）,所以内核可以根据bprm建立合适
	大小的堆栈空间，并建立正确的用户堆栈内容（程序运行环境）。
	
	《Linux内核源代码情景分析》中说，-4是为了给第0个参数预留空间，即argv[0]--可执行程序本身路径名
*/

	bprm.p = PAGE_SIZE*MAX_ARG_PAGES-4;
	for (i=0 ; i<MAX_ARG_PAGES ; i++)	/* clear page-table */
		bprm.page[i] = 0;
	//根据可执行文件的执行路径，找到其i节点
	retval = open_namei(filename, 0, 0, &bprm.inode, NULL);
	if (retval)
		return retval;
	//存储可执行文件的路径名
	bprm.filename = filename;
	//保存字符串指针数组argv[]中参数的个数
	if ((bprm.argc = count(argv)) < 0)
		return bprm.argc;
	//保存字符串指针数组envp[]中参数的个数
	//由于数组是在用户空间而不在系统空间，所以计数没那么简单
	if ((bprm.envc = count(envp)) < 0)
		return bprm.envc;
	
restart_interp:
	//如果打开的不是普通的文件
	if (!S_ISREG(bprm.inode->i_mode)) {	/* must be regular file */
		retval = -EACCES;
		goto exec_error2;
	}
	//如果可执行文件不允许在挂上的文件系统上执行程序
	if (IS_NOEXEC(bprm.inode)) {		/* FS mustn't be mounted noexec */
		retval = -EPERM;
		goto exec_error2;
	}
	//如果可执行文件没有对应的超级块
	if (!bprm.inode->i_sb) {
		retval = -EACCES;
		goto exec_error2;
	}
	i = bprm.inode->i_mode;
	//IS_NOSUID：执行程序时，不遵照set-user-ID和set-group-ID位
	if (IS_NOSUID(bprm.inode) && (((i & S_ISUID) && bprm.inode->i_uid != current->
	    euid) || ((i & S_ISGID) && !in_group_p(bprm.inode->i_gid))) && !suser()) {
		retval = -EPERM;
		goto exec_error2;
	}
	/* make sure we don't let suid, sgid files be ptraced. */
	if (current->flags & PF_PTRACED) {
		bprm.e_uid = current->euid;
		bprm.e_gid = current->egid;
	} else {
		bprm.e_uid = (i & S_ISUID) ? bprm.inode->i_uid : current->euid;
		bprm.e_gid = (i & S_ISGID) ? bprm.inode->i_gid : current->egid;
	}
	//验证可执行文件的执行权限
	if ((retval = permission(bprm.inode, MAY_EXEC)) != 0)
		goto exec_error2;
	if (!(bprm.inode->i_mode & 0111) && fsuser()) {
		retval = -EACCES;
		goto exec_error2;
	}
	/* better not execute files which are being written to */
	//如果有进程在对此可执行文件进行写操作，最好不要执行此程序
	if (bprm.inode->i_wcount > 0) {
		retval = -ETXTBSY;
		goto exec_error2;
	}
	//buf清零
	memset(bprm.buf,0,sizeof(bprm.buf));
	old_fs = get_fs();
	set_fs(get_ds());	//因为要将数据读入内核数据段的bprm.buf，所以需要将fs设置为内核数据段
	//从可执行程序中读入开头的128字节到bprm中的缓冲区
	//128字节中包含了可执行文件属性的必要信息
	retval = read_exec(bprm.inode,0,bprm.buf,128);
	set_fs(old_fs);	//将fs恢复
	if (retval < 0)
		goto exec_error2;
	//如果可执行文件开头两个字节是“#!”，则表明其是shell过程
	//需要shell解释器来执行，所以需要加载shell解释器来执行shell脚本，并且只需要加载一次
	//由于此语句存在于一个可能的循环中，所以需要判断!sh_bang
	if ((bprm.buf[0] == '#') && (bprm.buf[1] == '!') && (!sh_bang)) {
		/*
		 * This section does the #! interpretation.
		 * Sorta（有几分，可以说是） complicated, but hopefully it will work.  -TYT
		 */

		char *cp, *interp, *i_name, *i_arg;

		iput(bprm.inode);
		bprm.buf[127] = '\0';
		//strchr(s,c)函数用于在字符串s中搜索字符c,如果找到了字符c,则返回指向字符c的指针
		if ((cp = strchr(bprm.buf, '\n')) == NULL)
			cp = bprm.buf+127;	//将cp指向缓冲区末尾
		*cp = '\0';
		while (cp > bprm.buf) {
			cp--;
			if ((*cp == ' ') || (*cp == '\t'))
				*cp = '\0';
			else
				break;
		}
		//cp = bprm.buf+2:跳过“#!”连个字节 这里是要找出解释器的路径名
		for (cp = bprm.buf+2; (*cp == ' ') || (*cp == '\t'); cp++);
		if (!cp || *cp == '\0') {
			retval = -ENOEXEC; /* No interpreter name found */
			goto exec_error1;
		}
		interp = i_name = cp;
		i_arg = 0;
		//这里是要找出解释器的名字
		for ( ; *cp && (*cp != ' ') && (*cp != '\t'); cp++) {
 			if (*cp == '/')
				i_name = cp+1;
		}
		//执行到这里，若解释器是/bin/bash
		//则interp指向“/bin/bash” i_name指向“bash”

		while ((*cp == ' ') || (*cp == '\t'))
			*cp++ = '\0';
		//i_arg指向传递给解释器的参数字符串
		if (*cp)
			i_arg = cp;
		/*
		 * OK, we've parsed out the interpreter name and
		 * (optional) argument.
		 */
		//如果原sh_bang值为0,进行这样的判断可能是in case
		if (sh_bang++ == 0) {
			//把执行的参数，也就是argv[]，以及运行的环境，也就是envp[]，从用户空间复制到数据结构bprm中
			//copy_strings()函数的意义就是将参数拷贝到一个内核的页面当中并设置为bprm的一个字段
			//argv* argv** envp* envp** 一般都是来自于用户空间 所以from_kmem=0
			//即使进程由内核进程调用本函数创建紧凑，from_kmem=0也没关系
			bprm.p = copy_strings(bprm.envc, envp, bprm.page, bprm.p, 0);
			bprm.p = copy_strings(--bprm.argc, argv+1, bprm.page, bprm.p, 0);
		}
		/*
		 * Splice in (1) the interpreter's name for argv[0]
		 *           (2) (optional) argument to interpreter
		 *           (3) filename of shell script
		 *
		 * This is done in reverse order, because of how the
		 * user environment and arguments are stored.
		 */
		//将可执行文件（这里是shell脚本文件）的路径名复制压入到参数堆栈中
		//bprm.filename 和&bprm.filename都属于内核数据段，所以from_kmem=2
		bprm.p = copy_strings(1, &bprm.filename, bprm.page, bprm.p, 2);
		bprm.argc++;	//增加参数个数
		//如果又要传递给解释器的参数字符串 则将其复制压入到参数堆栈中
		if (i_arg) {
			//因为i_arg是此内核函数的局部变量，所以属于内核数据段
			//而&i_arg也自然属于内核数据段，所以from_kmem=2
			bprm.p = copy_strings(1, &i_arg, bprm.page, bprm.p, 2);
			bprm.argc++;
		}
		//将解释器名称压入参数堆栈 同上from_kmem=2
		bprm.p = copy_strings(1, &i_name, bprm.page, bprm.p, 2);
		bprm.argc++;
		//如果经过上面的压栈过程后，bprm.p为0了，说明参数堆栈放不下所有的参数字符串
		if (!bprm.p) {
			retval = -E2BIG;
			goto exec_error1;
		}
		/*
		 * OK, now restart the process with the interpreter's inode.
		 * Note that we use open_namei() as the name is now in kernel
		 * space, and we don't need to copy it.
		 */
		//我们重新启动这个程序，以让内核加载执行解释器（解释器执行shell脚本文件）
		retval = open_namei(interp, 0, 0, &bprm.inode, NULL);
		if (retval)
			goto exec_error1;
		goto restart_interp;
	}
	//如果是二进制可执行文件（sh_bang==0）则说明上面的if判断语句之前未执行过，参数还没有复制到用户堆栈
	//否则，若是restart过一次执行到此的，则sh_bang不为0,之前已经复制了参数字符串，就不需要复制了
	if (!sh_bang) {
		bprm.p = copy_strings(bprm.envc,envp,bprm.page,bprm.p,0);
		bprm.p = copy_strings(bprm.argc,argv,bprm.page,bprm.p,0);
		if (!bprm.p) {
			retval = -E2BIG;
			goto exec_error2;
		}
	}

	bprm.sh_bang = sh_bang;
	
	//以上过程只是根据可执行程序文件头建立起了程序的执行环境，并没有将可执行程序加载进内存
	
	//遍历formats链表，让链表中的每个成员都试试他们的load_binary()函数（用来装入可执行程序）
	//找出一个认识该目标文件格式的linux_binprm 对于a.out格式，就执行下面的load_aout_binary()函数
	for (fmt = formats ; fmt ; fmt = fmt->next) {
		int (*fn)(struct linux_binprm *, struct pt_regs *) = fmt->load_binary;
		if (!fn)
			break;
		retval = fn(&bprm, regs);
		if (retval >= 0) {
			iput(bprm.inode);
			current->did_exec = 1;
			return retval;
		}
		if (retval != -ENOEXEC)
			break;
	}
exec_error2:
	iput(bprm.inode);
exec_error1:
	for (i=0 ; i<MAX_ARG_PAGES ; i++)
		free_page(bprm.page[i]);
	return(retval);
}

//在这里，我觉得是在为进程的可执行文件内存镜像的bss段做映射
static void set_brk(unsigned long start, unsigned long end)
{
	start = PAGE_ALIGN(start);
	end = PAGE_ALIGN(end);
	if (end <= start)
		return;
	//将进程的bss段做匿名映射，映射到内核“零页” 并做写保护
	//当写此区间是，会为进程分配实际的物理内存，实现按需加载
	do_mmap(NULL, start, end - start,
		PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_FIXED | MAP_PRIVATE, 0);
}

/*
 * These are the functions used to load a.out style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */
//通过读取放在可执行文件中的信息为当前进程建立一个新的执行环境
static int load_aout_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	//所有的a.out格式的可执行文文件（二进制代码）的开头都应该是一个exec数据结构
	//定义在a.out.h文件中
	struct exec ex;
	struct file * file;
	int fd, error;
	unsigned long p = bprm->p;
	unsigned long fd_offset;

	//填充ex结构
	ex = *((struct exec *) bprm->buf);		/* exec-header */
	//文件头信息匹配：判断可执行文件格式是不是a.out类型的
/*
	a.out 文件中包含符号表和两个重定位表，这三个表的内容在连接目标文件
	以生成可执行文件时起作用。在最终可执行的 a.out 文件中，这三个表的
	长度都为 0。a.out 文件在连接时就把所有外部定义包含在可执行程序中，
	如果从程序设计的角度来看，这是一种硬编码方式，或者可称为模块之间是
	强藕和的。在后面的讨论中，我们将会具体看到ELF格式和动态连接机制是
	如何对此进行改进的。
	ex.a_trsize就是文件中代码段的重定位信息，ex.a_drsize就是文件中数据段
	的重定位信息，在可执行文件中，这两个字段应该为0，否则，可能是目标文件
	而不是最终的可执行文件
*/
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != OMAGIC && 
	     N_MAGIC(ex) != QMAGIC) ||
	    ex.a_trsize || ex.a_drsize ||
	    bprm->inode->i_size < ex.a_text+ex.a_data+ex.a_syms+N_TXTOFF(ex)) {
		return -ENOEXEC;
	}

	//将新进程的“个性”设置为PER_LINUX，及标准执行格式
	//linux也可以执行在其他系统上编译的程序，比如SunOS BSD等，那么，就需要
	//一个字段来区分这些不同系统编译的不同程序
	current->personality = PER_LINUX;
/*
	各种a.out格式的文件因目标代码的特性不同，其正文段的起始位置也就不同。为此提供了一个
	宏操作N_TXTOFF(ex)，以便根据代码的特性取得正文在目标文件中的起始位置
*/

	fd_offset = N_TXTOFF(ex);
	//ZMAGIC格式加入了对按需分页的支持，代码段和数据段的长度需要是页宽的整数倍
	//执行头部、代码段和数据段都被链接程序处理成多个页面大小的块
	if (N_MAGIC(ex) == ZMAGIC && fd_offset != BLOCK_SIZE) {
		printk(KERN_NOTICE "N_TXTOFF != BLOCK_SIZE. See a.out.h.\n");
		return -ENOEXEC;
	}

	if (N_MAGIC(ex) == ZMAGIC && ex.a_text &&
	    (fd_offset < bprm->inode->i_sb->s_blocksize)) {
		printk(KERN_NOTICE "N_TXTOFF < BLOCK_SIZE. Please convert binary.\n");
		return -ENOEXEC;
	}

	/* OK, This is the point of no return */
	//已经取得了足够的信息，是跟当前进程脱离的时候了
	flush_old_exec(bprm);	//函数完成放弃从父进程“继承”下来的全部用户空间

	//根据a.out的文件头信息初始化进程的mm结构
/*
	每个进程都有自己独立的mm_struct，使得每个进程都有一个抽象的平坦的独立的32或64位地址空间，
	各个进程都在各自的地址空间中相同的地址内存存放不同的数据而且互不干扰。如果进程之间共享相
	同的地址空间，则被称为线程。其中[start_code,end_code)表示代码段的地址空间范围。
	[start_data,end_start)表示数据段的地址空间范围。[start_brk,brk)分别表示heap段的起始空间
	和当前的heap指针。[start_stack,end_stack)表示stack段的地址空间范围
*/
	current->mm->brk = ex.a_bss +
		(current->mm->start_brk =
		(current->mm->end_data = ex.a_data +
		(current->mm->end_code = ex.a_text +
		(current->mm->start_code = N_TXTADDR(ex)))));
	current->mm->rss = 0;	//进程当前占用的实际物理内存页面数
	current->mm->mmap = NULL;
	current->suid = current->euid = current->fsuid = bprm->e_uid;
	current->sgid = current->egid = current->fsgid = bprm->e_gid;
	
/*
	对于二进制,函数将加载一个“a.out”可执行文件并以使用 mmap()加载磁盘文件或调用 
	read_exec()而结束。前一种方法使用了 Linux的按需加载机理,在程序被访问时使用出
	错加载方式(fault-in)加载程序页面,而后一种方式是在主机文件系统不支持内存映像时
	(例如“msdos”文件系统)使用的。从1.1版本内核开始内嵌了一个修订的 msdos 文件系统,
	它支持 mmap()。而且linux_binfmt 结构已是一个链表而不是一个数组了,以允许以一个
	内核模块的方式加载一个新的二进制格式。最后,结构的本身也已经被扩展成能够访问与
	格式相关的核心转储程序了。
*/
	//OMAGIC表示代码和数据段紧随在执行头后面并且是连续存放的。内核将代码和数据段
	//都加载到可读写内存中。
	//之所以将OMAGIC拿出来特殊处理，是因为代码段和数据段是连续存放的，数据段进跟在
	//代码段后面，没有文本和数据的分离。而其他的标志则将代码段和数据段分开，并且分页
	if (N_MAGIC(ex) == OMAGIC) {
		//完成可执行映像向虚存区域的映射，由它建立有关的虚存区域
		//file参数为NULL，表示匿名映射，从用户地址空间0开始建立映射
		//映射的长度为代码段的长度和数据段的长度之和 匿名映射会将产生的
		//虚拟地址对应的进程地址空间的进程表项全部设置为映射到内核的一个“零页”
		//并产生写保护。当调用read_exec()函数时，会进行一个类似“写时复制”的过程
		//将可执行文件读入并映射到进程地址空间，参见read_exec()中的详细注释
		do_mmap(NULL, 0, ex.a_text+ex.a_data,
			PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_FIXED|MAP_PRIVATE, 0);
		//读入可执行文件的代码段和数据段，需要跳过文件头（占32个字节）
		//但这里的(char *) 0并不是没有将文件内容读入到任何缓冲中，
		//而是将数据写入了用户地址空间从0开始的位置
		read_exec(bprm->inode, 32, (char *) 0, ex.a_text+ex.a_data);
	} else {
		//代码段和数据段的长度需要是页宽的整数倍
		if (ex.a_text & 0xfff || ex.a_data & 0xfff)
			printk(KERN_NOTICE "executable not page aligned\n");
		
		//以只读的方式打开要执行的可执行文件
		fd = open_inode(bprm->inode, O_RDONLY);
		
		if (fd < 0)
			return fd;
		file = current->files->fd[fd];
		//如果文件不支持mmap（和文件系统有关，而与可执行文件格式无关）
		if (!file->f_op || !file->f_op->mmap) {
			sys_close(fd);	//关闭文件 因为此时不能依靠文件来做映射了
			//采用匿名映射的方式 先将进程的用户空间地址对应的代码段和数据段
			//区间全部映射到“零页”（此过程实则是为了建立进程相应的虚拟地址空间范围）
			do_mmap(NULL, 0, ex.a_text+ex.a_data,
				PROT_READ|PROT_WRITE|PROT_EXEC,
				MAP_FIXED|MAP_PRIVATE, 0);
			//文件不支持mmap 就需要read_exec()函数来读取可执行文件
			read_exec(bprm->inode, fd_offset,
				  (char *) N_TXTADDR(ex), ex.a_text+ex.a_data);
			goto beyond_if;
		}

		//将a.out的代码段映射加载到可读可执行的内存中
		error = do_mmap(file, N_TXTADDR(ex), ex.a_text,
			PROT_READ | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
			fd_offset);

		//因为映射的时候制定了MAP_FIXED标志，表示
		//如果参数start所指的地址无法成功建立映射时，则放弃映射，不对地址做修正。通常不鼓励用此旗标
		//do_mmap()函数成功则返回映射的起始地址
		if (error != N_TXTADDR(ex)) {
			sys_close(fd);	//关闭文件
			send_sig(SIGKILL, current, 0);	//杀死自己
			return error;
		}

		//将a.out的数据段映射加载到可读可写可执行的内存中		
 		error = do_mmap(file, N_TXTADDR(ex) + ex.a_text, ex.a_data,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
				fd_offset + ex.a_text);
		sys_close(fd);	//映射完毕，需要关闭可执行文件 打开文件是为了获取其i节点
		if (error != N_TXTADDR(ex) + ex.a_text) {
			send_sig(SIGKILL, current, 0);
			return error;
		}
	}
beyond_if:
	/*
	*下面进行的有关引用计数的自减操作，是为了减少原进程，即创建新进程时继承自其父进程
	*的exec_domain和binfmt。然后内核根据新进程的特点重新为进程分配新的合适的exec_domain
	*和binfmt，然后自增这些新引用的结构的引用计数
	*/
	//如果当前进程有执行域（继承自父进程而来的）并且执行域具有引用计数 则减少引用计数
	if (current->exec_domain && current->exec_domain->use_count)
		(*current->exec_domain->use_count)--;
	//如果当前进程对用的二进制可执行文件格式（继承自父进程而来的）不为空，且其具有引用计数 则减少引用计数
	if (current->binfmt && current->binfmt->use_count)
		(*current->binfmt->use_count)--;
	//根据设置的新进程执行个性，找到其执行域
	current->exec_domain = lookup_exec_domain(current->personality);
	current->binfmt = &aout_format;	//将新进程的二进制执行格式设置为a.out文件格式的
	//增加当前进程的执行域的引用计数
	if (current->exec_domain && current->exec_domain->use_count)
		(*current->exec_domain->use_count)++;
	//增加当前进程的二进制执行文件格式的引用计数
	if (current->binfmt && current->binfmt->use_count)
		(*current->binfmt->use_count)++;

	//映射进程的bss段
	set_brk(current->mm->start_brk, current->mm->brk);
	//将进程的用户态堆栈映射到进程相应的地址空间（进程用户地址空间末尾）
	//函数返回的是TASK_SIZE
	//因为p最初指向的是一个MAX_ARG_PAGES*PAGE_SIZE（128KB）大小的空间顶部
	//（准确来说是128KB-4），所以在执行create_tables()函数之前和之后，p都指向了
	//一个TASK_SIZE-当前堆栈内容大小的位置。
	//也即，p（p<TASK_SIZE）--TASK_SIZE范围内是copy_strings()函数“压入”的字符串参数总大小的空间
	//而且复制的字符串参数也是存放在这个空间里的
	p += setup_arg_pages(ex.a_text,bprm->page);
	p -= MAX_ARG_PAGES*PAGE_SIZE;
	//举一个极端的例子：copy_strings()函数复制占用了全部的128KB堆栈空间，那么执行到这里以后
	//p在用户空间指向TASK_SIZE-128KB的位置处，create_tables()函数会在p以下的地址空间开始建立
	//用户堆栈内容，其参数数据来自于p以上的部分（这部分空间不就浪费了吗？？）
	//并且若堆栈空间不够 内核会产生缺页中断来处理
	//create_tables()函数实则是创建了进程的堆栈空间vm
	/*上面说的空间浪费是不对的，实际上，create_tables()函数是根据copy_strings()函数复制得到的参数字符串
	  来建立了一张对此字符串表进行索引的索引表，放在了实际的字符串参数空间下面
	*/
	p = (unsigned long)create_tables((char *)p,
					bprm->argc, bprm->envc,
					current->personality != PER_LINUX);
	current->mm->start_stack = p;
	//设置进程相关寄存器的值 其中 ex.a_entry设置为进程的eip，p设置为进程的esp
	//当系统调用结束，返回用户空间时，就会恢复用户空间的相关寄存器的值
	start_thread(regs, ex.a_entry, p);
	if (current->flags & PF_PTRACED)
		send_sig(SIGTRAP, current, 0);
	return 0;
}


static int load_aout_library(int fd)
{
    struct file * file;
	struct exec ex;
	struct  inode * inode;
	unsigned int len;
	unsigned int bss;
	unsigned int start_addr;
	int error;
	
	file = current->files->fd[fd];
	inode = file->f_inode;
	
	set_fs(KERNEL_DS);
	if (file->f_op->read(inode, file, (char *) &ex, sizeof(ex)) != sizeof(ex)) {
		return -EACCES;
	}
	set_fs(USER_DS);
	
	/* We come in here for the regular a.out style of shared libraries */
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != QMAGIC) || ex.a_trsize ||
	    ex.a_drsize || ((ex.a_entry & 0xfff) && N_MAGIC(ex) == ZMAGIC) ||
	    inode->i_size < ex.a_text+ex.a_data+ex.a_syms+N_TXTOFF(ex)) {
		return -ENOEXEC;
	}
	if (N_MAGIC(ex) == ZMAGIC && N_TXTOFF(ex) && 
	    (N_TXTOFF(ex) < inode->i_sb->s_blocksize)) {
		printk("N_TXTOFF < BLOCK_SIZE. Please convert library\n");
		return -ENOEXEC;
	}
	
	if (N_FLAGS(ex)) return -ENOEXEC;

	/* For  QMAGIC, the starting address is 0x20 into the page.  We mask
	   this off to get the starting address for the page */

	start_addr =  ex.a_entry & 0xfffff000;

	/* Now use mmap to map the library into memory. */
	error = do_mmap(file, start_addr, ex.a_text + ex.a_data,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE,
			N_TXTOFF(ex));
	if (error != start_addr)
		return error;
	len = PAGE_ALIGN(ex.a_text + ex.a_data);
	bss = ex.a_text + ex.a_data + ex.a_bss;
	if (bss > len)
		do_mmap(NULL, start_addr + len, bss-len,
			PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_PRIVATE|MAP_FIXED, 0);
	return 0;
}
