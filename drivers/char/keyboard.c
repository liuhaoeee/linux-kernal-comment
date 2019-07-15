/*
 * linux/drivers/char/keyboard.c
 *
 * Keyboard driver for Linux v0.99 using Latin-1.
 *
 * Written for linux by Johan Myreen as a translation from
 * the assembly version by Linus (with diacriticals added)
 *
 * Some additional features added by Christoph Niemann (ChN), March 1993
 *
 * Loadable keymaps by Risto Kankkunen, May 1993
 *
 * Diacriticals redone & other small changes, aeb@cwi.nl, June 1993
 * Added decr/incr_console, dynamic keymaps, Unicode support,
 * dynamic function/string keys, led setting,  Sept 1994
 * 
 */
/*
	i8042 有 4 个 8 bits 的寄存器，他们是 Status Register（状态寄存器），
	Output Buffer（输出缓冲器），Input Buffer（输入缓冲器），Control 
	Register（控制寄存器）。使用两个 IO 端口，60h 和 64h,前者是命令端口，
	后者是数据端口

	Status Register（状态寄存器）
	状态寄存器是一个8位只读寄存器，任何时刻均可被cpu读取。其各位定义如下

	Bit7: PARITY-EVEN(P_E): 从键盘获得的数据奇偶校验错误
	Bit6: RCV-TMOUT(R_T): 接收超时，置1
	Bit5: TRANS_TMOUT(T_T): 发送超时，置1
	Bit4: KYBD_INH(K_I): 为1，键盘没有被禁止。为0，键盘被禁止。
	Bit3: CMD_DATA(C_D): 为1，输入缓冲器中的内容为命令，为0，输入缓冲器中的内容为数据。
	Bit2: SYS_FLAG(S_F): 系统标志，加电启动置0，自检通过后置1
	Bit1: INPUT_BUF_FULL(I_B_F): 输入缓冲器满置1，i8042 取走后置0
	BitO: OUT_BUF_FULL(O_B_F): 输出缓冲器满置1，CPU读取后置0

	Output Buffer（输出缓冲器）
	输出缓冲器是一个8位只读寄存器。驱动从这个寄存器中读取数据。这些数据包括，扫描码，发往 i8042 命令的响应，间接的发往 i8048 命令的响应。

	Input Buffer（输入缓冲器）
	输入缓冲器是一个8位只写寄存器。缓冲驱动发来的内容。这些内容包括，发往 i8042 的命令，通过 i8042 间接发往 i8048 的命令，以及作为命令参数的数据。

	Control Register（控制寄存器）
	也被称作 Controller Command Byte （控制器命令字节）。其各位定义如下

	Bit7: 保留，应该为0
	Bit6: 将第二套扫描码翻译为第一套
	Bit5: 置1，禁止鼠标
	Bit4: 置1，禁止键盘
	Bit3: 置1，忽略状态寄存器中的 Bit4
	Bit2: 设置状态寄存器中的 Bit2
	Bit1: 置1，enable 鼠标中断
	BitO: 置1，enable 键盘中断

*/
#define KEYBOARD_IRQ 1

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/ioport.h>

#include <asm/bitops.h>

#include "kbd_kern.h"
#include "diacr.h"
#include "vt_kern.h"

#define SIZE(x) (sizeof(x)/sizeof((x)[0]))

#define KBD_REPORT_ERR
#define KBD_REPORT_UNKN
/* #define KBD_IS_FOCUS_9000 */

#ifndef KBD_DEFMODE
#define KBD_DEFMODE ((1 << VC_REPEAT) | (1 << VC_META))
#endif

#ifndef KBD_DEFLEDS
/*
 * Some laptops take the 789uiojklm,. keys as number pad when NumLock
 * is on. This seems a good reason to start with NumLock off.
 */
#define KBD_DEFLEDS 0
#endif

#ifndef KBD_DEFLOCK
#define KBD_DEFLOCK 0
#endif

/*
 * The default IO slowdown is doing 'inb()'s from 0x61, which should be
 * safe. But as that is the keyboard controller chip address, we do our
 * slowdowns here by doing short jumps: the keyboard controller should
 * be able to keep up
 */
#define REALLY_SLOW_IO
#define SLOW_IO_BY_JUMPING
#include <asm/io.h>
#include <asm/system.h>

extern void poke_blanked_console(void);
extern void ctrl_alt_del(void);
extern void reset_vc(unsigned int new_console);
extern void change_console(unsigned int new_console);
extern void scrollback(int);
extern void scrollfront(int);
extern int vc_cons_allocated(unsigned int);

#ifdef __i386__
#define fake_keyboard_interrupt() __asm__ __volatile__("int $0x21")
#else
#define fake_keyboard_interrupt() do ; while (0)
#endif

unsigned char kbd_read_mask = 0x01;	/* modified by psaux.c */

/*
 * global state includes the following, and various static variables
 * in this module: prev_scancode, shift_state, diacr, npadch, dead_key_next.
 * (last_console is now a global variable)
 */

/* shift state counters.. */
static unsigned char k_down[NR_SHIFT] = {0, };
/* keyboard key bitmap */
#define BITS_PER_LONG (8*sizeof(unsigned long))
static unsigned long key_down[256/BITS_PER_LONG] = { 0, };

extern int last_console;
static int want_console = -1;
static int dead_key_next = 0;
/* 
 * In order to retrieve the shift_state (for the mouse server), either
 * the variable must be global, or a new procedure must be created to 
 * return the value. I chose the former way.
 */
/*static*/ int shift_state = 0;
static int npadch = -1;			/* -1 or number assembled on pad */
static unsigned char diacr = 0;
static char rep = 0;			/* flag telling character repeat */
struct kbd_struct kbd_table[MAX_NR_CONSOLES];
static struct tty_struct **ttytab;
static struct kbd_struct * kbd = kbd_table;
static struct tty_struct * tty = NULL;

/* used only by send_data - set by keyboard_interrupt */
static volatile unsigned char reply_expected = 0;
static volatile unsigned char acknowledge = 0;
static volatile unsigned char resend = 0;

extern void compute_shiftstate(void);

typedef void (*k_hand)(unsigned char value, char up_flag);
typedef void (k_handfn)(unsigned char value, char up_flag);

static k_handfn
	do_self, do_fn, do_spec, do_pad, do_dead, do_cons, do_cur, do_shift,
	do_meta, do_ascii, do_lock, do_lowercase, do_ignore;

