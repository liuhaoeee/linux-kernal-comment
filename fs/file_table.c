/*
 *  linux/fs/file_table.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

//Linux操作系统内核就是这样 曾经风云变幻 到最后却被千万行的代码淹没的无影无踪  
//幸好 他有时又会留下一点蛛丝马迹 引领我们穿越操作系统内部所有的秘密
/*
	The PDF documents about understanding Linux Kernel 1.2 are made by Liu Yihao
	这份学习Linux内核的PDF文档由刘以浩同学整理完成
	包含了作者在学习内核过程中搜集到的大量有关Linux
	内核的知识和自己的一些理解以及注释
	当然，最宝贵的还是这份原生的内核代码
	NOTE：如果在阅读过程中遇到什么问题，欢迎和我交流讨论
	Email：liuyihaolovem@163.com
*/

//此文件即为简单，建议对buffer.c文件有所了解之后再看

/*
					文件对象
	文件对象是已打开的文件在内存中的表示，主要用于建立进程和磁盘上的文件的对应关系。它由sys_open()
	现场创建，由sys_close()销毁。文件对象和物理文件的关系有点像进程和程序的关系一样。当我们站在用户
	空间来看 待VFS，我们像是只需与文件对象打交道，而无须关心超级块，索引节点或目录项。因为多个进程
	可以同时打开和操作 同一个文件，所以同一个文件也可能存在多个对应的文件对象。文件对象仅仅在进程观
	点上代表已经打开的文件，它 反过来指向目录项对象（反过来指向索引节点）。一个文件对应的文件对象可能
	不是惟一的，但是其对应的索引节点和 目录项对象无疑是惟一的。
*/

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mm.h>

struct file * first_file;	//当前内核中所有扥file节点均链入到此链表中
int nr_files = 0;	//first_file链表中节点个数

//将file结构插入到first_file链表头部
static void insert_file_free(struct file *file)
{
	file->f_next = first_file;
	file->f_prev = first_file->f_prev;
	file->f_next->f_prev = file;
	file->f_prev->f_next = file;
	first_file = file;
}

//将指定的file结构从first_file链表中移除
static void remove_file_free(struct file *file)
{
	if (first_file == file)
		first_file = first_file->f_next;
	if (file->f_next)
		file->f_next->f_prev = file->f_prev;
	if (file->f_prev)
		file->f_prev->f_next = file->f_next;
	file->f_next = file->f_prev = NULL;
}

//将指定的file放到first_file链表尾部
static void put_last_free(struct file *file)
{
	remove_file_free(file);
	file->f_prev = first_file->f_prev;
	file->f_prev->f_next = file;
	file->f_next = first_file;
	file->f_next->f_prev = file;
}

//增加内核中struct_file数据结构个数
void grow_files(void)
{
	struct file * file;
	int i;

	file = (struct file *) get_free_page(GFP_KERNEL);

	if (!file)
		return;

	nr_files+=i= PAGE_SIZE/sizeof(struct file);

	if (!first_file)
		file->f_next = file->f_prev = first_file = file++, i--;

	for (; i ; i--)
		insert_file_free(file++);
}

unsigned long file_table_init(unsigned long start, unsigned long end)
{
	first_file = NULL;
	return start;
}

//获取一个空的file结构
struct file * get_empty_filp(void)
{
	int i;
	struct file * f;

	if (!first_file)
		grow_files();
repeat:
	for (f = first_file, i=0; i < nr_files; i++, f = f->f_next)
		if (!f->f_count) {
			remove_file_free(f);
			memset(f,0,sizeof(*f));
			put_last_free(f);
			f->f_count = 1;
			f->f_version = ++event;
			return f;
		}
	if (nr_files < NR_FILE) {
		grow_files();
		goto repeat;
	}
	return NULL;
}
