#ifndef _LINUX_STAT_H
#define _LINUX_STAT_H

#ifdef __KERNEL__
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
struct old_stat {
	unsigned short st_dev;
	unsigned short st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned long  st_size;
	unsigned long  st_atime;
	unsigned long  st_mtime;
	unsigned long  st_ctime;
};

struct new_stat {
	unsigned short st_dev;	//文件的设备编号
	unsigned short __pad1;	//文件的i-node
	unsigned long st_ino;	//文件的类型和存取的权限
	unsigned short st_mode;	
	unsigned short st_nlink;	//连到该文件的硬连接数目，刚建立的文件值为1
	unsigned short st_uid;	//文件所有者的用户识别码
	unsigned short st_gid;	//文件所有者的组识别码
	unsigned short st_rdev;	//若此文件为装置设备文件，则为其设备编号
	unsigned short __pad2;	
	unsigned long  st_size;	//文件大小，以字节计算
	unsigned long  st_blksize;	//文件系统的I/O 缓冲区大小
	unsigned long  st_blocks;	//占用文件区块的个数，每一区块大小为512 个字节
	unsigned long  st_atime;
	unsigned long  __unused1;
	unsigned long  st_mtime;
	unsigned long  __unused2;
	unsigned long  st_ctime;
	unsigned long  __unused3;
	unsigned long  __unused4;
	unsigned long  __unused5;
};

#endif
/*
	S_IFMT 0170000 文件类型的位遮罩
	S_IFSOCK 0140000 scoket
	S_IFLNK 0120000 符号连接
	S_IFREG 0100000 一般文件
	S_IFBLK 0060000 区块装置
	S_IFDIR 0040000 目录
	S_IFCHR 0020000 字符装置
	S_IFIFO 0010000 先进先出
	S_ISUID 04000 文件的（set user-id on execution）位
	S_ISGID 02000 文件的（set group-id on execution）位
	S_ISVTX 01000 文件的sticky位
	S_IRUSR（S_IREAD） 00400 文件所有者具可读取权限
	S_IWUSR（S_IWRITE）00200 文件所有者具可写入权限
	S_IXUSR（S_IEXEC） 00100 文件所有者具可执行权限
	S_IRGRP 00040 用户组具可读取权限
	S_IWGRP 00020 用户组具可写入权限
	S_IXGRP 00010 用户组具可执行权限
	S_IROTH 00004 其他用户具可读取权限
	S_IWOTH 00002 其他用户具可写入权限
	S_IXOTH 00001 其他用户具可执行权限
	上述的文件类型在POSIX 中定义了检查这些类型的宏定义
	S_ISLNK （st_mode） 判断是否为符号连接
	S_ISREG （st_mode） 是否为一般文件
	S_ISDIR （st_mode）是否为目录
	S_ISCHR （st_mode）是否为字符装置文件
	S_ISBLK （s3e） 是否为先进先出
	S_ISSOCK （st_mode） 是否为socket
	若一目录具有sticky 位（S_ISVTX），则表示在此目录下的文件只能被该文件所有者、此目录所有者或root来删除或改名。
*/
#define S_IFMT  00170000
#define S_IFSOCK 0140000
#define S_IFLNK	 0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)
//当新创建一个文件时，需要指定mode 参数:
#define S_IRWXU 00700	//文件拥有者有读写执行权限
#define S_IRUSR 00400	//文件拥有者仅有读权限
#define S_IWUSR 00200	//文件拥有者仅有写权限
#define S_IXUSR 00100	//文件拥有者仅有执行权限

#define S_IRWXG 00070	//组用户有读写执行权限
#define S_IRGRP 00040	//组用户仅有读权限
#define S_IWGRP 00020	//组用户仅有写权限
#define S_IXGRP 00010	//组用户仅有执行权限

#define S_IRWXO 00007	//其他用户有读写执行权限
#define S_IROTH 00004	//其他用户仅有读权限
#define S_IWOTH 00002	//其他用户仅有写权限
#define S_IXOTH 00001	//其他用户仅有执行权限

#ifdef __KERNEL__
#define S_IRWXUGO	(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
#define S_IRUGO		(S_IRUSR|S_IRGRP|S_IROTH)
#define S_IWUGO		(S_IWUSR|S_IWGRP|S_IWOTH)
#define S_IXUGO		(S_IXUSR|S_IXGRP|S_IXOTH)
#endif

#endif