static k_hand key_handler[16] = {
	do_self, do_fn, do_spec, do_pad, do_dead, do_cons, do_cur, do_shift,
	do_meta, do_ascii, do_lock, do_lowercase,
	do_ignore, do_ignore, do_ignore, do_ignore
};

typedef void (*void_fnp)(void);
typedef void (void_fn)(void);

static void_fn do_null, enter, show_ptregs, send_intr, lastcons, caps_toggle,
	num, hold, scroll_forw, scroll_back, boot_it, caps_on, compose,
	SAK, decr_console, incr_console, spawn_console, bare_num;

static void_fnp spec_fn_table[] = {
	do_null,	enter,		show_ptregs,	show_mem,
	show_state,	send_intr,	lastcons,	caps_toggle,
	num,		hold,		scroll_forw,	scroll_back,
	boot_it,	caps_on,	compose,	SAK,
	decr_console,	incr_console,	spawn_console,	bare_num
};

/* maximum values each key_handler can handle */
const int max_vals[] = {
	255, SIZE(func_table) - 1, SIZE(spec_fn_table) - 1, NR_PAD - 1,
	NR_DEAD - 1, 255, 3, NR_SHIFT - 1,
	255, NR_ASCII - 1, NR_LOCK - 1, 255
};

const int NR_TYPES = SIZE(max_vals);

static void put_queue(int);
static unsigned char handle_diacr(unsigned char);

/* pt_regs - set by keyboard_interrupt(), used by show_ptregs() */
static struct pt_regs * pt_regs;
/*
	当命令被发往i8042的时候，命令被放入输入缓冲器，同时引起状态寄存器的 Bit1 置1，
	表示输入缓冲器满，同时引起状态寄存器的 Bit2 置1，表示写入输入缓冲器的是一个命令。

	向i8042发命令的方法，首先，读取状态寄存器，判断bit1，状态寄存器bit1为0，说明
	输入缓冲器为空，可以写入。保证状态寄存器bit1为0，然后对64h端口进行写操作，写入命令。
	
	正因为上面所述，所以在向键盘控制器发送命令之前需要调用kb_wait()函数等待输入缓冲器空
*/
static inline void kb_wait(void)
{
	int i;

	for (i=0; i<0x100000; i++)
		//读键盘控制器状态，并测试输入缓冲器是否为空，若不为空，则继续循环
		if ((inb_p(0x64) & 0x02) == 0)
			return;
	printk("Keyboard timed out\n");
}

static inline void send_cmd(unsigned char c)
{
	kb_wait();
	outb(c,0x64);
}

/*
 * Many other routines do put_queue, but I think either
 * they produce ASCII, or they produce some user-assigned
 * string, and in both cases we might assume that it is
 * in utf-8 already.
 */
void to_utf8(ushort c) {
    if (c < 0x80)
	put_queue(c);			/*  0*******  */
    else if (c < 0x800) {
	put_queue(0xc0 | (c >> 6)); 	/*  110***** 10******  */
	put_queue(0x80 | (c & 0x3f));
    } else {
	put_queue(0xe0 | (c >> 12)); 	/*  1110**** 10****** 10******  */
	put_queue(0x80 | ((c >> 6) & 0x3f));
	put_queue(0x80 | (c & 0x3f));
    }
    /* UTF-8 is defined for words of up to 31 bits,
       but we need only 16 bits here */
}

/*
 * Translation of escaped scancodes to keycodes.
 * This is now user-settable.
 * The keycodes 1-88,96-111,119 are fairly standard, and
 * should probably not be changed - changing might confuse X.
 * X also interprets scancode 0x5d (KEY_Begin).
 *
 * For 1-88 keycode equals scancode.
 */

#define E0_KPENTER 96
#define E0_RCTRL   97
#define E0_KPSLASH 98
#define E0_PRSCR   99
#define E0_RALT    100
#define E0_BREAK   101  /* (control-pause) */
#define E0_HOME    102
#define E0_UP      103
#define E0_PGUP    104
#define E0_LEFT    105
#define E0_RIGHT   106
#define E0_END     107
#define E0_DOWN    108
#define E0_PGDN    109
#define E0_INS     110
#define E0_DEL     111

#define E1_PAUSE   119

/*
 * The keycodes below are randomly located in 89-95,112-118,120-127.
 * They could be thrown away (and all occurrences below replaced by 0),
 * but that would force many users to use the `setkeycodes' utility, where
 * they needed not before. It does not matter that there are duplicates, as
 * long as no duplication occurs for any single keyboard.
 */
#define SC_LIM 89

#define FOCUS_PF1 85           /* actual code! */
#define FOCUS_PF2 89
#define FOCUS_PF3 90
#define FOCUS_PF4 91
#define FOCUS_PF5 92
#define FOCUS_PF6 93
#define FOCUS_PF7 94
#define FOCUS_PF8 95
#define FOCUS_PF9 120
#define FOCUS_PF10 121
#define FOCUS_PF11 122
#define FOCUS_PF12 123

#define JAP_86     124
/* tfj@olivia.ping.dk:
 * The four keys are located over the numeric keypad, and are
 * labelled A1-A4. It's an rc930 keyboard, from
 * Regnecentralen/RC International, Now ICL.
 * Scancodes: 59, 5a, 5b, 5c.
 */
#define RGN1 124
#define RGN2 125
#define RGN3 126
#define RGN4 127

static unsigned char high_keys[128 - SC_LIM] = {
  RGN1, RGN2, RGN3, RGN4, 0, 0, 0,                   /* 0x59-0x5f */
  0, 0, 0, 0, 0, 0, 0, 0,                            /* 0x60-0x67 */
  0, 0, 0, 0, 0, FOCUS_PF11, 0, FOCUS_PF12,          /* 0x68-0x6f */
  0, 0, 0, FOCUS_PF2, FOCUS_PF9, 0, 0, FOCUS_PF3,    /* 0x70-0x77 */
  FOCUS_PF4, FOCUS_PF5, FOCUS_PF6, FOCUS_PF7,        /* 0x78-0x7b */
  FOCUS_PF8, JAP_86, FOCUS_PF10, 0                   /* 0x7c-0x7f */
};

/* BTC */
#define E0_MACRO   112
/* LK450 */
#define E0_F13     113
#define E0_F14     114
#define E0_HELP    115
#define E0_DO      116
#define E0_F17     117
#define E0_KPMINPLUS 118
/*
 * My OmniKey generates e0 4c for  the "OMNI" key and the
 * right alt key does nada. [kkoller@nyx10.cs.du.edu]
 */
