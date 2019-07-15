/*
 * n_tty.c --- implements the N_TTY line discipline.
 * 
 * This code used to be in tty_io.c, but things are getting hairy
 * enough that it made sense to split things off.  (The N_TTY
 * processing has changed so much that it's hardly recognizable,
 * anyway...)
 *
 * Note that the open routine for N_TTY is guaranteed never to return
 * an error.  This is because Linux will fall back to setting a line
 * to N_TTY if it can not switch to any other line discipline.  
 *
 * Written by Theodore Ts'o, Copyright 1994.
 * 
 * This file also contains code originally written by Linus Torvalds,
 * Copyright 1991, 1992, 1993, and by Julian Cowley, Copyright 1994.
 * 
 * This file may be redistributed under the terms of the GNU Public
 * License.
 */

/*本文件即是TTY驱动的第二层：线路规程*/
//从tty_io.c中的tty_read/tty_write函数可以看出，
//他们最后调用到了线路规程的read/write函数

//此文件中含有在linux中潜藏了五年的漏洞
//http://blog.csdn.net/hu3167343/article/details/39162431

/*
Line editing. Most users make mistakes while typing, so a backspace key is often useful.
This could of course be implemented by the applications themselves, but in accordance with 
the UNIX design philosophy, applications should be kept as simple as possible. So as a 
convenience, the operating system provides an editing buffer and some rudimentary editing 
commands (backspace, erase word, clear line, reprint), which are enabled by default inside 
the line discipline. Advanced applications may disable these features by putting the line 
discipline in raw mode instead of the default cooked (or canonical) mode. Most interactive 
applications (editors, mail user agents, shells, all programs relying on curses or readline) 
run in raw mode, and handle all the line editing commands themselves. The line discipline 
also contains options for character echoing and automatic conversion between carriage returns 
and linefeeds. Think of it as a primitive kernel-level sed(1), if you like.

Incidentally, the kernel provides several different line disciplines. Only one of them is 
attached to a given serial device at a time. The default discipline, which provides line 
editing, is called N_TTY (drivers/char/n_tty.c, if you're feeling adventurous). Other disciplines 
are used for other purposes, such as managing packet switched data (ppp, IrDA, serial mice), but 
that is outside the scope of this article.
*/

#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/kd.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/malloc.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>

#define CONSOLE_DEV MKDEV(TTY_MAJOR,0)

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

/* number of characters left in xmit buffer before select has we have room */
#define WAKEUP_CHARS 256

/*
 * This defines the low- and high-watermarks for throttling and
 * unthrottling the TTY driver.  These watermarks are used for
 * controlling the space in the read buffer.
 */
#define TTY_THRESHOLD_THROTTLE		(N_TTY_BUF_SIZE - 128)
#define TTY_THRESHOLD_UNTHROTTLE 	128

static inline void put_tty_queue(unsigned char c, struct tty_struct *tty)
{
/*
	终端的read_buf[]缓冲区是个环形缓冲区，tty->read_head总是指向下一个可以写入
	的位置，如果read_buf[]中已经写满，则丢弃新来的输入。
*/
	if (tty->read_cnt < N_TTY_BUF_SIZE) {
		tty->read_buf[tty->read_head] = c;
		tty->read_head = (tty->read_head + 1) & (N_TTY_BUF_SIZE-1);
		tty->read_cnt++;
	}
}

/*
 * Flush the input buffer
 */
/**
*    n_tty_flush_buffer    -    clean input queue
*    @tty:    terminal device
*
*    Flush the input buffer. Called when the line discipline is
*    being closed, when the tty layer wants the buffer flushed (eg
*    at hangup) or when the N_TTY line discipline internally has to
*    clean the pending queue (for example some signals).
*/
void n_tty_flush_buffer(struct tty_struct * tty)
{
	tty->read_head = tty->read_tail = tty->read_cnt = 0;
	tty->canon_head = tty->canon_data = tty->erasing = 0;
	memset(&tty->read_flags, 0, sizeof tty->read_flags);
	
	if (!tty->link)
		return;

	if (tty->driver.unthrottle)
		(tty->driver.unthrottle)(tty);
	if (tty->link->packet) {
		tty->ctrl_status |= TIOCPKT_FLUSHREAD;
		wake_up_interruptible(&tty->link->read_wait);
	}
}

/*
 * Return number of characters buffered to be delivered to user
 */
int n_tty_chars_in_buffer(struct tty_struct *tty)
{
	return tty->read_cnt;
}

/*
 * Perform OPOST processing.  Returns -1 when the output device is
 * full and the character must be retried.
 */
static int opost(unsigned char c, struct tty_struct *tty)
{
	int	space, spaces;
	
	//首先检查终端的写缓冲区中是否还有空间
	space = tty->driver.write_room(tty);
	if (!space)
		return -1;
	//这里的处理主要是针对“\r”,"\n","\t"以及"\b"进行的
	if (O_OPOST(tty)) {
		switch (c) {
		//对于换行键"\n"的主要考虑是自动在前面插入一个回车字符“\r”
		case '\n':
			if (O_ONLRET(tty))
				tty->column = 0;
			if (O_ONLCR(tty)) {
				if (space < 2)
					return -1;
				tty->driver.put_char(tty, '\r');
				tty->column = 0;
			}
			tty->canon_column = tty->column;
			break;
		case '\r':
			if (O_ONOCR(tty) && tty->column == 0)
				return 0;
			if (O_OCRNL(tty)) {
				c = '\n';
				if (O_ONLRET(tty))
					tty->canon_column = tty->column = 0;
				break;
			}
			tty->canon_column = tty->column = 0;
			break;
			//对于换行键"\t"的处理则是要把它展开成若干个空格
		case '\t':
			spaces = 8 - (tty->column & 7);
			if (O_TABDLY(tty) == XTABS) {
				if (space < spaces)
					return -1;
				tty->column += spaces;
				tty->driver.write(tty, 0, "        ", spaces);
				return 0;
			}
			tty->column += spaces;
			break;
		case '\b':
			if (tty->column > 0)
				tty->column--;
			break;
		default:
			if (O_OLCUC(tty))
				c = toupper(c);
			if (!iscntrl(c))
				tty->column++;
			break;
		}
	}
	tty->driver.put_char(tty, c);
	return 0;
}

