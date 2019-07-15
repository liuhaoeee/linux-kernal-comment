#ifndef __A_OUT_GNU_H__
#define __A_OUT_GNU_H__

#define __GNU_EXEC_MACROS__

#ifndef __STRUCT_EXEC_OVERRIDE__

//所有的a.out格式的可执行文文件（二进制代码）的开头都应该是一个exec数据结构
//即a.out文件头，用于指明文件的组成情况,方便访问文件的各个部分
struct exec
{
  unsigned long a_info;		/* Use macros N_MAGIC, etc for access *///高16位是一个代表目标CPU类型的代码，i386就是100；低16位就是magic number
				//该字段含有三个子字段，分别是标志字段、机器类型标识字段和魔数字段
  unsigned a_text;		/* length of text, in bytes *///该字段含有代码段的长度值，字节数
  unsigned a_data;		/* length of data, in bytes *///该字段含有数据段的长度值，字节数
  unsigned a_bss;		/* length of uninitialized data area for file, in bytes */
				//含有bss段的长度，内核用其设置在数据段后初始的break(brk)。
				//内核在加载程序时，这段可写内存显现出处于数据段后面，并且初始时为全零
  unsigned a_syms;		/* length of symbol table data in file, in bytes *///含有符号表部分的字节长度值
  unsigned a_entry;		/* start address *///含有内核将执行文件加载到内存中以后，程序执行起始点的内存地址
  unsigned a_trsize;		/* length of relocation info for text, in bytes *///该字段含有代码重定位表的大小，是字节数
  unsigned a_drsize;		/* length of relocation info for data, in bytes *///该字段含有数据重定位表的大小，是字节数
};

#endif /* __STRUCT_EXEC_OVERRIDE__ */

/* these go in the N_MACHTYPE field */
enum machine_type {
#if defined (M_OLDSUN2)
  M__OLDSUN2 = M_OLDSUN2,
#else
  M_OLDSUN2 = 0,
#endif
#if defined (M_68010)
  M__68010 = M_68010,
#else
  M_68010 = 1,
#endif
#if defined (M_68020)
  M__68020 = M_68020,
#else
  M_68020 = 2,
#endif
#if defined (M_SPARC)
  M__SPARC = M_SPARC,
#else
  M_SPARC = 3,
#endif
  /* skip a bunch so we don't run into any of sun's numbers */
  M_386 = 100,
};

#if !defined (N_MAGIC)
#define N_MAGIC(exec) ((exec).a_info & 0xffff)
#endif
#define N_MACHTYPE(exec) ((enum machine_type)(((exec).a_info >> 16) & 0xff))
#define N_FLAGS(exec) (((exec).a_info >> 24) & 0xff)
#define N_SET_INFO(exec, magic, type, flags) \
	((exec).a_info = ((magic) & 0xffff) \
	 | (((int)(type) & 0xff) << 16) \
	 | (((flags) & 0xff) << 24))
#define N_SET_MAGIC(exec, magic) \
	((exec).a_info = (((exec).a_info & 0xffff0000) | ((magic) & 0xffff)))

#define N_SET_MACHTYPE(exec, machtype) \
	((exec).a_info = \
	 ((exec).a_info&0xff00ffff) | ((((int)(machtype))&0xff) << 16))

#define N_SET_FLAGS(exec, flags) \
	((exec).a_info = \
	 ((exec).a_info&0x00ffffff) | (((flags) & 0xff) << 24))

/* Code indicating object file or impure(假的) executable.  */
//表示代码和数据段紧随在执行头后面并且是连续存放的。内核将代码和数据段
//都加载到可读写内存中。编译器编译出的目标文件的魔数是OMAGIC(八进制0407)

//OMAGIC格式在文件头后有连续段，没有文本和数据的分离。也被用作目标文件格式
#define OMAGIC 0407
/* Code indicating pure executable.  */
//同OMAGIC一样，代码和数据段紧随在执行头后面并且是连续存放的。然而内
//核将代码加载到了只读内存中，并把数据段加载到代码段后下一页可读写内存边界开始