#define E0_OK	124
/*
 * New microsoft keyboard is rumoured to have
 * e0 5b (left window button), e0 5c (right window button),
 * e0 5d (menu button). [or: LBANNER, RBANNER, RMENU]
 * [or: Windows_L, Windows_R, TaskMan]
 */
#define E0_MSLW	125
#define E0_MSRW	126
#define E0_MSTM	127

static unsigned char e0_keys[128] = {
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x00-0x07 */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x08-0x0f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x10-0x17 */
  0, 0, 0, 0, E0_KPENTER, E0_RCTRL, 0, 0,	      /* 0x18-0x1f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x20-0x27 */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x28-0x2f */
  0, 0, 0, 0, 0, E0_KPSLASH, 0, E0_PRSCR,	      /* 0x30-0x37 */
  E0_RALT, 0, 0, 0, 0, E0_F13, E0_F14, E0_HELP,	      /* 0x38-0x3f */
  E0_DO, E0_F17, 0, 0, 0, 0, E0_BREAK, E0_HOME,	      /* 0x40-0x47 */
  E0_UP, E0_PGUP, 0, E0_LEFT, E0_OK, E0_RIGHT, E0_KPMINPLUS, E0_END,/* 0x48-0x4f */
  E0_DOWN, E0_PGDN, E0_INS, E0_DEL, 0, 0, 0, 0,	      /* 0x50-0x57 */
  0, 0, 0, E0_MSLW, E0_MSRW, E0_MSTM, 0, 0,	      /* 0x58-0x5f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x60-0x67 */
  0, 0, 0, 0, 0, 0, 0, E0_MACRO,		      /* 0x68-0x6f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x70-0x77 */
  0, 0, 0, 0, 0, 0, 0, 0			      /* 0x78-0x7f */
};

int setkeycode(unsigned int scancode, unsigned int keycode)
{
	if (scancode < SC_LIM || scancode > 255 || keycode > 127)
	  return -EINVAL;
	if (scancode < 128)
	  high_keys[scancode - SC_LIM] = keycode;
	else
	  e0_keys[scancode - 128] = keycode;
	return 0;
}

int getkeycode(unsigned int scancode)
{
	return
	  (scancode < SC_LIM || scancode > 255) ? -EINVAL :
	  (scancode < 128) ? high_keys[scancode - SC_LIM] :
	    e0_keys[scancode - 128];
}