static inline void put_char(unsigned char c, struct tty_struct *tty)
{
	tty->driver.put_char(tty, c);
}

/* Must be called only when L_ECHO(tty) is true. */

static void echo_char(unsigned char c, struct tty_struct *tty)
{
/*
	对于同时按下了Ctrl键的字符，回打时要在该字符前加上一个“^”字符
	对于正常输入的字符情况更为复杂，所以通过opost()完成回打。
*/
	if (L_ECHOCTL(tty) && iscntrl(c) && c != '\t') {
		put_char('^', tty);
		put_char(c ^ 0100, tty);
		tty->column += 2;
	} else
		opost(c, tty);
}

static inline void finish_erasing(struct tty_struct *tty)
{
	if (tty->erasing) {
		put_char('/', tty);
		tty->column += 2;
		tty->erasing = 0;
	}
}

static void eraser(unsigned char c, struct tty_struct *tty)
{
	enum { ERASE, WERASE, KILL } kill_type;
	int head, seen_alnums;

	if (tty->read_head == tty->canon_head) {
		/* opost('\a', tty); */		/* what do you think? */
		return;
	}
	if (c == ERASE_CHAR(tty))
		kill_type = ERASE;
	else if (c == WERASE_CHAR(tty))
		kill_type = WERASE;
	else {
		if (!L_ECHO(tty)) {
			tty->read_cnt -= ((tty->read_head - tty->canon_head) &
					  (N_TTY_BUF_SIZE - 1));
			tty->read_head = tty->canon_head;
			return;
		}
		if (!L_ECHOK(tty) || !L_ECHOKE(tty)) {
			tty->read_cnt -= ((tty->read_head - tty->canon_head) &
					  (N_TTY_BUF_SIZE - 1));
			tty->read_head = tty->canon_head;
			finish_erasing(tty);
			echo_char(KILL_CHAR(tty), tty);
			/* Add a newline if ECHOK is on and ECHOKE is off. */
			if (L_ECHOK(tty))
				opost('\n', tty);
			return;
		}
		kill_type = KILL;
	}

	seen_alnums = 0;
	while (tty->read_head != tty->canon_head) {
		head = (tty->read_head - 1) & (N_TTY_BUF_SIZE-1);
		c = tty->read_buf[head];
		if (kill_type == WERASE) {
			/* Equivalent to BSD's ALTWERASE. */
			if (isalnum(c) || c == '_')
				seen_alnums++;
			else if (seen_alnums)
				break;
		}
		tty->read_head = head;
		tty->read_cnt--;
		if (L_ECHO(tty)) {
			if (L_ECHOPRT(tty)) {
				if (!tty->erasing) {
					put_char('\\', tty);
					tty->column++;
					tty->erasing = 1;
				}
				echo_char(c, tty);
			} else if (!L_ECHOE(tty)) {
				echo_char(ERASE_CHAR(tty), tty);
			} else if (c == '\t') {
				unsigned int col = tty->canon_column;
				unsigned long tail = tty->canon_head;

				/* Find the column of the last char. */
				while (tail != tty->read_head) {
					c = tty->read_buf[tail];
					if (c == '\t')
						col = (col | 7) + 1;
					else if (iscntrl(c)) {
						if (L_ECHOCTL(tty))
							col += 2;
					} else
						col++;
					tail = (tail+1) & (N_TTY_BUF_SIZE-1);
				}

				/* Now backup to that column. */
				while (tty->column > col) {
					/* Can't use opost here. */
					put_char('\b', tty);
					tty->column--;
				}
			} else {
				if (iscntrl(c) && L_ECHOCTL(tty)) {
					put_char('\b', tty);
					put_char(' ', tty);
					put_char('\b', tty);
					tty->column--;
				}
				if (!iscntrl(c) || L_ECHOCTL(tty)) {
					put_char('\b', tty);
					put_char(' ', tty);
					put_char('\b', tty);
					tty->column--;
				}
			}
		}
		if (kill_type == ERASE)
			break;
	}
	if (tty->read_head == tty->canon_head)
		finish_erasing(tty);
}

static void isig(int sig, struct tty_struct *tty)
{
	if (tty->pgrp > 0)
		kill_pg(tty->pgrp, sig, 1); //向tty所关联的组发送信号
	if (!L_NOFLSH(tty)) {
		n_tty_flush_buffer(tty);
		if (tty->driver.flush_buffer)
			tty->driver.flush_buffer(tty);
	}
}

static inline void n_tty_receive_break(struct tty_struct *tty)
{
	if (I_IGNBRK(tty))
		return;
	if (I_BRKINT(tty)) {
		isig(SIGINT, tty);
		return;
	}
	if (I_PARMRK(tty)) {
		put_tty_queue('\377', tty);
		put_tty_queue('\0', tty);
	}
	put_tty_queue('\0', tty);
	wake_up_interruptible(&tty->read_wait);
}

static inline void n_tty_receive_overrun(struct tty_struct *tty)
{
	char buf[64];

	tty->num_overrun++;
	if (tty->overrun_time < (jiffies - HZ)) {
		printk("%s: %d input overrun(s)\n", _tty_name(tty, buf),
		       tty->num_overrun);
		tty->overrun_time = jiffies;
		tty->num_overrun = 0;
	}
}