//NMAGIC格式与OMAGIC相像，但数据段出现在文本段结束后的下一页，且文本段被标为只读
#define NMAGIC 0410
/* Code indicating demand-paged executable.  */
//内核在必要时从二进制执行文件中加载独立的页面。执行头部、代码段和数据段都被链
//接程序处理成多个页面大小的块。内核加载的代码页面是只读的，而数据段的页面是可写的。
//链接生成的可执行文件的魔数即是ZMAGIC(0413，即0x10b)

//ZMAGIC格式加入了对按需分页的支持，代码段和数据段的长度需要是页宽的整数倍
#define ZMAGIC 0413
/* This indicates a demand-paged executable with the header in the text. 
   The first page is unmapped to help trap NULL pointer references */
/*
	QMAGIC是一种类似旧格式的a.out（亦称为ZMAGIC）的可执行档格式，这种格式会
	使得第一个分页无法map。当0-4096的范围内没有mapping存在时，则可允许NULL 
	dereference trapping更加的容易。所产生的边界效应是你的执行档会比较小（大约少1K左右）。
        只有即将作废的连结器有支持ZMAGIC，一半已埋入棺材的连结器有支持这两种格式；
	而目前的版本仅支持QMAGIC而已。事实上，这并没有多大的影响，那是因为目前的
	核心两种格式都能执行。 
*/
//QMAGIC二进制文件通常被加载在虚拟地址池的底端，用以通过段错误捕获对空指针的解引用。
//a.out头部与文本段的第一页合并，通常会省下一页的内存
#define QMAGIC 0314

/* Code indicating core file.  */
//旧版的Linux使用此格式来存放核心转储
#define CMAGIC 0421
/*
  在a.out.h头文件中定义了几个宏，这些宏使用exec结构来测试一致性或者定位执行文件中各个部分(节)的位置偏移值。这些宏有：

　　◆N_BADMAG(exec)。如果a_magic字段不能被识别，则返回非零值。

　　◆N_TXTOFF(exec)。代码段的起始位置字节偏移值。

　　◆N_DATOFF(exec)。数据段的起始位置字节偏移值。

　　◆N_DRELOFF(exec)。数据重定位表的起始位置字节偏移值。

　　◆N_TRELOFF(exec)。代码重定位表的起始位置字节偏移值。

　　◆N_SYMOFF(exec)。符号表的起始位置字节偏移值。

　　◆N_STROFF(exec)。字符串表的起始位置字节偏移值。
*/
#if !defined (N_BADMAG)
#define N_BADMAG(x)	  (N_MAGIC(x) != OMAGIC		\
			&& N_MAGIC(x) != NMAGIC		\
  			&& N_MAGIC(x) != ZMAGIC \
		        && N_MAGIC(x) != QMAGIC)
#endif

#define _N_HDROFF(x) (1024 - sizeof (struct exec))

#if !defined (N_TXTOFF)
#define N_TXTOFF(x) \
 (N_MAGIC(x) == ZMAGIC ? _N_HDROFF((x)) + sizeof (struct exec) : \
  (N_MAGIC(x) == QMAGIC ? 0 : sizeof (struct exec)))
#endif

#if !defined (N_DATOFF)
#define N_DATOFF(x) (N_TXTOFF(x) + (x).a_text)
#endif

#if !defined (N_TRELOFF)
#define N_TRELOFF(x) (N_DATOFF(x) + (x).a_data)
#endif

#if !defined (N_DRELOFF)
#define N_DRELOFF(x) (N_TRELOFF(x) + (x).a_trsize)
#endif

#if !defined (N_SYMOFF)
#define N_SYMOFF(x) (N_DRELOFF(x) + (x).a_drsize)
#endif

#if !defined (N_STROFF)
#define N_STROFF(x) (N_SYMOFF(x) + (x).a_syms)
#endif

/* Address of text segment in memory after it is loaded.  */
#if !defined (N_TXTADDR)
#define N_TXTADDR(x) (N_MAGIC(x) == QMAGIC ? PAGE_SIZE : 0)
#endif

/* Address of data segment in memory after it is loaded.
   Note that it is up to you to define SEGMENT_SIZE
   on machines not listed here.  */
