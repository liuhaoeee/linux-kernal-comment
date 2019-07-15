/*
 *  linux/init/main.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
/*
	The PDF documents about understanding Linux Kernel 1.2 are made by Liu Yihao
	这份学习Linux内核的PDF文档由刘以浩同学整理完成
	包含了作者在学习内核过程中搜集到的大量有关Linux
	内核的知识和自己的一些理解以及注释
	当然，最宝贵的还是这份原生的内核代码
	NOTE：如果在阅读过程中遇到什么问题，欢迎和我交流讨论
	Email：liuyihaolovem@163.com
*/
#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <asm/system.h>
#include <asm/io.h>

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/head.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/ioport.h>
#include <linux/hdreg.h>
#include <linux/mm.h>

#include <asm/bugs.h>

extern unsigned long * prof_buffer;
extern unsigned long prof_len;
extern char etext, end;
extern char *linux_banner;

static char printbuf[1024];

extern int console_loglevel;

extern void init(void);
extern void init_IRQ(void);
extern void init_modules(void);
extern long console_init(long, long);
extern long kmalloc_init(long,long);
extern long blk_dev_init(long,long);
extern long chr_dev_init(long,long);
extern void sock_init(void);
extern long rd_init(long mem_start, int length);
unsigned long net_dev_init(unsigned long, unsigned long);
extern long bios32_init(long, long);

extern void bmouse_setup(char *str, int *ints);
extern void eth_setup(char *str, int *ints);
extern void xd_setup(char *str, int *ints);
extern void floppy_setup(char *str, int *ints);
extern void mcd_setup(char *str, int *ints);
extern void aztcd_setup(char *str, int *ints);
extern void st_setup(char *str, int *ints);
extern void st0x_setup(char *str, int *ints);
extern void tmc8xx_setup(char *str, int *ints);
extern void t128_setup(char *str, int *ints);
extern void pas16_setup(char *str, int *ints);
extern void generic_NCR5380_setup(char *str, int *intr);
extern void aha152x_setup(char *str, int *ints);
extern void aha1542_setup(char *str, int *ints);
extern void aha274x_setup(char *str, int *ints);
extern void buslogic_setup(char *str, int *ints);
extern void scsi_luns_setup(char *str, int *ints);
extern void sound_setup(char *str, int *ints);
#ifdef CONFIG_SBPCD
extern void sbpcd_setup(char *str, int *ints);
#endif CONFIG_SBPCD
#ifdef CONFIG_CDU31A
extern void cdu31a_setup(char *str, int *ints);
#endif CONFIG_CDU31A
#ifdef CONFIG_CDU535
extern void sonycd535_setup(char *str, int *ints);
#endif CONFIG_CDU535
void ramdisk_setup(char *str, int *ints);

#ifdef CONFIG_SYSVIPC
extern void ipc_init(void);
#endif
#ifdef CONFIG_SCSI
extern unsigned long scsi_dev_init(unsigned long, unsigned long);
#endif

/*
 * Boot command-line arguments
 */
#define MAX_INIT_ARGS 8
#define MAX_INIT_ENVS 8

extern void time_init(void);

static unsigned long memory_start = 0;
static unsigned long memory_end = 0;

static char term[21];
int rows, cols;

int ramdisk_size;
int root_mountflags = 0;

static char * argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
static char * envp_init[MAX_INIT_ENVS+2] = { "HOME=/", term, NULL, };

static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", term, NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", term, NULL };

char *get_options(char *str, int *ints)
{
	char *cur = str;
	int i=1;

	while (cur && isdigit(*cur) && i <= 10) {
		ints[i++] = simple_strtoul(cur,NULL,0);
		if ((cur = strchr(cur,',')) != NULL)
			cur++;
	}
	ints[0] = i-1;
	return(cur);
}