static inline void n_tty_receive_parity_error(struct tty_struct *tty,
					      unsigned char c)
{
	if (I_IGNPAR(tty)) {
		return;
	}
	if (I_PARMRK(tty)) {
		put_tty_queue('\377', tty);
		put_tty_queue('\0', tty);
		put_tty_queue(c, tty);
	} else
		put_tty_queue('\0', tty);
	wake_up_interruptible(&tty->read_wait);
}

static inline void n_tty_receive_char(struct tty_struct *tty, unsigned char c)
{
	//如果终端运行于原始模式，那就简单的将从flip缓冲区读出的字符写入终端的read_buf[]缓冲区
	if (tty->raw) {
		put_tty_queue(c, tty);
		return;
	}

/*
	对于非原始模式，或者称加工模式，则要进行一系列的处理，这些处理包括一般的和特殊的
	两部分，前者包括终端的自动暂停（进入省电模式）和启动、强制转换成小写字符、往显示
	屏上回打等。后者则包括许多因具体字符而异的处理，其中也包括基于XON/XOFF字符（一
	般是Ctrl-Q和Ctrl-S）的流量控制
*/
	if (tty->stopped && I_IXON(tty) && I_IXANY(tty)) {
		start_tty(tty);
		return;
	}
	
	if (I_ISTRIP(tty))
		c &= 0x7f;
	if (I_IUCLC(tty) && L_IEXTEN(tty))
		c=tolower(c);

	if (tty->closing) {
		if (I_IXON(tty)) {
			if (c == START_CHAR(tty))
				start_tty(tty);
			else if (c == STOP_CHAR(tty))
				stop_tty(tty);
		}
		return;
	}

	/*
	 * If the previous character was LNEXT, or we know that this
	 * character is not one of the characters that we'll have to
	 * handle specially, do shortcut processing to speed things
	 * up.
	 */
/*
	tty_struct结构中的process_char_map位图指明了需要特殊处理的字符，只要一个
	字符不属于这个位图所代表的集合，对其进一步处理就只限于回打，然后就通过tty_put_queue()
	把该字符写入read_buf[]缓冲区，如果此前的字符是个“erase”字符，如键盘上的backspace键，
	则终端处于正在退字符的状态，现在新的字符既然不属于process_char_map，那就不再是“erase”
	字符，所以先调用finish_erasing()结束终端的退字符状态，然后就是对回打的处理了。
*/
	if (!test_bit(c, &tty->process_char_map) || tty->lnext) {
		finish_erasing(tty);
		tty->lnext = 0;
/*
		如果终端的设置模式表明需要回打，就调用echo_char()完成这个操作，但是先要检查
		一下read_buf[]缓冲区中是否还有空位可以接受当前的字符，如果已经满了就要向显示
		器接口写一个'\a'，让它“嘟”的响一下，以免使用者误以为打入的字符已经被接受了。
*/
		if (L_ECHO(tty)) {
			if (tty->read_cnt >= N_TTY_BUF_SIZE-1) {
				//put_char()==tty->driver.put_char()
				put_char('\a', tty); /* beep if no space */
				return;
			}
			/* Record the column of first canon char. */
			if (tty->canon_head == tty->read_head)
				tty->canon_column = tty->column;
			echo_char(c, tty);
		}
		if (I_PARMRK(tty) && c == (unsigned char) '\377')
			put_tty_queue(c, tty);
		put_tty_queue(c, tty);
		return;
	}

/*
	这里主要是对\r、\n以及XON/XOFF	等字符的处理。这就是说，不管这些
	字符是否属于process_char_map，对这些字符总是要进行一些处理，只是
	方式略有不同。还有就是几个可以导致向终端控制进程发送的信号的字符，
	如Ctrl-Z，Ctrl-C这些字符。
*/
	if (c == '\r') {
		if (I_IGNCR(tty))
			return;
		if (I_ICRNL(tty))
			c = '\n';
	} else if (c == '\n' && I_INLCR(tty))
		c = '\r';
	if (I_IXON(tty)) {
		if (c == START_CHAR(tty)) {
			start_tty(tty);
			return;
		}
		if (c == STOP_CHAR(tty)) {
			stop_tty(tty);
			return;
		}
	}
	//如果接受到CTRL-C等控制字符就向在该终端上运行的进程发送信号
	if (L_ISIG(tty)) {
		if (c == INTR_CHAR(tty)) {
			isig(SIGINT, tty);
			return;
		}
		if (c == QUIT_CHAR(tty)) {
			isig(SIGQUIT, tty);
			return;
		}
		if (c == SUSP_CHAR(tty)) {
			if (!is_orphaned_pgrp(tty->pgrp))
				isig(SIGTSTP, tty);
			return;
		}
	}
//下面就是对终端设备输入的规范模式，对输入的加工或者烹调
/*
	在规范模式下，尽管终端设备的read_buf[]缓冲区中已经有了数据，
	却并不马上就唤醒可能正在睡眠中等待着要从该终端读出的进程，而要
	到接受到\n字符（有可能从\r转换而来）以后，或者接受到EOF、EOL
	等字符时才唤醒。相比之下，在非规范模式下，包括原始模式下，则只要
	缓冲区中的字节数量达到预先设定的minimum_to_wake，就会唤醒正在
	睡眠等待进程，而minimum_to_wake一般就是1。具体地，这发生于从
	n_tty_receive_char()返回到n_tty_receive_buf()以后
*/
	if (L_ICANON(tty)) {
		if (c == ERASE_CHAR(tty) || c == KILL_CHAR(tty) ||
		    (c == WERASE_CHAR(tty) && L_IEXTEN(tty))) {
			eraser(c, tty);
			return;
		}
		if (c == LNEXT_CHAR(tty) && L_IEXTEN(tty)) {
			tty->lnext = 1;
			if (L_ECHO(tty)) {
				finish_erasing(tty);
				if (L_ECHOCTL(tty)) {
					put_char('^', tty);
					put_char('\b', tty);
				}
			}
			return;
		}
		if (c == REPRINT_CHAR(tty) && L_ECHO(tty) &&
		    L_IEXTEN(tty)) {
			unsigned long tail = tty->canon_head;

			finish_erasing(tty);
			echo_char(c, tty);
			opost('\n', tty);
			while (tail != tty->read_head) {
				echo_char(tty->read_buf[tail], tty);
				tail = (tail+1) & (N_TTY_BUF_SIZE-1);
			}
			return;
		}
		if (c == '\n') {
			if (L_ECHO(tty) || L_ECHONL(tty)) {
				if (tty->read_cnt >= N_TTY_BUF_SIZE-1) {
					put_char('\a', tty);
					return;
				}
				opost('\n', tty);
			}
			goto handle_newline;
		}
		if (c == EOF_CHAR(tty)) {
		        if (tty->canon_head != tty->read_head)
			        set_bit(TTY_PUSH, &tty->flags);
			c = __DISABLED_CHAR;
			goto handle_newline;
		}
		if ((c == EOL_CHAR(tty)) ||
		    (c == EOL2_CHAR(tty) && L_IEXTEN(tty))) {
			/*
			 * XXX are EOL_CHAR and EOL2_CHAR echoed?!?
			 */
			if (L_ECHO(tty)) {
				if (tty->read_cnt >= N_TTY_BUF_SIZE-1) {
					put_char('\a', tty);
					return;
				}
				/* Record the column of first canon char. */
				if (tty->canon_head == tty->read_head)
					tty->canon_column = tty->column;
				echo_char(c, tty);
			}
			/*
			 * XXX does PARMRK doubling happen for
			 * EOL_CHAR and EOL2_CHAR?
			 */
			if (I_PARMRK(tty) && c == (unsigned char) '\377')
				put_tty_queue(c, tty);

		handle_newline:
			set_bit(tty->read_head, &tty->read_flags);
			put_tty_queue(c, tty);
			tty->canon_head = tty->read_head;
			tty->canon_data++;
			if (tty->fasync)
				kill_fasync(tty->fasync, SIGIO);
			if (tty->read_wait)
				wake_up_interruptible(&tty->read_wait);
			return;
		}
	}
	
	finish_erasing(tty);
	if (L_ECHO(tty)) {
		if (tty->read_cnt >= N_TTY_BUF_SIZE-1) {
			put_char('\a', tty); /* beep if no space */
			return;
		}
		if (c == '\n')
			opost('\n', tty);
		else {
			/* Record the column of first canon char. */
			if (tty->canon_head == tty->read_head)
				tty->canon_column = tty->column;
			echo_char(c, tty);
		}
	}

	if (I_PARMRK(tty) && c == (unsigned char) '\377')
		put_tty_queue(c, tty);

	put_tty_queue(c, tty);
}	

