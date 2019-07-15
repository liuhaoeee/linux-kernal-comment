/*
 * Dynamic loading of modules into the kernel.
 *
 * Modified by Bjorn Ekwall <bj0rn@blox.se>
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


#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#ifdef CONFIG_MODVERSIONS
# ifndef __GENKSYMS__
#  ifdef MODULE
#   define _set_ver(sym,vers) sym ## _R ## vers
#   include <linux/modversions.h>
#  else /* MODULE */
#   ifdef EXPORT_SYMTAB
#    define _set_ver(sym,vers) sym
#    include <linux/modversions.h>
#   endif /* EXPORT_SYMTAB */
#  endif /* MODULE */
# else /* __GENKSYMS__ */
#  define _set_ver(sym,vers) sym
#  endif /* __GENKSYMS__ */
#endif /* CONFIG_MODVERSIONS */

/* values of module.state */
#define MOD_UNINITIALIZED 0
#define MOD_RUNNING 1
#define MOD_DELETED 2

/* maximum length of module name */
#define MOD_MAX_NAME 64

/* maximum length of symbol name */
/* symbol name最大长度 */
#define SYM_MAX_NAME 60

/* kernel_sysm结构，传给"insmod" */
struct kernel_sym { /* sent to "insmod" */
	unsigned long value;		/* value of symbol *//* symbol 的值，是sysmbol的地址 */
	char name[SYM_MAX_NAME];	/* name of symbol *//* symbol 的名字 */	
};

struct module_ref {
	struct module *module;
	struct module_ref *next;
};

struct internal_symbol {
	void *addr;
	char *name;
	};

/*
	symbol_table是个非常重要的数据结构，它包含两方面的信息：符号信息和引用信息。
	符号信息：这里说的符号，是指模块提供出来的函数和变量。由于模块是个目标文件，
	最终要与核心链接成一个整体。因此，它要提供一套机制，或者说一种数据结构，方便
	模块与核心相链接。symbol_table就是这样一种数据结构。symbol_table中的符号信息
	是模块提供出来的可供其他地方使用的函数和变量。核心也把它提供出来的函数和变量放
	在一个symbol_table结构中，其他模块使用的核心函数（如kmalloc等）就是通过这个结
	构查到函数的实际地址，从而为正确链接提供了数据。引用信息：模块之间有可能有引用
	关系。这里记录下模块引用到其他模块的信息。

	后面是两个零大小的数组声明。这是GCC编译器对标准C的扩充。我们使用数组时，往往不
	能事先确定其大小，因此总是在使用时动态申请空间。如果我们在symbol_table结构中改
	用两个指针，指向分配到的内存空间，也能实现同样的功能，但多用了两个指针本身占用
	的空间。而使用零大小的数组声明，编译器认为symbol_table类型的大小只相当于三个int
	类型之和。因此，当两个数组大小（n_symbols、n_refs）知道后，可使用语句
	mysize = sizeof(struct symbol_table) +
	 mytab->n_symbols * sizeof(struct internal_symbol) +
	 mytab->n_refs * sizeof(struct module_ref)
	得到其实际占用的空间大小。注意这个大小不等于symbol_table结构中的size。后一个size
	往往还包括字符串表的大小。字符串表记录了符号的名称。
*/

struct symbol_table { /* received from "insmod" */
	int size; /* total, including string table!!! *///size是symbol_table的总大小
	int n_symbols;	//内核符号表中符号的个数,n_symbols是symbol_table中的符号数，即数组symbol的大小。
	int n_refs;	//n_refs是symbol_table中的引用数，即数组ref的大小。
	//下面的0大小数组的用法很精妙，参见上访的注释说明
	struct internal_symbol symbol[0]; /* actual size defined by n_symbols */
	struct module_ref ref[0]; /* actual size defined by n_refs */
};
/*
 * Note: The string table follows immediately after the symbol table in memory!
 */

struct module {
	struct module *next;	//next指针表明模块在系统中是通过链表方式连接起来的。有一个名为module_list的
				//指针始终指向链表头，通过这个指针可以遍历所有加到系统中的模块。
	struct module_ref *ref;	/* the list of modules that refer to me *///ref指针记录了模块之间的引用关系。
						//它指向所有引用该模块的模块，这些模块也是用链表连起来。
	struct symbol_table *symtab;	//symtab指针指向该模块对应的符号表。
	char *name;	//name指针指向模块的名字。其名字存放在module结构后面的64个字节里。对module结构申请空间的方式一般为：（MOD_MAX_NAME = 64）
			//mp = (struct module*) kmalloc(sizeof(struct module) + MOD_MAX_NAME, GFP_KERNEL)
	int size;			/* size of module in pages *///size是模块代码的大小，以页为单位。
	void* addr;			/* address of module *///addr是模块代码所在位置。module结构是管理模块用的，
								//真正的模块代码存放在addr指向的地方。
	int state;	//state是模块的状态。模块有三种状态：
			//MOD_UNINITIALIZED：模块刚创建，还没有被初始化。
			//MOD_RUNNING：模块已经能被使用。
			//MOD_DELETED：模块要被删除，它所占用的空间要被释放。
	void (*cleanup)(void);		/* cleanup routine *///cleanup是清除函数。每个模块都可能会提供自己的
							     //init和cleanup函数。这里记录了cleanup函数。
};

/* module的初始化和清除程序结构定义 */
struct mod_routines {
	int (*init)(void);		/* initialization routine *//* 初始化程序 */
	void (*cleanup)(void);		/* cleanup routine *//* 清除程序 */
};

/* rename_module_symbol(old_name, new_name)  WOW! */
extern int rename_module_symbol(char *, char *);

/* insert new symbol table */
extern int register_symtab(struct symbol_table *);

/*
 * The first word of the module contains the use count.
 */
/* module的第一个字表示module的引用数 */
#define GET_USE_COUNT(module)	(* (int *) (module)->addr)
/*
 * define the count variable, and usage macros.
 */
/* module模块记数宏 */
extern int mod_use_count_;
#if defined(CONFIG_MODVERSIONS) && defined(MODULE) && !defined(__GENKSYMS__)
int Using_Versions; /* gcc will handle this global (used as a flag) correctly */
#endif

#define MOD_INC_USE_COUNT      mod_use_count_++
#define MOD_DEC_USE_COUNT      mod_use_count_--
#define MOD_IN_USE	       (mod_use_count_ != 0)

#endif