//键盘中断服务程序
static void keyboard_interrupt(int irq, struct pt_regs *regs)
{
	unsigned char scancode, keycode;
	static unsigned int prev_scancode = 0;   /* remember E0, E1 */
	char up_flag;				 /* 0 or 0200 */
	char raw_mode;

	pt_regs = regs;
	send_cmd(0xAD);		/* disable keyboard */
	kb_wait();
	if ((inb_p(0x64) & kbd_read_mask) != 0x01)
		goto end_kbd_intr;
	scancode = inb(0x60);
	mark_bh(KEYBOARD_BH);
/*
	键盘并不是一种只读设备，对键盘也有输出操作。键盘在收到数据后都要回送一个0xfa
	予以确认，或者回送一个0xfe要求重发，而CPU必须将其与正常的输入区分开来。为了
	这个目的，内核设置了一个全局变量reply_expected，每当发送一个字节给键盘时
	就要把reply_expected设为1,如果在处理过程中发现reply_expected为1就要把
	输入丢弃
*/
	if (reply_expected) {
	  /* 0xfa, 0xfe only mean "acknowledge", "resend" for most keyboards */
	  /* but they are the key-up scancodes for PF6, PF10 on a FOCUS 9000 */
		reply_expected = 0;
		if (scancode == 0xfa) {
			acknowledge = 1;
			goto end_kbd_intr;
		} else if (scancode == 0xfe) {
			resend = 1;
			goto end_kbd_intr;
		}
		/* strange ... */
		reply_expected = 1;
#if 0
		printk("keyboard reply expected - got %02x\n", scancode);
#endif
	}
	if (scancode == 0) {
#ifdef KBD_REPORT_ERR
		printk("keyboard buffer overflow\n");
#endif
		prev_scancode = 0;
		goto end_kbd_intr;
	}

/*
	PC机的控制台终端由显示器（图形卡）和键盘两部分构成，所以除tty_struct
	结构外，还有一个kbd_struct数据结构。同时，物理的显示器和键盘又可用于
	多个虚拟终端，通过Alt键与功能键的组合来切换。显然，每个虚拟终端都应该
	有自己的tty_struct结构和kbd_struct结构。为此，内核中设立了ttytab[]
	和kbd_tab[]两个结构数组，而全局变量fg_console则记录着当前的“前台”虚拟
	终端号。同时，为了方便于后面的处理，又设立了tty和kbd两个全局变量，使它们
	分别指向“前台”虚拟终端的tty_struct和kbd_struct结构
*/
	tty = ttytab[fg_console];
 	kbd = kbd_table + fg_console;
	//如果前台终端的键盘工作于原始模式VC_RAW（可以通过系统调用ioctl()设置）
	//那就把键盘扫描码直接放到键盘的接收队列中，否则就要将扫描码转换成“键码”
	//后才放到队列中。所谓的原始（raw）模式对于不同的层次有不同的意义，键盘
	//的原始模式有两种，其中最原始的就是VC_RAW，表示直接将键码送给应用层。
	if ((raw_mode = (kbd->kbdmode == VC_RAW))) {
 		put_queue(scancode);
		/* we do not return yet, because we want to maintain
		   the key_down array, so that we have the correct
		   values when finishing RAW mode or when changing VT's */
 	}

	if (scancode == 0xff) {
	        /* in scancode mode 1, my ESC key generates 0xff */
		/* the calculator keys on a FOCUS 9000 generate 0xff */
#ifndef KBD_IS_FOCUS_9000
#ifdef KBD_REPORT_ERR
		if (!raw_mode)
		  printk("keyboard error\n");
#endif
#endif
		prev_scancode = 0;
		goto end_kbd_intr;
	}
/*
	如果读入的扫描码是0xe0或0xe1,那就是扩充码的前缀。此时的扫描码是个序列，
	所以需要为之实现一种“有限状态机”，全局变量prev_scancode就是用于这个目
	的。这里先把前缀码作为一种状态保存在prev_scancode中，并返回，表示这个
	字节应该丢弃。此外，0xff也应该丢弃。
	早期的PC键盘上只有83个键，后来扩充到101或104键，例如右边的Ctrl就是一个
	扩充键。当按下或松开一个扩充键时，键盘扫描码和系统扫描吗都以一个0xe0开头
*/
	if (scancode == 0xe0 || scancode == 0xe1) {
		prev_scancode = scancode;
		goto end_kbd_intr;
 	}

 	/*
	 *  Convert scancode to keycode, using prev_scancode.
 	 */
	//局部变量up_flag实际上相当于将扫描码的最高位提取了出来，即表示键是按下还是松开
	up_flag = (scancode & 0200);
 	scancode &= 0x7f;

/*
	对于扫描码本身的处理因是否是扩充键而异。如果prev_scancode非0,那就说明在此之前的
	字节是个前缀码，此时又要看前缀码是否是0xe0。
	前缀码0xe1是个特例，在按下或松开pause键的时候，键盘向主机发送一个二字节序列-----
	[0xe1,0x1d,0x45]或[0x0e1,0x1d,0xc5]。代码中为这个序列设置了个中间状态0x100
	如果三个字节都对，那就是E1_PAUSE，否则就丢弃。除pause外，其他扩充键的前缀码都是
	0xe0
*/
	if (prev_scancode) {
	  /*
	   * usually it will be 0xe0, but a Pause key generates
	   * e1 1d 45 e1 9d c5 when pressed, and nothing when released
	   */
	  if (prev_scancode != 0xe0) {
	      if (prev_scancode == 0xe1 && scancode == 0x1d) {
		  prev_scancode = 0x100;
		  goto end_kbd_intr;
	      } else if (prev_scancode == 0x100 && scancode == 0x45) {
		  keycode = E1_PAUSE;
		  prev_scancode = 0;
	      } else {
#ifdef KBD_REPORT_UNKN
		  if (!raw_mode)
		    printk("keyboard: unknown e1 escape sequence\n");
#endif
		  prev_scancode = 0;
		  goto end_kbd_intr;
	      }
	  } else {
	      prev_scancode = 0;
	      /*
	       *  The keyboard maintains its own internal caps lock and
	       *  num lock statuses. In caps lock mode E0 AA precedes make
	       *  code and E0 2A follows break code. In num lock mode,
	       *  E0 2A precedes make code and E0 AA follows break code.
	       *  We do our own book-keeping, so we will just ignore these.
	       */
	      /*
	       *  For my keyboard there is no caps lock mode, but there are
	       *  both Shift-L and Shift-R modes. The former mode generates
	       *  E0 2A / E0 AA pairs, the latter E0 B6 / E0 36 pairs.
	       *  So, we should also ignore the latter. - aeb@cwi.nl
	       */
/*
			左右两个Shift键并非扩充键，但是有些键盘在NumLock或CapLock状态下操作左右
			Shift键时就会把它们当成扩充键，由于Linux内核维持各种锁定状态，所以丢弃作为
			扩充键的左右Shift扫描码。
*/
	      if (scancode == 0x2a || scancode == 0x36)
				goto end_kbd_intr;
		      //对于扩充键，从扫描码到键码的转换由e0_keys[]数组提供，例如，右Ctrl的扫描码
		      //为[0xe0,0x1d]，所以用0x1d为下标从表中查得其键码E0_RCTRL。又如PageUp键的
		      //扫描码为[0xe0,0x49]，所以用0x49为下标从表中查得其键码E0_PAGE
	      if (e0_keys[scancode])
				keycode = e0_keys[scancode];
	      else {
#ifdef KBD_REPORT_UNKN
		  if (!raw_mode)
		    printk("keyboard: unknown scancode e0 %02x\n", scancode);
#endif
		  goto end_kbd_intr;
	      }
	  }
/*下面是不带前缀的扫描码，即非扩充键的扫描码*/
	} else if (scancode >= SC_LIM) {
	    /* This happens with the FOCUS 9000 keyboard
	       Its keys PF1..PF12 are reported to generate
	       55 73 77 78 79 7a 7b 7c 74 7e 6d 6f
	       Moreover, unless repeated, they do not generate
	       key-down events, so we have to zero up_flag below */
	    /* Also, Japanese 86/106 keyboards are reported to
	       generate 0x73 and 0x7d for \ - and \ | respectively. */
	    /* Also, some Brazilian keyboard is reported to produce
	       0x73 and 0x7e for \ ? and KP-dot, respectively. */
	  //数值大于SC_LIM的扫描码需要由high_keys[]数组提供转换(为键码)
/*
	   在high_keys[]数组范围内的多是功能键，其键码的数值在89--127范围中，
	   		那么，为什么要将扫描码转换成键码呢？
	带前缀0xe0和不带前缀的扫描码占据着两块128字节的区间，但是实际上两个区间都是很
	稀疏的，就键盘的大小来说，现在还在120键以下。如果一键一码则只需要120字节以下，	
	完全可以合并到同一个126字节的区间中去。可是，又不宜简单的将前缀丢弃了事，因为那
	样至少会造成功能键与普通字符穿插在一起而带来不便。比较好的办法当然是统一编排一下
	定义出一个标准的键码，使功能键的代码都集中在一起。这样，不管是PC键盘也好，
	Macintoshi键盘也好，一旦转换到键码都一样，与具体的键盘无关了。
*/
	  keycode = high_keys[scancode - SC_LIM];

	  if (!keycode) {
	      if (!raw_mode) {
#ifdef KBD_REPORT_UNKN
		  printk("keyboard: unrecognized scancode (%02x) - ignored\n"
			 , scancode);
#endif
	      }
	      goto end_kbd_intr;
	  }
 	} else //SC_LIM=89=0x59,数值小于SC_LIM的扫描码与键码相同而无需转换。
	  keycode = scancode;

	/*
	 * At this point the variable `keycode' contains the keycode.
	 * Note: the keycode must not be 0.
	 * We keep track of the up/down status of the key, and
	 * return the keycode if in MEDIUMRAW mode.
	 */
	//执行到这里，扫描码scancode已经被转换成键码keycode了

/*
	局部变量up_flag的值表明相应的键是处于按下状态还是放开状态。当然，键盘上的
	大多数键都处于放开状态，但是也有可能同时有几个键处于按下状态，所以代码中用
	一个全局位图key_down来记录各个键的状态。如果读入的扫描码表明相应的键处于
	按下状态，那就把位图key_down中对应的位设成1，进一步，如果这一位原来就是1
	那就说明使用者按下这个键不放，所以键盘开始了自动重复功能。反之，如果读入的
	扫描码表明相应的键处于放开状态，那就把位图中的对应位设成0,如果原来就是0,
	那就说明至少是漏了一个扫描码
*/
	if (up_flag) {
		rep = 0;
 		if(!clear_bit(keycode, key_down)) {
		    /* unexpected, but this can happen:
		       maybe this was a key release for a FOCUS 9000
		       PF key; if we want to see it, we have to clear
		       up_flag */
		    if (keycode >= SC_LIM || keycode == 85)
		      up_flag = 0;
		}
	} else
 		rep = set_bit(keycode, key_down);

	if (raw_mode)
		goto end_kbd_intr;

/*
	如果键盘的运行模式是VC_MEDIUMRAW，或者说是半生半熟的，半原始的，那么至此
	已经可以把转换后的键码放入接收队列了。这样，如果键盘运行于两种原始模式之一，
	应用进程从键盘（控制台）读到的就是扫描码或键码。这样，就可以在进程中处理
	从扫描码或键码向具体语言文字编码的转换，当然，也可以不让进程运行于原始模式，
	而在内核中完成这最后一步转换。
*/
	if (kbd->kbdmode == VC_MEDIUMRAW) {
		/* soon keycodes will require more than one byte */
 		put_queue(keycode + up_flag);
		goto end_kbd_intr;
 	}

/*下面将做对键盘输入的最后一步转换，这一次是要转换成ASCII码或其他拼音文字的代码。在正常模式运行下
的键盘要把输入转换为ASCII码后才放入接受队列，有的则根本不放入接受队列*/
 	/*
	 * Small change in philosophy: earlier we defined repetition by
	 *	 rep = keycode == prev_keycode;
	 *	 prev_keycode = keycode;
	 * but now by the fact that the depressed key was down already.
	 * Does this ever make a difference? Yes.
	 */

	/*
 	 *  Repeat a key only if the input buffers are empty or the
 	 *  characters get echoed locally. This makes key repeat usable
 	 *  with slow applications and under heavy loads.
	 */
/*
	对于正常状态下的键盘输入，即rep为0时，这一段代码的执行是无条件的。而对于
	因按下键不放而自动产生的键盘输入，则是有条件的，条件是键盘运行于VC_REPEAT
	模式，即允许自动重复，并且终端运行于“echo”模式，或者输入缓冲区已经空了（以前
	的输入都被读走了），如果不满足这些条件，键盘输入就被丢弃了。
*/
	if (!rep ||
	    (vc_kbd_mode(kbd,VC_REPEAT) && tty &&
	     (L_ECHO(tty) || (tty->driver.chars_in_buffer(tty) == 0)))) {
		u_short keysym;
		u_char type;
/*
	键码向目标码keysym的转换是通过数组实现的，但是考虑到Ctrl、Shift、
	Alt等及其按照一定规则组合，一起来影响目标代码的产生，比如，平时按下
	a，则是a，但是在Shift按下的状态下按下a就是A。所以需要不仅仅一个码表
	内核设立全局变量shift_state，以位图的形式记录当前处于按下状态的辅助
	键，用于码表数组的数组key_maps[]的索引
	同时，系统运行过程中也可以通过调用ioctl()下载、替换系统默认的码表，
	系统还有个工具/usr/bin/loadkeys，可以在运行时生成码表并且下载，达到
	动态地改变码表、从而改变目标语言的目的
*/
		/* the XOR below used to be an OR */
		int shift_final = shift_state ^ kbd->lockstate;
		//key_map指向当前状态下的码表
		ushort *key_map = key_maps[shift_final];
/*
	每一个键，有一个单独的码对应它，称之为键码(keycode)。键和键码之间
	的关系是绝对固定的。 (粗略来说，你可以说 "键码就是键")。

	连接到每一个键码 (或键)的是一个keysym的表列。一个keysym是一个代表
	印在键盘符号上的数字常数。在预设的情况，大多数的键只有一个keysym 与之
	对应，例如 SHIFT，A，B，Delete，Linefeed等等。keysym既非ASCII或EBCDIC
	字元，也非server用以维持keysym和字元的关系。你可以对每一个键有两个
	keysym。在预设对应中，有很多连接到两个keysym的键，例如冒号(:) 和分号(;)
	，7 和 &等等。对一个键附属的keysym表列中，第一个keysym是未按下修饰键
	的状况下的键。第二个keysym是指当SHIFT (或Lock)已被同时按下时的键，如
	果在表列中只有一项，且为字母，则系统自动假设第二项为相对的大写字母。
	超过两项的keysym并没有特别的意义，键盘和keysym之间的关系被称之为键盘
	对应。 
*/
		if (key_map != NULL) {
			//从码表中取目标码
			keysym = key_map[keycode];
			type = KTYP(keysym);
/*
			目标码的高8位代表键的类型，当高8位的数值大于等于0xf0时，其数值
			与0xf0之差表示键的类型，小于0xf0时则表示采用UTF-8编码
*/
			if (type >= 0xf0) {
			    type -= 0xf0;
				//KT_LETTER表示字母。字母类的输入与普通的可打印字符的区别仅在于
				//CapsLock键的作用，所以这里把KT_LETTER替换为KT_LATIN
			    if (type == KT_LETTER) {
					type = KT_LATIN; //KT_LATIN表示普通可打印字符
				//根据当前键盘上的CapsLock发光二极管是否亮着决定是否将Shift键的作用反相
				if (vc_kbd_led(kbd, VC_CAPSLOCK)) {
				    key_map = key_maps[shift_final ^ (1<<KG_SHIFT)];
				    if (key_map)
				      keysym = key_map[keycode];
				}
			    }
				//根据键的类型调用相应的处理程序。key_handle[]是个以类型为下标的函数指针数组
				//比如，对于KT_LATIN类型的处理函数是do_self()
			    (*key_handler[type])(keysym & 0xff, up_flag);
			} else {
			    /* maybe only if (kbd->kbdmode == VC_UNICODE) ? */
			    if (!up_flag)
			      to_utf8(keysym);
			}
		} else {
			/* maybe beep? */
			/* we have at least to update shift_state */
#if 1			/* how? two almost equivalent choices follow */
			compute_shiftstate();
#else
			keysym = U(plain_map[keycode]);
			type = KTYP(keysym);
			if (type == KT_SHIFT)
			  (*key_handler[type])(keysym & 0xff, up_flag);
#endif
		}
	}

end_kbd_intr:
	send_cmd(0xAE);         /* enable keyboard */
}