static void n_tty_receive_buf(struct tty_struct *tty, unsigned char *cp,
			      char *fp, int count)
{
	unsigned char *p;
	char *f, flags = 0;
	int	i;

	if (!tty->read_buf)
		return;

/*
	终端的tty_struct结构中有raw和real_raw两个字段，都表示终端层次上的
	原始模式，但是real_raw更为原始，表示即使在链路上出现了break状态，或
	者接受到的数据有奇偶校检错误，也都原封不动的上交。
	所以，对于运行于real_raw模式的终端来说，只要把flip缓冲区中的内容复制到
	终端的read_buf[]缓冲区中就可以了。
	否则，就要通过for循环逐个字节的边搬运边处理加工，这就是比较耗时的操作所在
*/
	if (tty->real_raw) {
		i = MIN(count, MIN(N_TTY_BUF_SIZE - tty->read_cnt,
				   N_TTY_BUF_SIZE - tty->read_head));
		memcpy(tty->read_buf + tty->read_head, cp, i);
		tty->read_head = (tty->read_head + i) & (N_TTY_BUF_SIZE-1);
		tty->read_cnt += i;
		cp += i;
		count -= i;

		i = MIN(count, MIN(N_TTY_BUF_SIZE - tty->read_cnt,
			       N_TTY_BUF_SIZE - tty->read_head));
		memcpy(tty->read_buf + tty->read_head, cp, i);
		tty->read_head = (tty->read_head + i) & (N_TTY_BUF_SIZE-1);
		tty->read_cnt += i;
	} else {
		for (i=count, p = cp, f = fp; i; i--, p++) {
			if (f)
				flags = *f++;
			switch (flags) {
/*
			flip缓冲区中的代码字节都是与一个flag字节配对存在的，这个flag字节指明了
			代码字节的性质和类型，不过由tty_insert_char()写入flip缓冲区时总是把
			相应的flag字节设置成0,就是TTY_NORMAL，所以这时调用的是n_tty_receive_char()
*/
			case TTY_NORMAL:
				n_tty_receive_char(tty, *p);
				break;
			case TTY_BREAK:
				n_tty_receive_break(tty);
				break;
			case TTY_PARITY:
			case TTY_FRAME:
				n_tty_receive_parity_error(tty, *p);
				break;
			case TTY_OVERRUN:
				n_tty_receive_overrun(tty);
				break;
			default:
				printk("%s: unknown flag %d\n", tty_name(tty),
				       flags);
				break;
			}
		}
		if (tty->driver.flush_chars)
			tty->driver.flush_chars(tty);
	}

	if (!tty->icanon && (tty->read_cnt >= tty->minimum_to_wake)) {
		if (tty->fasync)
			kill_fasync(tty->fasync, SIGIO);
		if (tty->read_wait)
			wake_up_interruptible(&tty->read_wait);
	}

	if ((tty->read_cnt >= TTY_THRESHOLD_THROTTLE) &&
	    tty->driver.throttle &&
	    !set_bit(TTY_THROTTLED, &tty->flags))
		tty->driver.throttle(tty);
}

