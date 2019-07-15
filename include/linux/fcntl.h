#ifndef _LINUX_FCNTL_H
#define _LINUX_FCNTL_H

/* open/fcntl - O_SYNC is only implemented on blocks devices and on files
   located on an ext2 file system */
#define O_ACCMODE	  0003
#define O_RDONLY	    00
#define O_WRONLY	    01
#define O_RDWR		    02
#define O_CREAT		  0100	/* not fcntl */
#define O_EXCL		  0200	/* not fcntl */
#define O_NOCTTY	  0400	/* not fcntl */
#define O_TRUNC		 01000	/* not fcntl */
#define O_APPEND	 02000
#define O_NONBLOCK	 04000
#define O_NDELAY	O_NONBLOCK
#define O_SYNC		010000
#define FASYNC		020000	/* fcntl, for BSD compatibility */

#define F_DUPFD		0	/* dup */
#define F_GETFD		1	/* get f_flags */
#define F_SETFD		2	/* set f_flags */
#define F_GETFL		3	/* more flags (cloexec) */
#define F_SETFL		4
#define F_GETLK		5
#define F_SETLK		6
#define F_SETLKW	7

#define F_SETOWN	8	/*  for sockets. */
#define F_GETOWN	9	/*  for sockets. */

/* for F_[GET|SET]FL */
#define FD_CLOEXEC	1	/* actually anything with low bit set goes */

#define F_RDLCK		0
#define F_WRLCK		1
#define F_UNLCK		2

/* For bsd flock () */
#define F_EXLCK		4	/* or 3 */
#define F_SHLCK		8	/* or 4 */

//我觉得struct flock结构和Fs.h中定义的struct file_lock结构不同之处在于
//前者是专用于fcntl函数的简化参数，而后者是内核专用，记录了比前者更多的、
//用户不必关心的、内核自动维护的信息，比如文件锁链表指针等
struct flock {
	short l_type;	//Type of lock: F_RDLCK	,F_WRLCK, F_UNLCK
	short l_whence;	//How to interpret l_start:SEEK_SET, SEEK_CUR, SEEK_END
	off_t l_start;	/* Starting offset for lock */	//相对于l_whence参数值的start值
	off_t l_len;	/* Number of bytes to lock */
	pid_t l_pid;	/* PID of process blocking our lock
                               (F_GETLK only) */
};
/*
	l_type	 Describes the type of lock. If the value of the Command parameter to the fcntl subroutine
	 is F_SETLK orF_SETLKW, the l_type field indicates the type of lock to be created. Possible values are:
	F_RDLCK
	A read lock is requested.
	F_WRLCK
	A write lock is requested.
	F_UNLCK
	Unlock. An existing lock is to be removed.
	If the value of the Command parameter to the fcntl subroutine is F_GETLK, the l_type field describes an
	 existing lock. Possible values are:

	F_RDLCK
	A conflicting read lock exists.
	F_WRLCK
	A conflicting write lock exists.
	F_UNLCK
	No conflicting lock exists.
	l_whence	 Defines the starting offset. The value of this field indicates the point from which the
	 relative offset, the l_startfield, is measured. Possible values are:
	SEEK_SET
	The relative offset is measured from the start of the file.
	SEEK_CUR
	The relative offset is measured from the current position.
	SEEK_END
	The relative offset is measured from the end of the file.
	These values are defined in the unistd.h file.

	l_start	 Defines the relative offset in bytes, measured（测量） from the starting point in the l_whence field.
	l_len	 Specifies the number of consecutive bytes to be locked.
	l_sysid	 Contains the ID of the node that already has a lock placed on the area defined by the fcntl subroutine.
	 This field is returned only when the value of the Command parameter is F_GETLK.
	l_pid	 Contains the ID of a process that already has a lock placed on the area defined by the fcntl subroutine. 
	This field is returned only when the value of the Command parameter is F_GETLK.
	l_vfs


	Specifies the file system type of the node identified in the l_sysid field.
*/
#endif