/*
	当用户按压键盘时, 键盘中断处理程序将经过转换的键盘功能码用
	tty_insert_flip_char()放入到当前打开终端的翻转缓冲区
	之中,然后将缓冲区输出任务函数(flush_to_ldisc)添加到时钟中断
	任务队列(tq_timer)并激活控制台软中断执行该任务
	函数. flush_to_ldisc()翻转读写缓冲区, 将缓冲区接收数据传
	递给tty终端规程的n_tty_receive_buf()接收函数, 
	n_tty_receive_buf()处理输入字符, 将输出字符缓冲在终端的
	循环缓冲区(read_buf)之中
	
	将flush_to_ldisc()函数绑定到flip所保存的下半部函数指针是
	由tty_io.c中的initialize_tty_struct()函数完成的
*/
//凡是收到的字符都通过此函数逐个地放入前台键盘接受队列
static void put_queue(int ch)
{
	wake_up(&keypress_wait);
	if (tty) {
		//对于从键盘接收到而未经过进一步加工的字符，先放在tty_struct结构内
		//的一个filp缓冲区中。下面两个函数均定义于include/linux/tty_flip.h
		tty_insert_flip_char(tty, ch, 0);
		//其实是执行键盘处理的bh函数
		tty_schedule_flip(tty);
	}
}