struct {
	char *str;
	void (*setup_func)(char *, int *);
} bootsetups[] = {
	{ "reserve=", reserve_setup },
	{ "ramdisk=", ramdisk_setup },
#ifdef CONFIG_BUGi386
	{ "no-hlt", no_halt },
	{ "no387", no_387 },
#endif
#ifdef CONFIG_INET
	{ "ether=", eth_setup },
#endif
#ifdef CONFIG_SCSI
	{ "max_scsi_luns=", scsi_luns_setup },
#endif
#ifdef CONFIG_BLK_DEV_IDE
	{ "hda=", hda_setup },
	{ "hdb=", hdb_setup },
	{ "hdc=", hdc_setup },
	{ "hdd=", hdd_setup },
	{ "hd=",  ide_setup },
#elif defined(CONFIG_BLK_DEV_HD)
	{ "hd=", hd_setup },
#endif
#ifdef CONFIG_CHR_DEV_ST
	{ "st=", st_setup },
#endif
#ifdef CONFIG_BUSMOUSE
	{ "bmouse=", bmouse_setup },
#endif
#ifdef CONFIG_SCSI_SEAGATE
	{ "st0x=", st0x_setup },
	{ "tmc8xx=", tmc8xx_setup },
#endif
#ifdef CONFIG_SCSI_T128
	{ "t128=", t128_setup },
#endif
#ifdef CONFIG_SCSI_PAS16
	{ "pas16=", pas16_setup },
#endif
#ifdef CONFIG_SCSI_GENERIC_NCR5380
	{ "ncr5380=", generic_NCR5380_setup },
#endif
#ifdef CONFIG_SCSI_AHA152X
	{ "aha152x=", aha152x_setup},
#endif
#ifdef CONFIG_SCSI_AHA1542
	{ "aha1542=", aha1542_setup},
#endif
#ifdef CONFIG_SCSI_AHA274X
	{ "aha274x=", aha274x_setup},
#endif
#ifdef CONFIG_SCSI_BUSLOGIC
	{ "buslogic=", buslogic_setup},
#endif
#ifdef CONFIG_BLK_DEV_XD
	{ "xd=", xd_setup },
#endif
#ifdef CONFIG_BLK_DEV_FD
	{ "floppy=", floppy_setup },
#endif
#ifdef CONFIG_MCD
	{ "mcd=", mcd_setup },
#endif
#ifdef CONFIG_AZTCD
	{ "aztcd=", aztcd_setup },
#endif
#ifdef CONFIG_CDU535
	{ "sonycd535=", sonycd535_setup },
#endif CONFIG_CDU535
#ifdef CONFIG_SOUND
	{ "sound=", sound_setup },
#endif
#ifdef CONFIG_SBPCD
	{ "sbpcd=", sbpcd_setup },
#endif CONFIG_SBPCD
#ifdef CONFIG_CDU31A
	{ "cdu31a=", cdu31a_setup },
#endif CONFIG_CDU31A
	{ 0, 0 }
};

void ramdisk_setup(char *str, int *ints)
{
   if (ints[0] > 0 && ints[1] >= 0)
      ramdisk_size = ints[1];
}

//根据由parse_options()传下来的字符串，checksetup函数在bootsetups数组中逐个搜索比对
//如果与某一选项的字符串相符，就执行相应的setup_func函数
static int checksetup(char *line)
{
	int i = 0;
	int ints[11];

	while (bootsetups[i].str) {
		int n = strlen(bootsetups[i].str);
		if (!strncmp(line,bootsetups[i].str,n)) {
			bootsetups[i].setup_func(get_options(line+n,ints), ints);
			return 1;
		}
		i++;
	}
	return 0;
}

unsigned long loops_per_sec = 1;

static void calibrate_delay(void)
{
	int ticks;

	printk("Calibrating delay loop.. ");
	while (loops_per_sec <<= 1) {
		/* wait for "start of" clock tick */
		ticks = jiffies;
		while (ticks == jiffies)
			/* nothing */;
		/* Go .. */
		ticks = jiffies;
		__delay(loops_per_sec);
		ticks = jiffies - ticks;
		if (ticks >= HZ) {
			loops_per_sec = muldiv(loops_per_sec, HZ, ticks);
			printk("ok - %lu.%02lu BogoMips\n",
				loops_per_sec/500000,
				(loops_per_sec/5000) % 100);
			return;
		}
	}
	printk("failed\n");
}


/*
 * This is a simple kernel command line parsing function: it parses
 * the command line, and fills in the arguments/environment to init
 * as appropriate. Any cmd-line option is taken to be an environment
 * variable if it contains the character '='.
 *
 *
 * This routine also checks for options meant for the kernel.
 * These options are not given to init - they are for internal kernel use only.
 */