#if defined(vax) || defined(hp300) || defined(pyr)
#define SEGMENT_SIZE page_size
#endif
#ifdef	sony
#define	SEGMENT_SIZE	0x2000
#endif	/* Sony.  */
#ifdef is68k
#define SEGMENT_SIZE 0x20000
#endif
#if defined(m68k) && defined(PORTAR)
#define PAGE_SIZE 0x400
#define SEGMENT_SIZE PAGE_SIZE
#endif

#ifdef linux
#include <asm/page.h>
#define SEGMENT_SIZE	1024
#endif

#define _N_SEGMENT_ROUND(x) (((x) + SEGMENT_SIZE - 1) & ~(SEGMENT_SIZE - 1))

#define _N_TXTENDADDR(x) (N_TXTADDR(x)+(x).a_text)

#ifndef N_DATADDR
#define N_DATADDR(x) \
    (N_MAGIC(x)==OMAGIC? (_N_TXTENDADDR(x)) \
     : (_N_SEGMENT_ROUND (_N_TXTENDADDR(x))))
#endif

/* Address of bss segment in memory after it is loaded.  */
#if !defined (N_BSSADDR)
#define N_BSSADDR(x) (N_DATADDR(x) + (x).a_data)
#endif

#if !defined (N_NLIST_DECLARED)
/*
	符号将名称映射为地址(或者更通俗地讲是字符串映射到值)。由于链接程序对地址的调整，
	一个符号的名称必须用来表示其地址，直到已被赋予一个绝对地址值。符号是由符号表中
	固定长度的记录以及字符串表中的可变长度名称组成。符号表是nlist结构的一个数组，
	如下所示
*/
struct nlist {
  union {
    char *n_name;
    struct nlist *n_next;
    long n_strx;
  } n_un;
  unsigned char n_type;
  char n_other;
  short n_desc;
  unsigned long n_value;
};
#endif /* no N_NLIST_DECLARED.  */
/*
◆N_UNDF。一个未定义的符号。链接程序必须在其他二进制目标文件中定位一个具有相同名称的外部符号，以确定该符号的绝对数据值。特殊情况下，如果n_type字段是非零值，并且没有二进制文件定义了这个符号，则链接程序在BSS段中将该符号解析为一个地址，保留长度等于n_value的字节。如果符号在多于一个二进制目标文件中都没有定义并且这些二进制目标文件对其长度值不一致，则链接程序将选择所有二进制目标文件中最大的长度。

　　◆N_ABS。一个绝对符号。链接程序不会更新一个绝对符号。

　　◆N_TEXT。一个代码符号。该符号的值是代码地址，链接程序在合并二进制目标文件时会更新其值。

　　◆N_DATA。一个数据符号。与N_TEXT类似，但是用于数据地址。对应代码和数据符号的值不是文件的偏移值而是地址;为了找出文件的偏移，就有必要确定相关部分开始加载的地址并减去它，然后加上该部分的偏移。

　　◆N_BSS。一个BSS符号。与代码或数据符号类似，但在二进制目标文件中没有对应的偏移。

　　◆N_FN。一个文件名符号。在合并二进制目标文件时，链接程序会将该符号插入在二进制文件中的符号之前。符号的名称就是给予链接程序的文件名，而其值是二进制文件中首个代码段地址。链接和加载时不需要文件名符号，但对于调式程序非常有用。

　　◆N_STAB。屏蔽码用于选择符号调式程序(例如gdb)感兴趣的位。其值在stab()中说明。
*/
#if !defined (N_UNDF)
#define N_UNDF 0
#endif
#if !defined (N_ABS)
#define N_ABS 2
#endif
#if !defined (N_TEXT)
#define N_TEXT 4
#endif
#if !defined (N_DATA)
#define N_DATA 6
#endif
#if !defined (N_BSS)
#define N_BSS 8
#endif
#if !defined (N_FN)
#define N_FN 15
#endif

#if !defined (N_EXT)
#define N_EXT 1
#endif
#if !defined (N_TYPE)
#define N_TYPE 036
#endif
#if !defined (N_STAB)
#define N_STAB 0340
#endif