static int n_tty_receive_room(struct tty_struct *tty)
{
	int	left = N_TTY_BUF_SIZE - tty->read_cnt - 1;

	/*
	 * If we are doing input canonicalization, and there are no
	 * pending newlines, let characters through without limit, so
	 * that erase characters will be handled.  Other excess
	 * characters will be beeped.
	 */
	if (tty->icanon && !tty->canon_data)
		return N_TTY_BUF_SIZE;

	if (left > 0)
		return left;
	return 0;
}

int is_ignored(int sig)
{
	return ((current->blocked & (1<<(sig-1))) ||
	        (current->sigaction[sig-1].sa_handler == SIG_IGN));
}

//根据终端设备的termios数据结构设置其tty_struct结构中的字符位图process_char_map
//和其它几个标志位（而不是设置termios结构），位图process_char_map的大小是32字节，
//共256位，其中的每一位都对应着一个字符，如果位图中的某一位为1,表示其对应的字符需要
//在“烹调”过程中加以特殊处理。
static void n_tty_set_termios(struct tty_struct *tty, struct termios * old)
{
	if (!tty)
		return;
	
	tty->icanon = (L_ICANON(tty) != 0);
	if (I_ISTRIP(tty) || I_IUCLC(tty) || I_IGNCR(tty) ||
	    I_ICRNL(tty) || I_INLCR(tty) || L_ICANON(tty) ||
	    I_IXON(tty) || L_ISIG(tty) || L_ECHO(tty) ||
	    I_PARMRK(tty)) {
		cli();
		memset(tty->process_char_map, 0, 256/32);

		if (I_IGNCR(tty) || I_ICRNL(tty))
			set_bit('\r', &tty->process_char_map);
		if (I_INLCR(tty))
			set_bit('\n', &tty->process_char_map);

		if (L_ICANON(tty)) {
			set_bit(ERASE_CHAR(tty), &tty->process_char_map);
			set_bit(KILL_CHAR(tty), &tty->process_char_map);
			set_bit(EOF_CHAR(tty), &tty->process_char_map);
			set_bit('\n', &tty->process_char_map);
			set_bit(EOL_CHAR(tty), &tty->process_char_map);
			if (L_IEXTEN(tty)) {
				set_bit(WERASE_CHAR(tty),
					&tty->process_char_map);
				set_bit(LNEXT_CHAR(tty),
					&tty->process_char_map);
				set_bit(EOL2_CHAR(tty),
					&tty->process_char_map);
				if (L_ECHO(tty))
					set_bit(REPRINT_CHAR(tty),
						&tty->process_char_map);
			}
		}
		if (I_IXON(tty)) {
			set_bit(START_CHAR(tty), &tty->process_char_map);
			set_bit(STOP_CHAR(tty), &tty->process_char_map);
		}
		if (L_ISIG(tty)) {
			set_bit(INTR_CHAR(tty), &tty->process_char_map);
			set_bit(QUIT_CHAR(tty), &tty->process_char_map);
			set_bit(SUSP_CHAR(tty), &tty->process_char_map);
		}
		clear_bit(__DISABLED_CHAR, &tty->process_char_map);
		sti();
		tty->raw = 0;
		tty->real_raw = 0;
	} else {
		tty->raw = 1;
		if ((I_IGNBRK(tty) || (!I_BRKINT(tty) && !I_PARMRK(tty))) &&
		    (I_IGNPAR(tty) || !I_INPCK(tty)) &&
		    (tty->driver.flags & TTY_DRIVER_REAL_RAW))
			tty->real_raw = 1;
		else
			tty->real_raw = 0;
	}
}

/**
*    n_tty_close        -    close the ldisc for this tty
*    @tty: device
*
*    Called from the terminal layer when this line discipline is
*    being shut down, either because of a close or becsuse of a
*    discipline change. The function will not be called while other
*    ldisc methods are in progress.
*/
static void n_tty_close(struct tty_struct *tty)
{
	tty_wait_until_sent(tty, 0);
	n_tty_flush_buffer(tty);
	if (tty->read_buf) {
		free_page((unsigned long) tty->read_buf);
		tty->read_buf = 0;
	}
}

static int n_tty_open(struct tty_struct *tty)
{
	if (!tty)
		return -EINVAL;
/*
	tty_driver结构中有一个filp成分，即tty_file_buffer数据结构，终端的
	设备输入缓冲区。终端设备的输入方式有两种，一种叫做“原始模式”，另一种叫做
	“加工模式”，在实际应用中大多使用加工模式，Unix的作者给“加工模式”取了个很
	形象的名字--“cooked”，即“经过烹调的”。它把从终端设备键盘接受到的字符流比
	作“生米”，而把经过处理以后的字符流比作“熟饭”。如果把tty_driver中的filp
	比作煮饭的郭，那么还得有个盘子来存放煮熟了的饭，为了这个目的，在tty_driver
	设置了一个read_buf指针，让它指向一个缓冲区，这就是盛饭碗的盘子。
*/
	if (!tty->read_buf) {
		tty->read_buf = (unsigned char *)
			get_free_page(intr_count ? GFP_ATOMIC : GFP_KERNEL);
		if (!tty->read_buf)
			return -ENOMEM;
	}
	memset(tty->read_buf, 0, N_TTY_BUF_SIZE);
	tty->read_head = tty->read_tail = tty->read_cnt = 0;
/*
	与上面的盘子缓冲区页面相对应，在tty_struct结构中还有一个位图read_flags[]
	位图中的每一位对应着上述缓冲区中的每个字符，在规范模式下用来分隔不同的缓冲行
*/
	memset(tty->read_flags, 0, sizeof(tty->read_flags));
	n_tty_set_termios(tty, 0);
	tty->minimum_to_wake = 1;
	tty->closing = 0;
	return 0;
}