static void puts_queue(char *cp)
{
	wake_up(&keypress_wait);
	if (!tty)
		return;

	while (*cp) {
		tty_insert_flip_char(tty, *cp, 0);
		cp++;
	}
	tty_schedule_flip(tty);
}

static void applkey(int key, char mode)
{
	static char buf[] = { 0x1b, 'O', 0x00, 0x00 };

	buf[1] = (mode ? 'O' : '[');
	buf[2] = key;
	puts_queue(buf);
}

static void enter(void)
{
	put_queue(13);
	if (vc_kbd_mode(kbd,VC_CRLF))
		put_queue(10);
}

static void caps_toggle(void)
{
	if (rep)
		return;
	chg_vc_kbd_led(kbd, VC_CAPSLOCK);
}

static void caps_on(void)
{
	if (rep)
		return;
	set_vc_kbd_led(kbd, VC_CAPSLOCK);
}

static void show_ptregs(void)
{
	if (pt_regs)
		show_regs(pt_regs);
}

static void hold(void)
{
	if (rep || !tty)
		return;

	/*
	 * Note: SCROLLOCK will be set (cleared) by stop_tty (start_tty);
	 * these routines are also activated by ^S/^Q.
	 * (And SCROLLOCK can also be set by the ioctl KDSKBLED.)
	 */
	if (tty->stopped)
		start_tty(tty);
	else
		stop_tty(tty);
}

static void num(void)
{
	if (vc_kbd_mode(kbd,VC_APPLIC))
		applkey('P', 1);
	else
		bare_num();
}

/*
 * Bind this to Shift-NumLock if you work in application keypad mode
 * but want to be able to change the NumLock flag.
 * Bind this to NumLock if you prefer that the NumLock key always
 * changes the NumLock flag.
 */
static void bare_num(void)
{
	if (!rep)
		chg_vc_kbd_led(kbd,VC_NUMLOCK);
}

static void lastcons(void)
{
	/* switch to the last used console, ChN */
	want_console = last_console;
}

static void decr_console(void)
{
	int i;
 
	for (i = fg_console-1; i != fg_console; i--) {
		if (i == -1)
			i = MAX_NR_CONSOLES-1;
		if (vc_cons_allocated(i))
			break;
	}
	want_console = i;
}

static void incr_console(void)
{
	int i;

	for (i = fg_console+1; i != fg_console; i++) {
		if (i == MAX_NR_CONSOLES)
			i = 0;
		if (vc_cons_allocated(i))
			break;
	}
	want_console = i;
}

static void send_intr(void)
{
	if (!tty)
		return;
	tty_insert_flip_char(tty, 0, TTY_BREAK);
	tty_schedule_flip(tty);
}

static void scroll_forw(void)
{
	scrollfront(0);
}

static void scroll_back(void)
{
	scrollback(0);
}

static void boot_it(void)
{
	ctrl_alt_del();
}

static void compose(void)
{
	dead_key_next = 1;
}

int spawnpid, spawnsig;

static void spawn_console(void)
{
        if (spawnpid)
	   if(kill_proc(spawnpid, spawnsig, 1))
	     spawnpid = 0;
}

static void SAK(void)
{
	do_SAK(tty);
#if 0
	/*
	 * Need to fix SAK handling to fix up RAW/MEDIUM_RAW and
	 * vt_cons modes before we can enable RAW/MEDIUM_RAW SAK
	 * handling.
	 * 
	 * We should do this some day --- the whole point of a secure
	 * attention key is that it should be guaranteed to always
	 * work.
	 */
	reset_vc(fg_console);
	do_unblank_screen();	/* not in interrupt routine? */
#endif
}

static void do_ignore(unsigned char value, char up_flag)
{
}

static void do_null()
{
	compute_shiftstate();
}

static void do_spec(unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	if (value >= SIZE(spec_fn_table))
		return;
	spec_fn_table[value]();
}

static void do_lowercase(unsigned char value, char up_flag)
{
	printk("keyboard.c: do_lowercase was called - impossible\n");
}

//对于VT_LATIN类型的字符的处理
static void do_self(unsigned char value, char up_flag)
{
	//up_flag非0表示当前的事件是放开而不是按下一个键，
	//所以什么事也不干，把输入丢弃就是了	
	if (up_flag)
		return;		/* no action, if this is a key release */

	if (diacr)
		value = handle_diacr(value);

	if (dead_key_next) {
		dead_key_next = 0;
		diacr = value;
		return;
	}
	
	//如果diacr和dead_key_next都是0，那就是一个正常的输入字符了
	//通过put_queue()把这个字符放在接受队列中
	put_queue(value);
}

#define A_GRAVE  '`'
#define A_ACUTE  '\''
#define A_CFLEX  '^'
#define A_TILDE  '~'
#define A_DIAER  '"'
static unsigned char ret_diacr[] =
	{A_GRAVE, A_ACUTE, A_CFLEX, A_TILDE, A_DIAER };

/* If a dead key pressed twice, output a character corresponding to it,	*/
/* otherwise just remember the dead key.				*/

static void do_dead(unsigned char value, char up_flag)
{
	if (up_flag)
		return;

	value = ret_diacr[value];
	if (diacr == value) {   /* pressed twice */
		diacr = 0;
		put_queue(value);
		return;
	}
	diacr = value;
}


/* If space is pressed, return the character corresponding the pending	*/
/* dead key, otherwise try to combine the two.				*/