/* The following type indicates the definition of a symbol as being
   an indirect reference to another symbol.  The other symbol
   appears as an undefined reference, immediately following this symbol.

   Indirection is asymmetrical.  The other symbol's value will be used
   to satisfy requests for the indirect symbol, but not vice versa.
   If the other symbol does not have a definition, libraries will
   be searched to find a definition.  */
#define N_INDR 0xa

/* The following symbols refer to set elements.
   All the N_SET[ATDB] symbols with the same name form one set.
   Space is allocated for the set in the text section, and each set
   element's value is stored into one word of the space.
   The first word of the space is the length of the set (number of elements).

   The address of the set is made into an N_SETV symbol
   whose name is the same as the name of the set.
   This symbol acts like a N_DATA global symbol
   in that it can satisfy undefined external references.  */

/* These appear as input to LD, in a .o file.  */
#define	N_SETA	0x14		/* Absolute set element symbol */
#define	N_SETT	0x16		/* Text set element symbol */
#define	N_SETD	0x18		/* Data set element symbol */
#define	N_SETB	0x1A		/* Bss set element symbol */

/* This is output from LD.  */
#define N_SETV	0x1C		/* Pointer to set vector in data area.  */

#if !defined (N_RELOCATION_INFO_DECLARED)
/* This structure describes a single relocation to be performed.
   The text-relocation section of the file is a vector of these structures,
   all of which apply to the text section.
   Likewise, the data-relocation section applies to the data section.  */
//重定位记录具有标准的格式，它使用重定位信息(relocation_info)结构来描述
/*
	该结构中各字段的含义如下：

　　1)r_address——该字段含有需要链接程序处理(编辑)的指针的字节偏移值。代码重定位的偏移值是从代码段开始处计数的，数据重定位的偏移值是从数据段开始处计算的。链接程序会将已经存储在该偏移处的值与使用重定位记录计算出的新值相加。

　　2)r_symbolnum——该字段含有符号表中一个符号结构的序号值(不是字节偏移值)。链接程序在算出符号的绝对地址以后，就将该地址加到正在进行重定位的指针上。(如果r_extern比特位是0，那么情况就不同，见下面。)

　　3)r_pcrel——如果设置了该位，链接程序就认为正在更新一个指针，该指针使用pc相关寻址方式，是属于机器码指令部分。当运行程序使用这个被重定位的指针时，该指针的地址被隐式地加到该指针上。

　　4)r_length——该字段含有指针长度的2的次方值：0表示1字节长，1表示2字节长，2表示4字节长。

　　5)r_extern——如果被置位，表示该重定位需要一个外部引用;此时链接程序必须使用一个符号地址来更新相应指针。当该位是0时，则重定位是“局部”的。链接程序更新指针以反映各个段加载地址中的变化，而不是反映一个符号值的变化。在这种情况下，r_symbolnum字段的内容是一个n_type值;这类字段告诉链接程序被重定位的指针指向那个段。

　　6)r_pad——Linux系统中没有使用的4个比特位。在写一个目标文件时最好全置0。
*/
struct relocation_info
{
  /* Address (within segment) to be relocated.  */
  int r_address;
  /* The meaning of r_symbolnum depends on r_extern.  */
  unsigned int r_symbolnum:24;
  /* Nonzero means value is a pc-relative offset
     and it should be relocated for changes in its own address
     as well as for changes in the symbol or section specified.  */
  unsigned int r_pcrel:1;
  /* Length (as exponent of 2) of the field to be relocated.
     Thus, a value of 2 indicates 1<<2 bytes.  */
  unsigned int r_length:2;
  /* 1 => relocate with value of symbol.
          r_symbolnum is the index of the symbol
	  in file's the symbol table.
     0 => relocate with the address of a segment.
          r_symbolnum is N_TEXT, N_DATA, N_BSS or N_ABS
	  (the N_EXT bit may be set also, but signifies nothing).  */
  unsigned int r_extern:1;
  /* Four bits that aren't used, but when writing an object file
     it is desirable to clear them.  */
#ifdef NS32K
  unsigned r_bsr:1;
  unsigned r_disp:1;
  unsigned r_pad:2;
#else
  unsigned int r_pad:4;
#endif
};
#endif /* no N_RELOCATION_INFO_DECLARED.  */


#endif /* __A_OUT_GNU_H__ */
