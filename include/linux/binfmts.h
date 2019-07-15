#ifndef _LINUX_BINFMTS_H
#define _LINUX_BINFMTS_H

#include <linux/ptrace.h>

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB !
 */
#define MAX_ARG_PAGES 32

/*
 * This structure is used to hold the arguments that are used when loading binaries.
 */
//内核中为可执行程序的载入定义了一个数据结构linux_binprm，以便将运行一个可执行文件时所需的信息组织在一起
//还可用于维护程序执行过程中所使用的各种数据
//参见do_execve()函数
struct linux_binprm{
	char buf[128];	//用于保存从可执行文件读入的开头的128个字节
	unsigned long page[MAX_ARG_PAGES];	//模拟了用户参数栈
	unsigned long p;
	int sh_bang;	//表明可执行文件的性质 当其为1时，说明是shell过程（由shell解释器执行），为0表示是二进制程序
	struct inode * inode;
	int e_uid, e_gid;
	int argc, envc;
	char * filename;	   /* Name of binary */
};

/*
 * This structure defines the functions that are used to load the binary formats that
 * linux accepts.
 */
 //Linux支持多种不同的可执行文件。每一种格式的可执行文件都用结构体linux_binfmt 来描述
 //在linux_binfmt中包含两个重要指向函数的指针，load_binary装入可执行代码
 //load_shlib装入共享库。Core_dump是个转储函数指针
struct linux_binfmt {
	struct linux_binfmt * next;	//由next构成一个链表，表头则由formats指向
	int *use_count;
	//读取可执行文件中的信息为当前进程建立一个新的执行环境
	int (*load_binary)(struct linux_binprm *, struct  pt_regs * regs);
	//动态地把一个共享库绑定到一个已经在运行的进程，这由uselib系统调用激活
	int (*load_shlib)(int fd);
	//在名为core的文件中存放当前进程的执行上下文
	//Core文件通常在进程接收到一个缺省操作为“dump”的信号时被创建
	int (*core_dump)(long signr, struct pt_regs * regs);
};

extern int register_binfmt(struct linux_binfmt *);
extern int unregister_binfmt(struct linux_binfmt *);

extern int read_exec(struct inode *inode, unsigned long offset,
	char * addr, unsigned long count);

extern int open_inode(struct inode * inode, int mode);

extern void flush_old_exec(struct linux_binprm * bprm);
extern unsigned long setup_arg_pages(unsigned long text_size,unsigned long * page);
extern unsigned long * create_tables(char * p,int argc,int envc,int ibcs);
extern unsigned long copy_strings(int argc,char ** argv,unsigned long *page,
		unsigned long p, int from_kmem);

/* this eventually goes away */
#define change_ldt(a,b) setup_arg_pages(a,b)

#endif