/*
	分析由内核引导程序发送给内核的启动选项，在初始化过程中按照某些选项运行
	(通过checksetup调用相应选项对应的bootsetups结构中的setup_func函数)，
	并将剩余部分传送给init进程。这些选项可能已经存储在配置文件中，也可能是
	由用户在系统启动时敲入的。但内核并不关心这些，这些细节都是内核引导程序
	关注的内容，嵌入式系统更是如此

	Linux内核在启动的时候，能接收某些命令行选项或启动时参数。当内核不能识别
	某些硬件进而不能设置硬件参数或者为了避免内核更改某些参数的值，可以通过这
	种方式手动将这些参数传递给内核。

	如果不使用启动管理器，比如直接从BIOS或者把内核文件用“cp zImage /dev/fd0”
	等方法直接从设备启动，就不能给内核传递参数或选项－－这也许是我们使用引导
	管理器比如LILO的好处之一
*/
/*
	Linux的内核参数是以空格分开的一个字符串列表，通常具有如下形式：
	name[=value_1][,value_2]...[,value_10]
	“name”是关键字，内核用它来识别应该把“关键字”后面的值传递给谁，也就是如何处理这个值，
	是传递给处理例程还是作为环境变量或者抛给“init”。值的个数限制为10，你可以通过再次使
	用该关键字使用超过10个的参数。
	首先，内核检查关键字是不是 `root='',`nfsroot='', `nfsaddrs='', `ro'', `rw'', `debug'
	或 `init''，然后内核在bootsetups数组里搜索于该关键字相关联的已注册的处理函数，如果找到
	相关的已注册的处理函数，则调用这些函数并把关键字后面的值作为参数传递给这些函数。比如你在
	启动时设置参数name＝a,b,c,d，内核搜索bootsetups数组，如果发现“name”已注册，则调用“name”
	的设置函数如name_setup()，并把a,b,c,d传递给name_setup()执行。

	所有型如“name＝value”参数，如果没有被上面所述的设置函数接收，将被解释为系统启动后的环境变
	量，比如“TERM=vt100”就会被作为一个启动时参数。
	
	所有没有被内核设置函数接收也没又被设置成环境变量的参数都将留给init进程处理，比如“single”。 
*/
static void parse_options(char *line)
{
	char *next;
	char *devnames[] = { "hda", "hdb", "hdc", "hdd", "sda", "sdb", "sdc", "sdd", "sde", "fd", "xda", "xdb", NULL };
	int devnums[]    = { 0x300, 0x340, 0x1600, 0x1640, 0x800, 0x810, 0x820, 0x830, 0x840, 0x200, 0xD00, 0xD40, 0};
	int args, envs;

	if (!*line)
		return;
	args = 0;
	envs = 1;	/* TERM is set to 'console' by default */
	next = line;
	while ((line = next) != NULL) {
/*
		strchr()函数：
				功能：查找字符串s中首次出现字符c的位置
				返回值：成功则返回要查找字符第一次出现的位置，失败返回NULL
*/
		if ((next = strchr(line,' ')) != NULL)
			*next++ = 0;	//如果找到第一个' '，则next此时已指向此' '
		/*
		 * check for kernel options first..
		 */
		//如果选项为"root="，表示要将文件系统的总根建立在指定的设备上
		if (!strncmp(line,"root=",5)) {
			int n;
			line += 5;  //由此可见"root=/dev/xxx"中，等号左右不可出现空格
			if (strncmp(line,"/dev/",5)) {
				ROOT_DEV = simple_strtoul(line,NULL,16);	//simple_strtoul定义于linux/lib/vsprintf.c
				continue;
			}
			line += 5;
			for (n = 0 ; devnames[n] ; n++) {
				int len = strlen(devnames[n]);
				if (!strncmp(line,devnames[n],len)) {
					ROOT_DEV = devnums[n]+simple_strtoul(line+len,NULL,0);
					break;
				}
			}
			continue;
		}
		if (!strcmp(line,"ro")) {
			root_mountflags |= MS_RDONLY;
			continue;
		}
		if (!strcmp(line,"rw")) {
			root_mountflags &= ~MS_RDONLY;
			continue;
		}
		if (!strcmp(line,"debug")) {
			console_loglevel = 10;
			continue;
		}
		if (checksetup(line))
			continue;
		/*
		 * Then check if it's an environment variable or
		 * an option.
		 */
		//所有型如“name＝value”参数，如果没有被上面所述的设置函数接收，将被解释为系统启动后的环境变量
		//比如“TERM=vt100”就会被作为一个启动时参数
		if (strchr(line,'=')) {
			if (envs >= MAX_INIT_ENVS)
				break;
			envp_init[++envs] = line;
		//	所有没有被内核设置函数接收也没又被设置成环境变量的参数都将留给init进程处理，比如“single”
		} else {
			if (args >= MAX_INIT_ARGS)
				break;
			//init进程环境变量参数数组
			argv_init[++args] = line;
		}
	}
	//在后面的init函数中exec函数执行init进程时将这两个数组作为参数数组和环境变量传递给init进程
	argv_init[args+1] = NULL;
	envp_init[envs+1] = NULL;
}

