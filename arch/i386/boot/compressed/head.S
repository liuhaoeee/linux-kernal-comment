/*
 *  linux/boot/head.S
 *
 *  Copyright (C) 1991, 1992, 1993  Linus Torvalds
 */

/*
 *  head.S contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00001000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory.
 *deliberately:故意地;深思熟虑地;从容不迫地
 * Page 0 is deliberately kept safe, since System Management Mode code in 
 * laptops may need to access the BIOS data stored there.  This is also
 * useful for future device drivers that either access the BIOS via VM86 
 * mode.
 */
/*
	当内核映像被加载到内存中，并且阶段 2 的引导加载程序释放控制权之后，内核阶段就开始了。
	内核映像并不是一个可执行的内核，而是一个压缩过的内核映像。通常它是一个 zImage（压缩
	映像，小于 512KB）或一个 bzImage（较大的压缩映像，大于 512KB），它是提前使用 zlib 
	进行压缩过的。在这个内核映像前面是一个例程，它实现少量硬件设置，并对内核映像中包含
	的内核进行解压，然后将其放入高端内存中，如果有初始 RAM 磁盘映像，就会将它移动到内存中，
	并标明以后使用。然后该例程会调用内核，并开始启动内核引导的过程。

	当 bzImage（用于 i386 映像）被调用时，我们从 ./arch/i386/boot/head.S 的 start 
	汇编例程开始执行（主要流程图请参看图 3）。这个例程会执行一些基本的硬件设置，并调用 
	./arch/i386/boot/compressed/head.S 中的 startup_32 例程。此例程会设置一个
	基本的环境（堆栈等），并清除 Block Started by Symbol（BSS）。然后调用一个叫做 
	decompress_kernel 的 C 函数（在 ./arch/i386/boot/compressed/misc.c 中）来
	解压内核。当内核被解压到内存中之后，就可以调用它了。这是另外一个 startup_32 函数，
	但是这个函数在 ./arch/i386/kernel/head.S 中。

	在这个新的 startup_32 函数（也称为清除程序或进程 0）中，会对页表进行初始化，并启
	用内存分页功能。然后会为任何可选的浮点单元（FPU）检测 CPU 的类型，并将其存储起来
	供以后使用。然后调用 start_kernel 函数（在 init/main.c 中），它会将您带入与体
	系结构无关的 Linux 内核部分。实际上，这就是 Linux 内核的 main 函数。
*/
/*
	0000:1000处是compressed/head.S的代码,它首先将堆栈设置到user_stack[]数组尾端，
	然后检查A20是否开启，BSS段清零，调用decompress_kernel()进行内核解压，解压后
	的内核位于内存0x100000处，解压返回后跳转到0x100000处运行内核代码(arch/i386/head.S
	的startup_32处)
	此前的setup模块已经使CPU进入保护模式了
*/
.text

#define __ASSEMBLY__
#include <asm/segment.h>

startup_32:
	cld
	cli
	movl $(KERNEL_DS),%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
	lss _stack_start,%esp
	xorl %eax,%eax
1:	incl %eax		# check that A20 really IS enabled
	movl %eax,0x000000	# loop forever if it isn't
	cmpl %eax,0x100000
	je 1b
/*
 * Initialize eflags.  Some BIOS's leave bits like NT set.  This would
 * confuse the debugger if this code is traced.
 * XXX - best to initialize before switching to protected mode.
 */
	pushl $0
	popfl
/*
 * Clear BSS
 */
	xorl %eax,%eax
	movl $__edata,%edi
	movl $__end,%ecx
	subl %edi,%ecx
	cld
	rep
	stosb
/*
 * Do the decompression, and jump to the new kernel..
 */
	//将内核解压缩到0x100000即1M处并跳转到那里执行
	//解压函数位于arch/i386/boot/compressed/misc.c
	call _decompress_kernel
	ljmp $(KERNEL_CS), $0x100000