//检查输入缓冲区中有否数据可供读出。
//在规范模式下检查的是经过加工以后的数量，而在原始模式下检查的是原始
//字符的数量
static inline int input_available_p(struct tty_struct *tty, int amt)
{
	if (L_ICANON(tty)) {
		if (tty->canon_data)
			return 1;
	} else if (tty->read_cnt >= (amt ? amt : 1))
		return 1;

	return 0;
}

/*
 * Helper function to speed up read_chan.  It is only called when
 * ICANON is off; it copies characters straight from the tty queue to
 * user space directly.  It can be profitably called twice; once to
 * drain the space from the tail pointer to the (physical) end of the
 * buffer, and once to drain the space from the (physical) beginning of
 * the buffer to head pointer.
 */
static inline void copy_from_read_buf(struct tty_struct *tty,
				      unsigned char **b,
				      unsigned int *nr)

{
	int	n;

	n = MIN(*nr, MIN(tty->read_cnt, N_TTY_BUF_SIZE - tty->read_tail));
	if (!n)
		return;
	memcpy_tofs(*b, &tty->read_buf[tty->read_tail], n);
	tty->read_tail = (tty->read_tail + n) & (N_TTY_BUF_SIZE-1);
	tty->read_cnt -= n;
	*b += n;
	*nr -= n;
}