extern void check_bugs(void);
extern void setup_arch(char **, unsigned long *, unsigned long *);

/*
	head.s在最后部分调用main.c中的start_kernel()函数，从而把控制权交给了它。所以启动
	程序从start_kernel()函数继续执行。这个函数是main.c乃至整个操作系统初始化的最重要
	的函数，一旦它执行完了，整个操作系统的初始化也就完成了
	
	计算机在执行start_kernel()前处已经进入了386的保护模式，设立了中断向量表并部分初
	始化了其中的几项，建立了段和页机制，设立了九个段，把线性空间中用于存放系统数据和代
	码的地址映射到了物理空间的头4MB，可以说我们已经使386处理器完全进入了全面执行操作系
	统代码的状态。但直到目前为止，我们所做的一切可以说都是针对386处理器所做的工作，也就
	是说几乎所有的多任务操作系统只要使用386处理器，都需要作这一切。而一旦start_kernel()
	开始执行，Linux内核的真实面目就一步步的展现在你的眼前了。start_kernel()执行后，你就
	可以以一个用户的身份登录和使用Linux了
*/
asmlinkage void start_kernel(void)
{
	char * command_line;
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
	//开始初始化自身的部分组件（包括内存，硬件终端，调度等）
	//初始化内存,返回内核参数和内核可用的物理地址范围
/*
	setup_arch()主要用于对处理器、内存等最基本的硬件相关部分的初始化，如初始化处理器的类型
	（是在386，486，还是586的状态下工作，这是有必要的，比如说，Pentium芯片支持4MB大小的页，
	而386就不支持），初始化RAM盘所占用的空间（如果你安装了RAM盘的话）等。其中，setup_arch()
	给系统分配了intel系列芯片统一使用的几个I/O端口的口地址

	setup_arch()函数是start_kernel阶段最重要的一个函数，每个体系都有自己的setup_arch()函数，
	是体系结构相关的，具体编译哪个体系的setup_arch()函数，由顶层Makefile中的ARCH变量决定

	setup_arch() (arch/i386/kernel/setup.c)；设置内核可用物理地址范围（memory_start~memory_end）；
	设置init_task.mm的范围；调用request_region（kernel/resource.c）申请I/O空间
*/
	setup_arch(&command_line, &memory_start, &memory_end);	//设置与体系结构相关的环境

	//它的具体作用是把线性地址中尚未映射到物理地址上的部分通过页机制进行映射。在这里需要特别
	//强调的是，当paging_init()函数调用完后，页的初始化就整个完成了,函数返回内存起始地址
	/*取消虚拟地址0x0对物理地址的低端4MB空间的映射；根据物理地址的实际大小初始化所有的页表*/
	memory_start = paging_init(memory_start,memory_end); /*该函数定义于arch/i386/mm/init.c 页表结构初始化*/
	//这个初始化程序是对中断向量表进行初始化，它通过调用set_trap_gate（或set_system_gate等）
	//宏对中断向量表的各个表项填写相应的中断响应程序的偏移地址
/*
	事实上，Linux操作系统仅仅在运行trap_init（）函数前使用BIOS的中断响应程序（我们这里先不考
	虑V86模式）。一旦真正进入了Linux操作系统，BIOS的中断向量将不再使用。对于软中断，Linux提供
	一套调用十分方便的中断响应程序，对于硬件设备，Linux要求设备驱动程序提供完善的中断响应程序，
	而调用使用多个参数的BIOS中断就被这些中断响应程序完全代替了。

	另外，在trap_init（）函数里，还要初始化第一个任务的Ldt和TSS，把它们填入Gdt相应的表项中。第
	一个任务就是init_task这个进程，填写完后，还要把init_task的TSS和LDT描述符分别读入系统的TSS
	和LDT寄存器。

	在IDT中设置各种入口地址，如异常事件处理程序入口，系统调用入口，调用门等。其中，trap0~trap17
	为各种错误入口（溢出，0除，页错误等，错误处理函数定义在arch/i386/kernel/entry.s）；
	trap18~trap47保留；设置系统调用（INT 0x80）的入口为system_call（arch/i386/kernel/entry.s）；
	在GDT中设置0号进程的TSS段描述符和LDT段描述符
*/
	//调用trap_init()函数和init_IRQ()函数对i386 CPU中断机制的核心——IDT进行初始化设置
	trap_init();	//初始化硬件中断 该函数在arch/i386/kernel/traps.c中定义
/*
	这个函数也是与中断有关的初始化函数。不过这个函数与硬件设备的中断关系更密切一些。
	我们知道intel的80386系列采用两片8259作为它的中断控制器。这两片级连的芯片一共可以提供16个引脚，
	其中15个与外部设备相连，一个用于级连。可是，从操作系统的角度来看，怎么知道这些引脚是否已经使用;
	如果一个引脚已被使用，Linux操作系统又怎么知道这个引脚上连的是什么设备呢?
	
	函数为两片8259分配了I/O地址，对应于连接在管脚上的硬中断，它初始化了从0x20开始的中断向量表的15个
	表项（386中断门），不过，这时的中断响应程序由于中断控制器的引脚还未被占用，自然是空程序了。当我
	们确切地知道了一个引脚到底连接了什么设备，并知道了该设备的驱动程序后，使用setup_x86_irq这个函数
	填写该引脚对应的386的中断门时，中断响应程序的偏移地址才被填写进中断向量表
*/
	//初始化IDT中0x20~0xff项
	init_IRQ();	/* 在arch/i386/kernel/irq.c中定义*/
/*
	看到这个函数的名字可能令你精神一振，终于到了进程调度部分了，但在这里，你非但看不到进程调度程序的
	影子，甚至连进程都看不到一个，这个程序是名副其实的初始化程序：仅仅为进程调度程序的执行做准备。它
	所做的具体工作是调用init_bh函数（在kernel/softirq.c中）把timer，tqueue，immediate三个任
	务队列加入下半部分的数组
*/
	sched_init();	//初始化调度	在/kernel/sched.c中定义
	//这个函数把启动时得到的参数如debug，init等等从命令行的字符串中分离出来，并把这些参数赋给相应的变量。
	//这其实是一个简单的词法分析程序 提取并分析核心启动参数（从环境变量中读取参数，设置相应标志位等待处理）
/*
	对命令行选项进行分析。这些选项是在命令启动时，由内核引导程序（主要是 arch/i386/boot/bootsect.c）装入，
	并存放在empty_zero_page 页的后 2K 字节中。它首先检查命令行参数，并设置相应的变量。然后把环境变量送入数
	组envp_init[]，把参数送入数组argv_init[]中。
*/
	parse_options(command_line);	//分析传给内核的各种选项,并根据选项调用相应的函数，参见本文件中定义的bootsetups结构数组
	//模块机制的初始化
	init_modules();
#ifdef CONFIG_PROFILE
	//剖析器(理睬器)数据结构初始化（prof_buffer和prof_len变量）
	prof_buffer = (unsigned long *) memory_start;
	/* only text is profiled */
	prof_len = (unsigned long) &etext;
	prof_len >>= CONFIG_PROFILE_SHIFT;
	memory_start += prof_len * sizeof(unsigned long);
#endif
	//这个函数用于对终端的初始化。在这里定义的终端并不是一个完整意义上的TTY设备，它只是一个用于打印各种
	//系统信息和有可能发生的错误的出错信息的终端。真正的TTY设备以后还会进一步定义
	memory_start = console_init(memory_start,memory_end);	//初始化控制台 在linux/drivers/char/tty_io.c中定义
	memory_start = bios32_init(memory_start,memory_end);
/*
	kmalloc代表的是kernel_malloc的意思，它是用于内核的内存分配函数。而这个针对kmalloc的初始化函数用来对
	内存中可用内存的大小进行检查，以确定kmalloc所能分配的内存的大小。所以，这种检查只是检测当前在系统段内
	可分配的内存块的大小
*/
	memory_start = kmalloc_init(memory_start,memory_end);	/*在linux/mm/kmalloc.c中定义*/
	sti();
	calibrate_delay();
	memory_start = chr_dev_init(memory_start,memory_end);
	memory_start = blk_dev_init(memory_start,memory_end);
	sti();
#ifdef CONFIG_SCSI
	memory_start = scsi_dev_init(memory_start,memory_end);
#endif
#ifdef CONFIG_INET
	memory_start = net_dev_init(memory_start,memory_end);
#endif
/*
	下面的几个函数是用来对Linux的文件系统进行初始化的，为了便于理解，这里需要把Linux的文件系统的机制稍做介绍。
	不过，这里是很笼统的描述，目的只在于使我们对初始化的解释工作能进行下去

	虚拟文件系统是一个用于消灭不同种类的实际文件系统间（相对于VFS而言，如ext2，fat等实际文件系统存在于某个磁
	盘设备上）差别的接口层。在这里， 不妨把它理解为一个存放在内存中的文件系统。它具体的作用非常明显：Linux对
	文件系统的所有操作都是靠VFS实现的。它把系统支持的各种以不同形式存放于磁盘上或内存中（如proc文件系统）的数
	据以统一的形式调入内存，从而完成对其的读写操作。（Linux可以同时支持许多不同的实际文件系统，就是说，你可以让你
	的一个磁盘分区使用windows的FAT文件系统，一个分区使用Unix的SYS5文件系统，然后可以在这两个分区间拷贝文件）。
	为了完成以及加速这些操作，VFS采用了块缓存，目录缓存（name_cach），索引节点（inode）缓存等各种机制，以下的
	这些函数，就是对这些机制的初始化
*/
	//这个函数是对VFS的索引节点管理机制进行初始化。这个函数非常简单：把用于索引节点查找的哈希表置入内存，
	//再把指向第一个索引节点的全局变量置为空 
	memory_start = inode_init(memory_start,memory_end);	/*在Linux/fs/inode.c中定义初始化内存inode表*/
	memory_start = file_table_init(memory_start,memory_end);	//创建内存文件描述符表
	//这个函数用来对VFS的目录缓存机制进行初始化。先初始化LRU1链表，再初始化LRU2链表
	memory_start = name_cache_init(memory_start,memory_end);	/*在linux/fs/dcache.c中定义*/
	//初始化empty_zero_page；标记已被占用的页
	//对主内存区起始位置的重新确定，标志着主内存区和缓冲区的位置和大小已经全都确定了，于是系统开始调用mem_init()函数
	mem_init(memory_start,memory_end);	/* 在arch/i386/mm/init.c中定义*/
	//这个函数用来对用于指示块缓存的buffer free list初始化
	buffer_init();	/*在linux/fs/buffer.c中定义*/
	//时间、定时器初始化（包括读取CMOS时钟、估测主频、初始化定时器中断等，time_init()）
	/*读取实时时间，重新设置时钟中断irq0的中断服务程序入口*/
	time_init();
	sock_init();
#ifdef CONFIG_SYSVIPC
	//进程间通信机制：信号量、共享内存、消息机制的初始化
	ipc_init();
#endif
	sti();
	check_bugs();	//检查体系结构漏洞

	//输出Linux版本信息（printk(linux_banner),linux_banner定义于version.c）
	printk(linux_banner);

	//fork() 生成进程1，执行init();这里的fork()是内联函数。
	//为了不使用用户栈。进程0从此死循环执行pause();
	//进程1：init 进程，由0进程创建，完成系统的初始化. 是系统中所有其它用户进程的祖先进程
	if (!fork())		/* we count on this going ok */
		init();	//进程1执行init()函数
/*
 * task[0] is meant to be used as an "idle" task: it may not sleep, but
 * it might do some general things like count free pages or it could be
 * used to implement a reasonable LRU algorithm for the paging routines:
 * anything that can be useful, but shouldn't take time from the real
 * processes.
 *
 * Right now task[0] just does a infinite idle loop.
 */
//进程0：Linux引导中创建的第一个进程，完成加载系统后，演变为进程调度、交换及存储管理的进程
//start_kernel()本身所在的执行体，这其实是一个"手工"创建的线程，它在创建了init()线程以后就
//进入cpu_idle()循环了，它不会在进程（线程）列表中出现
	for(;;)
		idle();
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

void init(void)
{
	int pid,i;

	//调用setup取硬盘分区信息hd、加载虚拟盘、加载根文件系统
	//setup()最终调用fs/super.c中的mount_root()函数挂载根文件系统
	//其核心的关键操作即将根文件系统的超级块读入，并建立起与之相关的
	//inode结构，并将此inode结构设置为进程1的当前目录和根目录
	//current->fs->pwd = inode;
	//current->fs->root = inode; 重中之重！
	//正是因为系统中的所有进程（除进程0）都是进程1的子进程，所以所有的进程
	//都继承了此根文件系统 也正是因为如此，所以进程1绝不能被杀死
	//linux中用kill函数给init进程发送一个终止信号有什么后果？
	//init进程是特殊进程，它不接收也不处理信号。发送终止信号给它是不会有任何结果的。 
/*
	Linux内核启动的下一过程是启动第一个进程init，但必须以根文件系统为载体，
	所以在启动init之前，还要挂载根文件系统
	在系统关闭之前，init 进程一直存活，因为它创建和监控在操作系统外层执行的所有进程的活动。
*/
	setup();
	sprintf(term, "TERM=con%dx%d", ORIG_VIDEO_COLS, ORIG_VIDEO_LINES);

	#ifdef CONFIG_UMSDOS_FS
	{
		/*
			When mounting a umsdos fs as root, we detect
			the pseudo_root (/linux) and initialise it here.
			pseudo_root is defined in fs/umsdos/inode.c
		*/
		extern struct inode *pseudo_root;
		if (pseudo_root != NULL){
			current->fs->root = pseudo_root;
			current->fs->pwd  = pseudo_root;
		}
	}
	#endif

/*
	 内核初始化结束后, 打开控制台终端设备/dev/console作为输入输出去运行/sbin/init. 
	/dev/console的设备号是0x0501,在打开时被定向到命令行(console=)指定的终端设备,
	 缺省情况下为/dev/tty1, 其设备号为0x0401. init程序完成用户初始化完成后启动若
	干个终端守护程序(getty). getty在打开所守护的终端文件之前先使用setsid()创建会话
	组并成为会话组的首领进程(leader=1), 首领进程的会话号(session)和进程组号(pgrp)等
	于进程号(pid).
 	
	首领进程打开终端文件后成为终端的控制进程, 其tty指针指向该终端打开结构, 该终端也成为
	本次会话的控制终端,其session号和pgrp号分别等于进程的session号和pgrp号. 当用户从
	终端登录后, 登录bash程序置换getty进程成为首领进程.

	 进程的会话号和组号随着进程的复制而传递, 子进程的首领标志复位.

	bash在用execve()执行用户程序之前将用setpgid()为每一程序建立属于它自已的进程组
	(使进程组号等于进程号), 再用对终端的(TIOCSPGRP)设备控制将终端进程组号设为将该进程号, 
	使终端切换到新的进程组. 终端进程组以外的进程组如果进行读操作将在其进程组中生成SIGTTIN
	信号, 缺省的信号处理器使进程进入暂停.
*/
	//设置终端标准IO
	(void) open("/dev/tty1",O_RDWR,0);	//调用终端tty驱动的打开程序，见tty_io.c文件
	(void) dup(0);
	(void) dup(0);

	//执行init进程
/*
	Linux内核启动后的最后一个动作，就是从根文件系统上找出并执行init服务。Linux内核会依照下列的顺序寻找init服务：
	1)/sbin/是否有init服务
	2)/etc/是否有init服务
	3)/bin/是否有init服务
	4)如果都找不到最后执行/bin/sh  ？？？
*/
/*
	找到init服务后，Linux会让init服务负责后续初始化系统使用环境的工作，init启动后，就代表系统已经顺利地启动了linux内核。
	启动init服务时，init服务会读取/etc/inittab文件，根据/etc/inittab中的设置数据进行初始化系统环境的工作。/etc/inittab
	定义init服务在linux启动过程中必须依序执行以下几个Script： /etc/rc.d/rc.sysinit /etc/rc.d/rc /etc/rc.d/rc.local

	/etc/rc.d/rc.sysinit主要的功能是设置系统的基本环境，当init服务执行rc.sysinit时 要依次完成下面一系列工作：
	(1)启动udev  (2)设置内核参数，执行sysctl –p,以便从/etc/sysctl.conf设置内核参数  (3)设置系统时间  (4)启用交换内存空间
	(5)检查并挂载所有文件系统  (6)初始化硬件设备：  a)定义在/etc/modprobe.conf的模块   b)ISA PnP的硬件设备   c)USB设备
	(7)初始化串行端口设备  (8)清除过期的锁定文件与IPC文件  (9)建立用户接口  (10)建立虚拟控制台

		Init 执行流程
	sysvinit 时代 Linux 启动流程：
	1. BIOS
	2. MBR
	3. Grub
	4. Linux-Kernel
	5. Init
		a. 运行 /sbin/init
		b. 读取 /etc/inittab（该文件定义了运行级别，在Ubuntu中演变为 /etc/init/rc-sysinit.conf）
		c. 执行 /etc/rc.d/rc-sysinit （Ubuntu中是 /etc/init/rc-sysinit.conf）
		d. 执行 /etc/rcS.d/ 目录下的脚本
		e. 切换到默认的运行级别执行（Ubuntu默认执行  " env DEFAULT_RUNLEVEL=2 "     telinit  "${DEFAULT_RUNLEVEL}" ） 
		执行 /etc/rc2.d/ 目录下的脚本
		f.  执行 /etc/rc.local （用户自定义的开机服务可放到该脚本中）
		g.  执行 /bin/login （此时，已完成 Init 全过程）
	6. 完成系统初始化，用户登录。

	Android中的init进程与Linux不同，其职责可以归结如下：
	作为守护进程
	解析和执行init.rc文件
	生成设备驱动节点
	属性服务
	http://blog.csdn.net/zhgxhuaa/article/details/22994095 带有init源码
*/
	execve("/etc/init",argv_init,envp_init);
	execve("/bin/init",argv_init,envp_init);
	execve("/sbin/init",argv_init,envp_init);  //若执行init成功，则进程1即init进程，等价
	/* if this fails, fall through to original stuff */
	
	//若execve执行用户进程init失败，则会执行到这里，进行内核级的init默认处理
	//而若execve成功，则不会执行到这里，因为execve不会返回了，我觉得用户init程序
	//的实现必定不会调用exit()函数退出，因为若退出，那么init进程就真的退出了。

	//创建进程2以/etc/rc为标准输入文件执行shell，完成rc文件中的命令
	if (!(pid=fork())) {
		close(0);
		if (open("/etc/rc",O_RDONLY,0))
			_exit(1);
		execve("/bin/sh",argv_rc,envp_rc);
		_exit(2);
	}
	//执行到这里，说明是父进程即进程1，若之前的init服务成功execve，则进程1也即init进程
	//执行到这里，也说明init服务执行完毕，即init服务完成了后续初始化系统使用环境的工作
	//init等进程2退出，进入死循环：创建子进程，建立新会话，设置标准IO终端，
	//以登录方式执行shell，剩下的动作由用户来决定了
	if (pid>0)
		while (pid != wait(&i))
			/* nothing */;
	while (1) {
		if ((pid = fork()) < 0) {
			printf("Fork failed in init\n\r");
			continue;
		}
		if (!pid) {
			close(0);close(1);close(2);
/*
			getty在打开所守护的终端文件之前先使用setsid()创建会话组并成为会话组的首领进程(leader=1),
			 首领进程的会话号(session)和进程组号(pgrp)等于进程号(pid). 首领进程打开终端文件后成为终端的
			控制进程, 其tty指针指向该终端打开结构, 该终端也成为本次会话的控制终端, 其session号和pgrp号
			分别等于进程的session号和pgrp号. 当用户从终端登录后, 登录bash程序置换getty进程成为首领进程
*/
			setsid();
			(void) open("/dev/tty1",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();
	}
	_exit(0);
}