unsigned char handle_diacr(unsigned char ch)
{
	int d = diacr;
	int i;

	diacr = 0;
	if (ch == ' ')
		return d;

	for (i = 0; i < accent_table_size; i++) {
		if (accent_table[i].diacr == d && accent_table[i].base == ch)
			return accent_table[i].result;
	}

	put_queue(d);
	return ch;
}

static void do_cons(unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	want_console = value;
}

static void do_fn(unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	if (value < SIZE(func_table)) {
		if (func_table[value])
			puts_queue(func_table[value]);
	} else
		printk("do_fn called with value=%d\n", value);
}

static void do_pad(unsigned char value, char up_flag)
{
	static char *pad_chars = "0123456789+-*/\015,.?";
	static char *app_map = "pqrstuvwxylSRQMnn?";

	if (up_flag)
		return;		/* no action, if this is a key release */

	/* kludge... shift forces cursor/number keys */
	if (vc_kbd_mode(kbd,VC_APPLIC) && !k_down[KG_SHIFT]) {
		applkey(app_map[value], 1);
		return;
	}

	if (!vc_kbd_led(kbd,VC_NUMLOCK))
		switch (value) {
			case KVAL(K_PCOMMA):
			case KVAL(K_PDOT):
				do_fn(KVAL(K_REMOVE), 0);
				return;
			case KVAL(K_P0):
				do_fn(KVAL(K_INSERT), 0);
				return;
			case KVAL(K_P1):
				do_fn(KVAL(K_SELECT), 0);
				return;
			case KVAL(K_P2):
				do_cur(KVAL(K_DOWN), 0);
				return;
			case KVAL(K_P3):
				do_fn(KVAL(K_PGDN), 0);
				return;
			case KVAL(K_P4):
				do_cur(KVAL(K_LEFT), 0);
				return;
			case KVAL(K_P6):
				do_cur(KVAL(K_RIGHT), 0);
				return;
			case KVAL(K_P7):
				do_fn(KVAL(K_FIND), 0);
				return;
			case KVAL(K_P8):
				do_cur(KVAL(K_UP), 0);
				return;
			case KVAL(K_P9):
				do_fn(KVAL(K_PGUP), 0);
				return;
			case KVAL(K_P5):
				applkey('G', vc_kbd_mode(kbd, VC_APPLIC));
				return;
		}

	put_queue(pad_chars[value]);
	if (value == KVAL(K_PENTER) && vc_kbd_mode(kbd, VC_CRLF))
		put_queue(10);
}

static void do_cur(unsigned char value, char up_flag)
{
	static char *cur_chars = "BDCA";
	if (up_flag)
		return;

	applkey(cur_chars[value], vc_kbd_mode(kbd,VC_CKMODE));
}

static void do_shift(unsigned char value, char up_flag)
{
	int old_state = shift_state;

	if (rep)
		return;

	/* Mimic typewriter:
	   a CapsShift key acts like Shift but undoes CapsLock */
	if (value == KVAL(K_CAPSSHIFT)) {
		value = KVAL(K_SHIFT);
		if (!up_flag)
			clr_vc_kbd_led(kbd, VC_CAPSLOCK);
	}

	if (up_flag) {
		/* handle the case that two shift or control
		   keys are depressed simultaneously */
		if (k_down[value])
			k_down[value]--;
	} else
		k_down[value]++;

	if (k_down[value])
		shift_state |= (1 << value);
	else
		shift_state &= ~ (1 << value);

	/* kludge */
	if (up_flag && shift_state != old_state && npadch != -1) {
		if (kbd->kbdmode == VC_UNICODE)
		  to_utf8(npadch & 0xffff);
		else
		  put_queue(npadch & 0xff);
		npadch = -1;
	}
}

/* called after returning from RAW mode or when changing consoles -
   recompute k_down[] and shift_state from key_down[] */
/* maybe called when keymap is undefined, so that shiftkey release is seen */
//依次扫描位图key_down[]中的每一位，对当前处于按下的每一个键都从plain_map[]中
//读出其代码，看看是什么类型，如果是锁定键或辅助键，则将shift_state中相应的位设1
void compute_shiftstate(void)
{
	int i, j, k, sym, val;

	shift_state = 0;
	for(i=0; i < SIZE(k_down); i++)
	  k_down[i] = 0;

	for(i=0; i < SIZE(key_down); i++)
	  if(key_down[i]) {	/* skip this word if not a single bit on */
	    k = i*BITS_PER_LONG;
	    for(j=0; j<BITS_PER_LONG; j++,k++)
	      if(test_bit(k, key_down)) {
		sym = U(plain_map[k]);
		if(KTYP(sym) == KT_SHIFT) {
		  val = KVAL(sym);
		  if (val == KVAL(K_CAPSSHIFT))
		    val = KVAL(K_SHIFT);
		  k_down[val]++;
		  shift_state |= (1<<val);
		}
	      }
	  }
}

static void do_meta(unsigned char value, char up_flag)
{
	if (up_flag)
		return;

	if (vc_kbd_mode(kbd, VC_META)) {
		put_queue('\033');
		put_queue(value);
	} else
		put_queue(value | 0x80);
}

static void do_ascii(unsigned char value, char up_flag)
{
	int base;

	if (up_flag)
		return;

	if (value < 10)    /* decimal input of code, while Alt depressed */
	    base = 10;
	else {       /* hexadecimal input of code, while AltGr depressed */
	    value -= 10;
	    base = 16;
	}

	if (npadch == -1)
	  npadch = value;
	else
	  npadch = npadch * base + value;
}

static void do_lock(unsigned char value, char up_flag)
{
	if (up_flag || rep)
		return;
	chg_vc_kbd_lock(kbd, value);
}

/*
 * send_data sends a character to the keyboard and waits
 * for a acknowledge, possibly retrying if asked to. Returns
 * the success status.
 */
static int send_data(unsigned char data)
{
	int retries = 3;
	int i;

	do {
		kb_wait();
		acknowledge = 0;
		resend = 0;
		reply_expected = 1;
		outb_p(data, 0x60);
		for(i=0; i<0x200000; i++) {
			inb_p(0x64);		/* just as a delay */
			if (acknowledge)
				return 1;
			if (resend)
				break;
		}
		if (!resend)
			return 0;
	} while (retries-- > 0);
	return 0;
}

/*
 * The leds display either (i) the status of NumLock, CapsLock, ScrollLock,
 * or (ii) whatever pattern of lights people want to show using KDSETLED,
 * or (iii) specified bits of specified words in kernel memory.
 */

static unsigned char ledstate = 0xff; /* undefined */
static unsigned char ledioctl;