/*
	一般而言，一个典型的读终端过程可以分成下列三部分，或者这三部分的循环：
	（1）、当前进程企图从终端的缓冲区读出，但是因为缓冲区中尚无足够的字符
		可供读出而受阻，进入睡眠；
	（2）、然后，但使用者在键盘（一个特例，可以是其他设备）上输入字符，底
		层驱动程序将足够的字符写入缓冲区以后，就把睡眠的进程唤醒；（设备驱动的主体）
	（3）、睡眠的进程被唤醒以后，继续完成读出
*/
static int read_chan(struct tty_struct *tty, struct file *file,
		     unsigned char *buf, unsigned int nr)
{
	struct wait_queue wait = { current, NULL };
	int c;
	unsigned char *b = buf;
	int minimum, time;
	int retval = 0;
	int size;

do_it_again:

	if (!tty->read_buf) {
		printk("n_tty_read_chan: called with read_buf == NULL?!?\n");
		return -EIO;
	}

	/* Job control check -- must be done at start and after
	   every sleep (POSIX.1 7.1.1.4). */
	/* NOTE: not yet done after every sleep pending a thorough
	   check of the logic of this change. -- jlc */
	/* don't stop on /dev/console */
	if (file->f_inode->i_rdev != CONSOLE_DEV &&
	    current->tty == tty) {
		if (tty->pgrp <= 0)
			printk("read_chan: tty->pgrp <= 0!\n");
		else if (current->pgrp != tty->pgrp) {
			if (is_ignored(SIGTTIN) ||
			    is_orphaned_pgrp(current->pgrp))
				return -EIO;
			kill_pg(current->pgrp, SIGTTIN, 1);
			return -ERESTARTSYS;
		}
	}

	if (L_ICANON(tty)) {
		minimum = time = 0;
		current->timeout = (unsigned long) -1;
	//原始模式，非规范模式
	} else {
/*
		在原始模式和非规范模式中，当输入缓冲区中有了最低限度的数据量minimum_to_wake
		时，就要唤醒正在等待着从该设备读出的进程。下面的代码用于确定一个合适的数值，
		这个数值一般都是1
*/
		time = (HZ / 10) * TIME_CHAR(tty);
		minimum = MIN_CHAR(tty);
		if (minimum) {
		  	current->timeout = (unsigned long) -1;
			if (time)
				tty->minimum_to_wake = 1;
			else if (!tty->read_wait ||
				 (tty->minimum_to_wake > minimum))
				tty->minimum_to_wake = minimum;
		} else {
			if (time) {
				current->timeout = time + jiffies;
				time = 0;
			} else
				current->timeout = 0;
			tty->minimum_to_wake = minimum = 1;
		}
	}
	//在当前进程的系统堆栈中准备下一个wait_queue_t数据结构wait，并把它挂入目标终端的等待队列
	//read_wait中，使终端设备的驱动程序在有数据可读时可以唤醒这个进程。当然，也许终端设备的
	//输入缓冲区中现在就有数据，因而根本就不需要进入睡眠，但是那也没有关系，读到了数据之后，再
	//把它从队列中摘除就可以了
	add_wait_queue(&tty->read_wait, &wait);
	while (1) {
/*
		对于伪终端设备，可以通过系统调用ioctl()将主/从两端的通信方式设置成“packet”(信包)模式
		因为这两端往往在通过网络链接的不同计算机中。这种情况下，tty_packet=1,而若tty_link_ctrl_status
		非0就表示有反映通信状态变化的控制信息需要提交，所以需要优先读出这些控制信息（一些标志位）
*/
		/* First test for status change. */
		if (tty->packet && tty->link->ctrl_status) {
			if (b != buf)
				break;
			put_fs_byte(tty->link->ctrl_status, b++);
			tty->link->ctrl_status = 0;
			break;
		}
		/* This statement must be first before checking for input
		   so that any interrupt will set the state back to
		   TASK_RUNNING. */
		current->state = TASK_INTERRUPTIBLE;
		
		//指针b开始时指向用户空间的缓冲区buf，随着字符的读出而向前推进
		//所以（b-buf）就是已经读出的字符数。如果minimum_to_wake开始
		//时不是1,那么在读出的过程中会向1逼近
		if (((minimum - (b - buf)) < tty->minimum_to_wake) &&
		    ((minimum - (b - buf)) >= 1))
			tty->minimum_to_wake = (minimum - (b - buf));
		//检查输入缓冲区中是否有数据可供读出。如果缓冲区中没有字符可供读出，
		//则当前进程一般要睡眠等待。到缓冲区中有了可供读出的字符时才会被唤醒
		//在规范模式下检查的是经过加工以后的数量，而在原始模式下检查的是原始
		//字符的数量
		if (!input_available_p(tty, 0)) {
			if (tty->flags & (1 << TTY_SLAVE_CLOSED)) {
				retval = -EIO;
				break;
			}
			if (tty_hung_up_p(file))
				break;
			if (!current->timeout)
				break;
			if (file->f_flags & O_NONBLOCK) {
				retval = -EAGAIN;
				break;
			}
			if (current->signal & ~current->blocked) {
				retval = -ERESTARTSYS;
				break;
			}
			//睡眠等待
			schedule();
			continue;
		}
		current->state = TASK_RUNNING;
		//执行到这里，说明进程已经被唤醒，缓冲区中已经有了数据（比如键盘按下，则键盘缓冲区中有了数据）
		/* Deal with packet mode. */
		if (tty->packet && b == buf) {
			put_fs_byte(TIOCPKT_DATA, b++);
			nr--;
		}
		
		//如果是规范模式，即加工模式
		if (L_ICANON(tty)) {
			while (1) {
				int eol;

				disable_bh(TQUEUE_BH);
/*
				在规范模式下，缓冲区中的字符是经过了加工的，要积累起一个“缓冲行”，即
				碰到“\n”字符才会唤醒等待读出的进程，此时tty->read_cnt表示缓冲行中
				字符的个数				
*/
				if (!tty->read_cnt) {
					enable_bh(TQUEUE_BH);
					break;
				}
				//tty_struct结构中的read_falgs是个位图，如果这个位图对应于
				//tty->read_tail这一位为1,则表示这个位置上已经是缓冲行的终点，
				//以后的数据就属于另一缓冲行
				eol = clear_bit(tty->read_tail,
						&tty->read_flags);
				//缓冲区tty->read_buf[]是按环形缓冲区使用的
				//tty->read_tail指向当前可供读出的地一个字符。
				c = tty->read_buf[tty->read_tail];
				tty->read_tail = ((tty->read_tail+1) &
						  (N_TTY_BUF_SIZE-1));
				tty->read_cnt--;
				enable_bh(TQUEUE_BH);
				//如果还没有到达缓冲行的终点
				if (!eol) {
					//逐个字符地调用put_fs_byte将其复制到用户空间去
					put_fs_byte(c, b++);
					if (--nr)
						continue;
					break;
				}
				if (--tty->canon_data < 0) {
					tty->canon_data = 0;
				}
				//这里的__DISABLED_CHAR就是\0,缓冲区中的最后一个字符如果是__DISABLED_CHAR
				//就不用复制到用户空间。__DISABLED_CHAR定义于tty.h
				if (c != __DISABLED_CHAR) {
					put_fs_byte(c, b++);
					nr--;
				}
				break;
			}
		//非规范模式
		} else {
/*
			在非规范模式中，缓冲区同样是tty->read_bu[],也按照环形缓冲区使用，
			但是缓冲区中的字符是未经过加工的，也没有“缓冲行”。另一方面，对于原始
			模式并没有不让把“\0”复制到用户空间的规定，所以这里通过copy_from_read_buf
			进行成片的复制，以加快速度
*/
			//通过此copy_from_read_buf函数进行成片的复制，
			//由于缓冲区是环形的，缓冲区的字符可能分成两段，所以调用两次
			disable_bh(TQUEUE_BH);
			copy_from_read_buf(tty, &b, &nr);
			copy_from_read_buf(tty, &b, &nr);
			enable_bh(TQUEUE_BH);
		}

		/* If there is enough space in the read buffer now, let the
		   low-level driver know. */
/*
		缓冲区的大小总是有限的，比如，如果从键盘打入字符的速度很快，而应用程序又来不及
		从缓冲区读出，则底层的驱动程序（主要是中断服务程序）可能已经因为缓冲区满而暂时
		把“阀门”关闭了。现在，如果缓冲区中剩余的字数量降到了“低水位”--TTY_THRESHOLD_UNTHROTTLE
		 以下，则打开阀门。
*/
		//缓冲区可以重新接收数据了，就打开开关让底层的驱动接受数据
		if (tty->driver.unthrottle &&
		    (tty->read_cnt <= TTY_THRESHOLD_UNTHROTTLE)
		    && clear_bit(TTY_THROTTLED, &tty->flags))
			//除了清除代表着阀门的标志位TTY_THROTTLED外，还可能要
			//调用一个函数，具体取决于终端的tty_driver数据结构。
			//对于控制台的tty_driver数据结构console_driver
			//这个函数是con_unthtottle()[唤醒可能在等待着要把数据写入缓冲区的进程]
			tty->driver.unthrottle(tty);
/*
		这里的buf指针指向用户空间的缓冲区，而b指出向该缓冲区中的下一个空闲位置
		所以，b-buf就是已经读入该缓冲区中的字符的数量。参数nr只是表明用户空间
		的大小，即读出字符数量的上限，在规范模式下实际读出的字符数取决于具体的
		缓冲行
*/
		if (b - buf >= minimum || !nr)
			break;
		if (time)
			current->timeout = time + jiffies;
	}
	remove_wait_queue(&tty->read_wait, &wait);

	if (!tty->read_wait)
		tty->minimum_to_wake = minimum;

	current->state = TASK_RUNNING;
	current->timeout = 0;
	size = b - buf;
/*
	标志位TTY_PUSH是由底层驱动程序在读到一个EOF字符并将其放入缓冲区时设置
	成1的，表示要让用户尽快把缓冲区中的内容读走。如果此后从缓冲区读出了缓冲区
	中所有的字符，就把这个标志位清零并结束整个读出操作。否则，就说明还要继续从
	缓冲区读。所以如果本次读操作实际并未读出，则不让它结束，即 goto do_it_again
	如果本次多少已经读出了若干字节则允许下次再读
*/
	if (size && nr)
	        clear_bit(TTY_PUSH, &tty->flags);
	if (!size && clear_bit(TTY_PUSH, &tty->flags))
		goto do_it_again;
	if (!size && !retval)
	        clear_bit(TTY_PUSH, &tty->flags);
        return (size ? size : retval);
}
/*
	这段代码中使用了wait等待队列，为什么要使用等待队列呢？我们在应用层打开一个设备文件的时候，
	有两种方式，阻塞和非阻塞，非阻塞很简单，不管结果怎样直接返回。但阻塞则有点死皮赖脸的意思，
	会一直等待，直到操作完成。那write函数的“阻塞”版本在内核里边是怎么实现的呢？就是使用等待队
	列，只要条件没有得到满足（驱动层调用write函数失败），那么就一直让出cpu，直到条件满足了才
	会继续执行，并将写操作的结果返回给上层。
	通过以上分析，我们也可以得到如下结论：阻塞是在ldisc层也就是线路规程里边实现的。出于代价
	和操作性的考虑，我们不会再驱动里边实现阻塞类型的write/read函数
*/
//write_chain主要根据数据是否是经过加工的调用tty->ops->flush_chars或者
//tty->ops->write把数据写入设备，当写入的空间不足时，且数据没有完全写完则
//调用schedule()把写操作加入写等待队列
static int write_chan(struct tty_struct * tty, struct file * file,
		      unsigned char * buf, unsigned int nr)
{
	//将当前进程放到等待队列中
	struct wait_queue wait = { current, NULL };
	int c;
	unsigned char *b = buf;
	int retval = 0;

	/* Job control check -- must be done at start (POSIX.1 7.1.1.4). */
	if (L_TOSTOP(tty) && file->f_inode->i_rdev != CONSOLE_DEV) {
		retval = tty_check_change(tty); //进程控制终端相关设置
		if (retval)
			return retval;
	}

	add_wait_queue(&tty->write_wait, &wait); //代表写进程的等待队列项加入到写等待队列中
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		if (current->signal & ~current->blocked) {
			retval = -ERESTARTSYS;
			break;
		}
		//进入此处继续执行的原因可能是被信号打断，而不是条件得到了满足。
		//只有条件得到了满足，我们才会继续，否则，直接返回！
		if (tty_hung_up_p(file) || (tty->link && !tty->link->count)) {
			retval = -EIO;
			break;
		}
		//OPOST设置，则操作可以选择加工过的输
		if (O_OPOST(tty)) {
			while (nr > 0) {
				c = get_fs_byte(b);
				if (opost(c, tty) < 0)
					break;
				b++; nr--;
			}
			if (tty->driver.flush_chars)
				tty->driver.flush_chars(tty);
		} else {
			//调用到具体的驱动中的write函数
			c = tty->driver.write(tty, 1, b, nr);
			b += c;
			nr -= c;
		}
		if (!nr)
			break;
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			break;
		}
		//假如是以非阻塞的方式打开的，那么也直接返回。否则，让出cpu，等条件满足以后再继续执行
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&tty->write_wait, &wait);
	return (b - buf) ? b - buf : retval;
}

