#ifndef _LINUX_TTY_LDISC_H
#define _LINUX_TTY_LDISC_H

/*
 * Definitions for the tty line discipline
 */

#include <linux/fs.h>
#include <linux/wait.h>

/*
	tty设备有串口、usb转串口、调制解调器（传统的WinModem类设备）等。
	Linux-tty驱动程序的核心紧挨在标准字符设备驱动层之下，并 统了一
	系列的功能，作为接口被终端类型设备使用。内核负责控制通过tty设备的
	数据流，并且格式化这些数据。为了控制数据流，有许多不同的线路规程
	（line discipline）可以虚拟地“插入”任何的tty设备上，这由不同的
	tty线路规程驱动程序实现。tty线路规程的作用是使用特殊的方法，把从
	用户或者硬件那里接受的数据格式化。这种格式化通常使用一些协议来完成
	转换，比如PPP或者是蓝牙Bluetooth
*/
/*
	结构名中的“ldisc”应为“line discipline”的缩写，表示“链路规则”的意思。与file_operations
	不同的是，这个结构中不但有供上层调用的函数指针如open、read、write等，还有供下层调用的函数
	指针 receive_buf、receive_room以及write_wakeup。此外，结构中还有几个并非函数指针的
	字段。内核中有个ldisc数组，用于各种不同的链路规则，包括实际上并不使用的“链路规则”，定义于
	tty_io.c

	如果把具体终端设备的驱动程序称为“终端设备层”的话，那么tty_driver结构是它与其
	上层，即常规的设备驱动层之间的界面，而tty_ldisc结构则是它与其下层，即物理设
	备层之间的界面。所谓“链路规则”，实际上就是怎样驱动具体的物理接口	
*/
struct tty_ldisc {
	int	magic;
	int	num;
	int	flags;
	/*
	 * The following routines are called from above.
	 */
	int	(*open)(struct tty_struct *);
	void	(*close)(struct tty_struct *);
	void	(*flush_buffer)(struct tty_struct *tty);
	int	(*chars_in_buffer)(struct tty_struct *tty);
	int	(*read)(struct tty_struct * tty, struct file * file,
			unsigned char * buf, unsigned int nr);
	int	(*write)(struct tty_struct * tty, struct file * file,
			 unsigned char * buf, unsigned int nr);	
	int	(*ioctl)(struct tty_struct * tty, struct file * file,
			 unsigned int cmd, unsigned long arg);
	void	(*set_termios)(struct tty_struct *tty, struct termios * old);
	int	(*select)(struct tty_struct * tty, struct inode * inode,
			  struct file * file, int sel_type,
			  struct select_table_struct *wait);
	
	/*
	 * The following routines are called from below.
	 */
	void	(*receive_buf)(struct tty_struct *, unsigned char *cp,
			       char *fp, int count);
	int	(*receive_room)(struct tty_struct *);
	void	(*write_wakeup)(struct tty_struct *);
};

#define TTY_LDISC_MAGIC	0x5403

#define LDISC_FLAG_DEFINED	0x00000001

#endif /* _LINUX_TTY_LDISC_H */