unsigned char getledstate(void) {
    return ledstate;
}

void setledstate(struct kbd_struct *kbd, unsigned int led) {
    if (!(led & ~7)) {
	ledioctl = led;
	kbd->ledmode = LED_SHOW_IOCTL;
    } else
	kbd->ledmode = LED_SHOW_FLAGS;
    set_leds();
}

static struct ledptr {
    unsigned int *addr;
    unsigned int mask;
    unsigned char valid:1;
} ledptrs[3];

void register_leds(int console, unsigned int led,
		   unsigned int *addr, unsigned int mask) {
    struct kbd_struct *kbd = kbd_table + console;
    if (led < 3) {
	ledptrs[led].addr = addr;
	ledptrs[led].mask = mask;
	ledptrs[led].valid = 1;
	kbd->ledmode = LED_SHOW_MEM;
    } else
	kbd->ledmode = LED_SHOW_FLAGS;
}

static inline unsigned char getleds(void){
    struct kbd_struct *kbd = kbd_table + fg_console;
    unsigned char leds;

    if (kbd->ledmode == LED_SHOW_IOCTL)
      return ledioctl;
    leds = kbd->ledflagstate;
    if (kbd->ledmode == LED_SHOW_MEM) {
	if (ledptrs[0].valid) {
	    if (*ledptrs[0].addr & ledptrs[0].mask)
	      leds |= 1;
	    else
	      leds &= ~1;
	}
	if (ledptrs[1].valid) {
	    if (*ledptrs[1].addr & ledptrs[1].mask)
	      leds |= 2;
	    else
	      leds &= ~2;
	}
	if (ledptrs[2].valid) {
	    if (*ledptrs[2].addr & ledptrs[2].mask)
	      leds |= 4;
	    else
	      leds &= ~4;
	}
    }
    return leds;
}

/*
 * This routine is the bottom half of the keyboard interrupt
 * routine, and runs with all interrupts enabled. It does
 * console changing, led setting and copy_to_cooked, which can
 * take a reasonably long time.
 *
 * Aside from timing (which isn't really that important for
 * keyboard interrupts as they happen often), using the software
 * interrupt routines for this thing allows us to easily mask
 * this when we don't want any of the above to happen. Not yet
 * used, but this allows for easy and efficient race-condition
 * prevention later on.
 */
static void kbd_bh(void * unused)
{
	unsigned char leds = getleds();

	if (leds != ledstate) {
		ledstate = leds;
		if (!send_data(0xed) || !send_data(leds))
			send_data(0xf4);	/* re-enable kbd if any errors */
	}
	if (want_console >= 0) {
		if (want_console != fg_console) {
			change_console(want_console);
			/* we only changed when the console had already
			   been allocated - a new console is not created
			   in an interrupt routine */
		}
		want_console = -1;
	}
	poke_blanked_console();
	cli();
	if ((inb_p(0x64) & kbd_read_mask) == 0x01)
		fake_keyboard_interrupt();
	sti();
}

//该函数是键盘代码执行的开始点
unsigned long kbd_init(unsigned long kmem_start)
{
	int i;
	struct kbd_struct kbd0;
	/* console_driver这个全局变量是很重要的，他维护着庞大的TTY/Console对象，承当TTY对外的输入和输出*/
	extern struct tty_driver console_driver;

	kbd0.ledflagstate = kbd0.default_ledflagstate = KBD_DEFLEDS; /* 缺省不亮灯、缺省，表示用key_map的第一个表，即没有lock键*/
	kbd0.ledmode = LED_SHOW_FLAGS; /* 缺省，用于显示flags */
	kbd0.lockstate = KBD_DEFLOCK;
	kbd0.modeflags = KBD_DEFMODE;  /* modeflags=0 */
	kbd0.kbdmode = VC_XLATE; /*ASCII模式*/
 
	for (i = 0 ; i < MAX_NR_CONSOLES ; i++)
		kbd_table[i] = kbd0;
	//ttytab是一个很重要的指针，他维护着当前各个控制台的tty_struct表（即相当于一个维表），
	//tty_struct可看成/dev/tty*的输入设备，只有在/dev/tty*打开时它才接收输入数据
	ttytab = console_driver.table;

	bh_base[KEYBOARD_BH].routine = kbd_bh;
	//请求键盘中断号并注册键盘中断驱动程序
	request_irq(KEYBOARD_IRQ, keyboard_interrupt, 0, "keyboard");
/*
	分配给键盘控制器的端口号范围是0x60-0x6f，但实际上IBM CP/AT使用的只有0x60
	和0x64两个端口地址（0x61,0x62和0x63用于与XT兼容的目的）
*/
	request_region(0x60,1,"kbd");
	request_region(0x64,1,"kbd");
#ifdef __alpha__
/*
	某些命令需要参数，我们在发送命令之后，发送它的参数，参数被放入输入缓冲器，同时引起状态寄存器的Bit1 置1，
	表示输入缓冲器满。这里我们要注意，向i8048发命令，是通过写60h端口，发命令的参数，也是写60h端口。i8042
	如何判断输入缓冲器中的内容是命令还是参数呢。i8042是这样判断的，如果当前状态寄存器的Bit3 为1，表示之前
	已经写入了一个命令，那么现在通过写60h端口放入输入缓冲器中的内容，就被当做之前命令的参数，并且引起状态寄
	存器的 Bit3 置0。如果当前状态寄存器的 Bit3 为0，表示之前没有写入命令，那么现在通过写60h端口放入输入缓
	冲器中的内容，就被当做一个间接发往i8048的命令，并且引起状态寄存器的 Bit3 置1。

	向i8048发参数的方法，首先，读取状态寄存器，判断bit1,状态寄存器bit1为0，说明输入缓冲器为空，可以写入。--即调用kb_wait()
	保证状态寄存器bit1为0，然后对60h端口进行写操作，写入参数。
*/
	/* enable keyboard interrupts, PC/AT mode */
	kb_wait();
	//60h：准备写入8042芯片的Command Byte；下一个通过60h写入的字节将会被放入Command Byte
	outb(0x60,0x64);	/* write PS/2 Mode Register */
	kb_wait();
	//命令字节0x41=0100 0001 即将第二套扫描码翻译为第一套、打开键盘中断
	outb(0x41,0x60);	/* KCC | EKI */
	kb_wait();
	if (!send_data(0xf0) || !send_data(0x02))
		printk("Scanmode 2 change failed\n");
#endif
	mark_bh(KEYBOARD_BH);
	enable_bh(KEYBOARD_BH);
	return kmem_start;
}