static int normal_select(struct tty_struct * tty, struct inode * inode,
			 struct file * file, int sel_type, select_table *wait)
{
	switch (sel_type) {
		case SEL_IN:
			if (input_available_p(tty, TIME_CHAR(tty) ? 0 :
					      MIN_CHAR(tty)))
				return 1;
			/* fall through */
		case SEL_EX:
			if (tty->packet && tty->link->ctrl_status)
				return 1;
			if (tty->flags & (1 << TTY_SLAVE_CLOSED))
				return 1;
			if (tty_hung_up_p(file))
				return 1;
			if (!tty->read_wait) {
				if (MIN_CHAR(tty) && !TIME_CHAR(tty))
					tty->minimum_to_wake = MIN_CHAR(tty);
				else
					tty->minimum_to_wake = 1;
			}
			select_wait(&tty->read_wait, wait);
			return 0;
		case SEL_OUT:
			if (tty->driver.chars_in_buffer(tty) < WAKEUP_CHARS)
				return 1;
			select_wait(&tty->write_wait, wait);
			return 0;
	}
	return 0;
}

/*
不管是哪一种设备，开始时总是采用与下标N_TTY对应的tty_ldisc
结构，实际上这个结构是tty_ldisc_N_TTY,即设备刚打开初始化
时都是以其 为模板初始化ldisc结构的
*/
//不同的tty类型的设备，具有不同的线路规程。这一层也由内核实现
//从tty_read/tty_write函数可以看出，他们最后调用到了线路规
//程的read/write函数
struct tty_ldisc tty_ldisc_N_TTY = {
	TTY_LDISC_MAGIC,	/* magic */
	0,			/* num */
	0,			/* flags */
	n_tty_open,		/* open */
	n_tty_close,		/* close */
	n_tty_flush_buffer,	/* flush_buffer */
	n_tty_chars_in_buffer,	/* chars_in_buffer */
	//tty_read函数的主体就是调用tty->ldisc.read函数，完成冲缓冲区read_buf到用户空间的复制
	read_chan,		/* read */
	write_chan,		/* write */
	n_tty_ioctl,		/* ioctl */
	n_tty_set_termios,	/* set_termios */
	normal_select,		/* select */
	n_tty_receive_buf,	/* receive_buf */
	n_tty_receive_room,	/* receive_room */
	0			/* write_wakeup */
};

