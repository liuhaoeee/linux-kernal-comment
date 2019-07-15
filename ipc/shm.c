#define THREE_LEVEL
/*
 * linux/ipc/shm.c
 * Copyright (C) 1992, 1993 Krishna Balasubramanian
 *         Many improvements/fixes by Bruno Haible.
 * Replaced `struct shm_desc' by `struct vm_area_struct', July 1994.
 */
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

/*
			Shared Memory （共享内存）
    共享内存允许一个或多个进程通过同时出现在它们的虚拟地址空间的内存通讯。
	这块虚拟内存的页面在每一个共享进程的页表中都有页表条目引用。但是不需
	要在所有进程的虚拟内存都有相同的地址。
*/
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/ipc.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/malloc.h>

#include <asm/segment.h>
#include <asm/pgtable.h>

extern int ipcperms (struct ipc_perm *ipcp, short shmflg);
extern unsigned int get_swap_page (void);
static int findkey (key_t key);
static int newseg (key_t key, int shmflg, int size);
static int shm_map (struct vm_area_struct *shmd);
static void killseg (int id);
static void shm_open (struct vm_area_struct *shmd);
static void shm_close (struct vm_area_struct *shmd);
static pte_t shm_swap_in(struct vm_area_struct *, unsigned long, unsigned long);

static int shm_tot = 0; /* total number of shared memory pages */
static int shm_rss = 0; /* number of shared memory pages that are in memory */
static int shm_swp = 0; /* number of shared memory pages that are in swap */
static int max_shmid = 0; /* every used id is <= max_shmid */
static struct wait_queue *shm_lock = NULL; /* calling findkey() may need to wait */
/*
		每一个新创建的内存区域都用一个shmid_ds数据结构来表达。这些数据结构
		保存在shm_segs向量表中.Shmid_ds数据结构描述了这个共享内存区有多大、
		多少个进程在使用它以及共享内存如何映射到它们的地址空间。由共享内存
		的创建者来控制对于这块内存的访问权限和它的key是公开或私有。如果有足
		够的权限它也可以把共享内存锁定在物理内存中.
		其实共享内存就是一个虚拟文件
*/
//共享内存描述符指针数组
static struct shmid_ds *shm_segs[SHMMNI];

static unsigned short shm_seq = 0; /* incremented, for recognizing stale（陈腐的） ids */

/* some statistics */
//尝试将共享内存占用的一页物理内存交换到交换设备中去的次数
static ulong swap_attempts = 0;
//成功将共享内存占用的一页物理内存交换到交换设备中去的次数
static ulong swap_successes = 0;
//系统中共享内存段的总数
static ulong used_segs = 0;

//初始化共享内存区数组
void shm_init (void)
{
	int id;

	//遍历共享内存区数组shm_segs，将数组的每一项都设置为IPC_UNUSED
	for (id = 0; id < SHMMNI; id++)
		shm_segs[id] = (struct shmid_ds *) IPC_UNUSED;
	shm_tot = shm_rss = shm_seq = max_shmid = used_segs = 0;
	shm_lock = NULL;
	return;
}

//根据shm的key找到其在shm_segs数组中的下标，即id
static int findkey (key_t key)
{
	int id;
	struct shmid_ds *shp;

	//遍历shm_segs数组，max_shmid是数组中最大的有效id值
	//即索引超过max_shmid的数组项都还未使用
	for (id = 0; id <= max_shmid; id++) {
		/*IPC_NOID: being allocated/destroyed */
		//IPC_NOID表示shm正在被分配或者销毁，所以要睡眠等待
		while ((shp = shm_segs[id]) == IPC_NOID)
			sleep_on (&shm_lock);
		if (shp == IPC_UNUSED)
			continue;
		if (key == shp->shm_perm.key)
			return id;
	}
	return -1;
}

/*
 * allocate new shmid_ds and pgtable. protected by shm_segs[id] = NOID.
 */
 //size是以字节为单位的内存大小
static int newseg (key_t key, int shmflg, int size)
{
	struct shmid_ds *shp;
	//将申请的内存字节数转换为页数
	int numpages = (size + PAGE_SIZE -1) >> PAGE_SHIFT;
	int id, i;

	//如果要创建的shm的大小小于最小的共享内存段的大小（1B）
	if (size < SHMMIN)
		return -EINVAL;
	//如果shm的总页数将大于系统允许的最大数，则返回一个表示没有空间的值
	if (shm_tot + numpages >= SHMALL)
		return -ENOSPC;
	//寻找一项还未使用的shm_segs数组项
	for (id = 0; id < SHMMNI; id++)
		if (shm_segs[id] == IPC_UNUSED) {
			//将找到的数组项设置为IPC_NOID，这样别的相关进程访问时就会睡眠在此项上
			shm_segs[id] = (struct shmid_ds *) IPC_NOID;
			goto found;
		}
	return -ENOSPC;

found:
	//申请一项shmid_ds的空间 由于申请kmalloc()可能导致进程睡眠，所以可能有进程因
	//shp而睡眠
	shp = (struct shmid_ds *) kmalloc (sizeof (*shp), GFP_KERNEL);
	//如果申请失败
	if (!shp) {
		//将找到的数组项还原成未占用的状态
		shm_segs[id] = (struct shmid_ds *) IPC_UNUSED;
		//如果shm_lock不为空，说明有进程睡眠在此队列中
		//则尝试唤醒之	
		if (shm_lock)
			wake_up (&shm_lock);
		return -ENOMEM;
	}

	//shm_pages：用于跟踪这块共享内存区域页面分配的一个“页表”——“页表
	//”在这里加了引号，是因为它不是一个真正的、硬件支持的页表。
	//不过它完成同样的工作。所以这里要分配numpages个ulong类型的变量，作为页表项使用
	shp->shm_pages = (ulong *) kmalloc (numpages*sizeof(ulong),GFP_KERNEL);
	//若分配失败
	if (!shp->shm_pages) {
		shm_segs[id] = (struct shmid_ds *) IPC_UNUSED;
		if (shm_lock)
			wake_up (&shm_lock);
		kfree(shp);
		return -ENOMEM;
	}

	//用一个for循环将共享内存的“页表项”清零
	for (i = 0; i < numpages; shp->shm_pages[i++] = 0);
	//增加系统中的共享内存总页数
	shm_tot += numpages;
	//初始化共享内存段对象的权限信息结构
	shp->shm_perm.key = key;
	shp->shm_perm.mode = (shmflg & S_IRWXUGO);
	shp->shm_perm.cuid = shp->shm_perm.uid = current->euid;
	shp->shm_perm.cgid = shp->shm_perm.gid = current->egid;
	shp->shm_perm.seq = shm_seq;
	shp->shm_segsz = size;
	shp->shm_cpid = current->pid;
	shp->attaches = NULL;
	shp->shm_lpid = shp->shm_nattch = 0;
	shp->shm_atime = shp->shm_dtime = 0;
	shp->shm_ctime = CURRENT_TIME;
	shp->shm_npages = numpages;

	//更新max_shmid值，使之总是代表有效she_segs项的最大下标值
	if (id > max_shmid)
		max_shmid = id;
	shm_segs[id] = shp;
	used_segs++;
	if (shm_lock)
		wake_up (&shm_lock);
	//返回此共享内存段的标识符
	//从这个公式可以看出，由资源标识符可以得出其序列号和在共享内存描述符数组中的下标
	return (unsigned int) shp->shm_perm.seq * SHMMNI + id;
}

/*
			shmget
	int shmget(key_t   key, size_t   size, int   flag);
	key: 标识符的规则
	size:共享存储段的字节数
	flag:读写的权限
	返回值：成功返回共享存储的id，失败返回-1
	key_t key
	-----------------------------------------------
	key标识共享内存的键值: 0/IPC_PRIVATE。 当key的取值为IPC_PRIVATE，则函数shmget()
	将创建一块新的共享内存；如果key的取值为0，而参数shmflg中设置了IPC_PRIVATE这个标
	志，则同样将创建一块新的共享内存。
	在IPC的通信模式下，不管是使用消息队列还是共享内存，甚至是信号量，每个IPC的对象
	(object)都有唯一的名字，称为“键”(key)。通过“键”，进程能够识别所用的对象。“键”与
	IPC对象的关系就如同文件名称之于文件，通过文件名，进程能够读写文件内的数据，甚至
	多个进程能够共用一个文件。而在IPC的通讯模式下，通过“键”的使用也使得一个IPC对象能
	为多个进程所共用。
	Linux系统中的所有表示System V中IPC对象的数据结构都包括一个ipc_perm结构，其中包含
	有IPC对象的键值，该键用于查找System V中IPC对象的引用标识符。如果不使用“键”，进程
	将无法存取IPC对象，因为IPC对象并不存在于进程本身使用的内存中。
	通常，都希望自己的程序能和其他的程序预先约定一个唯一的键值，但实际上并不是总可能
	的，因为自己的程序无法为一块共享内存选择一个键值。因此，在此把key设为IPC_PRIVATE，
	这样，操作系统将忽略键，建立一个新的共享内存，指定一个键值，然后返回这块共享内存IPC标识符ID。
	而将这个新的共享内存的标识符ID告诉其他进程可以在建立共享内存后通过派生子进程，或写入文件或管道来实现。


	int size(单位字节Byte)
	-----------------------------------------------
    size是要建立共享内存的长度。所有的内存分配操作都是以页为单位的。所以如果一段进程只申请
	一块只有一个字节的内存，内存也会分配整整一页(在i386机器中一页的缺省大小PACE_SIZE=4096
	字节)这样，新创建的共享内存的大小实际上是从size这个参数调整而来的页面大小。即如果size
	为1至4096，则实际申请到的共享内存大小为4K(一页)；4097到8192，则实际申请到的共享内存大小
	为8K(两页)，依此类推。


	int shmflg
	-----------------------------------------------
    shmflg主要和一些标志有关。其中有效的包括IPC_CREAT和IPC_EXCL，它们的功能与open()的O_CREAT和O_EXCL相当。
    IPC_CREAT   如果共享内存不存在，则创建一个共享内存，否则打开操作。
    IPC_EXCL    只有在共享内存不存在的时候，新的共享内存才建立，否则就产生错误。
    如果单独使用IPC_CREAT，shmget()函数要么返回一个已经存在的共享内存的操作符，要么返回一个
	新建的共享内存的标识符。如果将IPC_CREAT和IPC_EXCL标志一起使用，shmget()将返回一个新建的
	共享内存的标识符；如果该共享内存已存在，或者返回-1。IPC_EXEL标志本身并没有太大的意义，但
	是和IPC_CREAT标志一起使用可以用来保证所得的对象是新建的，而不是打开已有的对象。对于用户的
	读取和写入许可指定SHM_R和SHM_W,(SHM_R>3)和(SHM_W>3)是一组读取和写入许可，而(SHM_R>6)和
	(SHM_W>6)是全局读取和写入许可。


	返回值
	-----------------------------------------------
	成功返回共享内存的标识符；不成功返回-1，errno储存错误原因。
    EINVAL        参数size小于SHMMIN或大于SHMMAX。
    EEXIST        预建立key所致的共享内存，但已经存在。
    EIDRM         参数key所指的共享内存已经删除。
    ENOSPC        超过了系统允许建立的共享内存的最大值(SHMALL )。
    ENOENT        参数key所指的共享内存不存在，参数shmflg也未设IPC_CREAT位。
    EACCES        没有权限。
    ENOMEM        核心内存不足。
*/
//得到一个共享内存标识符或创建一个共享内存对象并返回共享内存标识符
//有点类似于文件的打开操作（key是文件名，shmflag是文件打开标志）
//从这里也可以看出，key相当于文件句柄或者说文件路径这样一个看得见摸得着的对象
//而IPC资源标识符相当于文件的inode，是全局唯一的
int sys_shmget (key_t key, int size, int shmflg)
{
	struct shmid_ds *shp;
	int id = 0;

	//检验size值的有效性
	if (size < 0 || size > SHMMAX)
		return -EINVAL;
	//如果key为IPC_PRIVATE，则表示要求创建一块新的私有的共享内存段
	//IPC_PRIVATE为0，是一个特殊的键值
	if (key == IPC_PRIVATE)
		return newseg(key, shmflg, size);
	//如果当前不存在键为key的IPC对象
	if ((id = findkey (key)) == -1) {
		//如果没有指定要创建IPC对象
		if (!(shmflg & IPC_CREAT))
			return -ENOENT;
		return newseg(key, shmflg, size);
	}
	//执行到这里，说明“文件”存在
	
	//如果设置了要创建“文件”的标志,并且也设置IPC_EXCL标志（表示独占），则返回-EEXIST
	if ((shmflg & IPC_CREAT) && (shmflg & IPC_EXCL))
		return -EEXIST;
	//取此共享内存段描述符对象
	shp = shm_segs[id];
	/* SHM_DEST:segment will be destroyed on last detach */
	//SHM_DEST：如果设置了此标志，则在关闭共享内存时，最终没有进程
	//附加在此共享内存段上时，将销毁此共享内存对象。见shm_close()函数
	//但是这里是获取共享内存对象，不能设置此标志。此标志只在创建或者
	//sys_shmctl()函数中改变
	if (shp->shm_perm.mode & SHM_DEST)
		return -EIDRM;
	//若要求得到的共享内存大小大于此共享内存段的大小
	if (size > shp->shm_segsz)
		return -EINVAL;
	if (ipcperms (&shp->shm_perm, shmflg))
		return -EACCES;
	//返回此共享内存段的标识id
	return (unsigned int) shp->shm_perm.seq * SHMMNI + id;
}

/*
 * Only called after testing nattch and SHM_DEST.
 * Here pages, pgtable and shmid_ds are freed.
 */
static void killseg (int id)
{
	struct shmid_ds *shp;
	int i, numpages;

	//shp指向要清除的共享内存对象
	shp = shm_segs[id];
	//如果此共享内存对象处于分配或销毁，或者还未使用的状态
	if (shp == IPC_NOID || shp == IPC_UNUSED) {
		printk ("shm nono: killseg called on unused seg id=%d\n", id);
		return;
	}
	shp->shm_perm.seq++;     /* for shmat */
	shm_seq = (shm_seq+1) % ((unsigned)(1<<31)/SHMMNI); /* increment, but avoid overflow */
	//将对应的shm_segs数组项设置为未使用的状态
	shm_segs[id] = (struct shmid_ds *) IPC_UNUSED;
	//减少系统中处于使用状态的共享内存段个数
	used_segs--;
	//更新max_shmid的值，使之总是代表有效she_segs项的最大下标值
	if (id == max_shmid)
		while (max_shmid && (shm_segs[--max_shmid] == IPC_UNUSED));
	if (!shp->shm_pages) {
		printk ("shm nono: killseg shp->pages=NULL. id=%d\n", id);
		return;
	}
	//下面的一段代码完全模拟了进程页表项的操作内容，精湛！
	numpages = shp->shm_npages;
	//遍历所有的“页表项”，每一页物理内存都有与之对应的一项“页表项”
	for (i = 0; i < numpages ; i++) {
		pte_t pte;
		//取当前“页表项”的内容
		pte_val(pte) = shp->shm_pages[i];
		//如果“页表项”内容为空，即代表没有对应的物理内存，也不存在交换页面
		if (pte_none(pte))
			continue;
		//如果“页表项”说明对应的物理内存在内存中，则将其释放
		if (pte_present(pte)) {
			free_page (pte_page(pte));
			//减少系统中共享内存所占物理内存总页数
			shm_rss--;
		//否则，对应的物理页面不再内存中，而是交换到磁盘上去了
		} else {
			//调用swap_free()将其释放
			swap_free(pte_val(pte));
			//减少系统中共享内存所占交换页面总数
			shm_swp--;
		}
	}
	//释放shm_pages
	kfree(shp->shm_pages);
	//减少系统中共享内存总页数
	shm_tot -= numpages;
	//释放shp
	kfree(shp);
	return;
}

//完成对共享内存的控制
int sys_shmctl (int shmid, int cmd, struct shmid_ds *buf)
{
	struct shmid_ds tbuf;
	struct shmid_ds *shp;
	struct ipc_perm *ipcp;
	int id, err;

	if (cmd < 0 || shmid < 0)
		return -EINVAL;
	//改变共享内存的状态，把buf所指的shmid_ds结构中的uid、gid、mode复制到共享内存的shmid_ds结构内
	//之所以把这段代码放这里，可能是因为这段代码可引起进程睡眠 而后面还会通过id取资源标识id
	if (cmd == IPC_SET) {
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_READ, buf, sizeof (*buf));
		if (err)
			return err;
		memcpy_fromfs (&tbuf, buf, sizeof (*buf));
	}

	switch (cmd) { /* replace with proc interface ? */
		case IPC_INFO:
		{
			struct shminfo shminfo;
			if (!buf)
				return -EFAULT;
			shminfo.shmmni = SHMMNI;
			shminfo.shmmax = SHMMAX;
			shminfo.shmmin = SHMMIN;
			shminfo.shmall = SHMALL;
			shminfo.shmseg = SHMSEG;
			err = verify_area (VERIFY_WRITE, buf, sizeof (struct shminfo));
			if (err)
				return err;
			memcpy_tofs (buf, &shminfo, sizeof(struct shminfo));
			return max_shmid;
		}
		case SHM_INFO:
		{
			struct shm_info shm_info;
			if (!buf)
				return -EFAULT;
			err = verify_area (VERIFY_WRITE, buf, sizeof (shm_info));
			if (err)
				return err;
			shm_info.used_ids = used_segs;
			shm_info.shm_rss = shm_rss;
			shm_info.shm_tot = shm_tot;
			shm_info.shm_swp = shm_swp;
			shm_info.swap_attempts = swap_attempts;
			shm_info.swap_successes = swap_successes;
			memcpy_tofs (buf, &shm_info, sizeof(shm_info));
			return max_shmid;
		}
		case SHM_STAT:
			if (!buf)
				return -EFAULT;
			err = verify_area (VERIFY_WRITE, buf, sizeof (*buf));
			if (err)
				return err;
			if (shmid > max_shmid)
				return -EINVAL;
			shp = shm_segs[shmid];
			if (shp == IPC_UNUSED || shp == IPC_NOID)
				return -EINVAL;
			if (ipcperms (&shp->shm_perm, S_IRUGO))
				return -EACCES;
			id = (unsigned int) shp->shm_perm.seq * SHMMNI + shmid;
			tbuf.shm_perm   = shp->shm_perm;
			tbuf.shm_segsz  = shp->shm_segsz;
			tbuf.shm_atime  = shp->shm_atime;
			tbuf.shm_dtime  = shp->shm_dtime;
			tbuf.shm_ctime  = shp->shm_ctime;
			tbuf.shm_cpid   = shp->shm_cpid;
			tbuf.shm_lpid   = shp->shm_lpid;
			tbuf.shm_nattch = shp->shm_nattch;
			memcpy_tofs (buf, &tbuf, sizeof(*buf));
			return id;
	}

	shp = shm_segs[id = (unsigned int) shmid % SHMMNI];
	if (shp == IPC_UNUSED || shp == IPC_NOID)
		return -EINVAL;
	if (shp->shm_perm.seq != (unsigned int) shmid / SHMMNI)
		return -EIDRM;
	ipcp = &shp->shm_perm;

	switch (cmd) {
	case SHM_UNLOCK:
		if (!suser())
			return -EPERM;
		if (!(ipcp->mode & SHM_LOCKED))
			return -EINVAL;
		ipcp->mode &= ~SHM_LOCKED;
		break;
	case SHM_LOCK:
/* Allow superuser to lock segment in memory */
/* Should the pages be faulted in here or leave it to user? */
/* need to determine interaction with current->swappable */
		if (!suser())
			return -EPERM;
		if (ipcp->mode & SHM_LOCKED)
			return -EINVAL;
		ipcp->mode |= SHM_LOCKED;
		break;
	case IPC_STAT:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_WRITE, buf, sizeof (*buf));
		if (err)
			return err;
		tbuf.shm_perm   = shp->shm_perm;
		tbuf.shm_segsz  = shp->shm_segsz;
		tbuf.shm_atime  = shp->shm_atime;
		tbuf.shm_dtime  = shp->shm_dtime;
		tbuf.shm_ctime  = shp->shm_ctime;
		tbuf.shm_cpid   = shp->shm_cpid;
		tbuf.shm_lpid   = shp->shm_lpid;
		tbuf.shm_nattch = shp->shm_nattch;
		memcpy_tofs (buf, &tbuf, sizeof(*buf));
		break;
	//IPC_SET ：对一个共享段来说，从buf 参数中取值设置shmid_ds结构的ipc_perm域的值
	case IPC_SET:
		if (suser() || current->euid == shp->shm_perm.uid ||
		    current->euid == shp->shm_perm.cuid) {
			ipcp->uid = tbuf.shm_perm.uid;
			ipcp->gid = tbuf.shm_perm.gid;
			ipcp->mode = (ipcp->mode & ~S_IRWXUGO)
				| (tbuf.shm_perm.mode & S_IRWXUGO);
			shp->shm_ctime = CURRENT_TIME;
			break;
		}
		return -EPERM;
	//IPC_RMID ：把一个段标记为删除 
/*
	IPC_RMID 命令实际上不从内核删除一个段，而是仅仅把这个段标记为删除，
	实际的删除发生在最后一个进程离开这个共享段时。 
	当一个进程不再需要共享内存段时，它将调用shmdt()系统调用取消这个段
	但是，这并不是从内核真正地删除这个段，而是把相关shmid_ds结构的 shm_nattch域的值减1
	当这个值为0时，内核才从物理上删除这个共享段
*/
	case IPC_RMID:
		if (suser() || current->euid == shp->shm_perm.uid ||
		    current->euid == shp->shm_perm.cuid) {
			shp->shm_perm.mode |= SHM_DEST;
			if (shp->shm_nattch <= 0)
				killseg (id);
			break;
		}
		return -EPERM;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * The per process internal structure for managing segments is
 * `struct vm_area_struct'.
 * A shmat will add to and shmdt will remove from the list.
 * shmd->vm_task	the attacher
 * shmd->vm_start	virt addr of attach, multiple of SHMLBA
 * shmd->vm_end		multiple of SHMLBA
 * shmd->vm_next	next attach for task
 * shmd->vm_next_share	next attach for segment
 * shmd->vm_offset	offset into segment
 * shmd->vm_pte		signature for this attach
 */
//这里提供了共享内存段的特有swapin方法，是因为因为共享内存的页表
//机制和一般的不同。一般情况下，shm_id中记录的其共享内存页表
//和映射了此共享内存的进程相关页表项都指向物理内存页面地址
//但是要将共享内存页面交换出去的时候，就要遍历共享此shm的所有进程
//修改其页表项，令其指向此共享内存段的shm_id中的相关“页表项”
//只有相关的所有进程的相关页都形式上交换了出去，即都指向shm_id的
//页表项的时候，才将此共享内存段的物理页面真正的交换出去，并将其shm_id
//中的页表项指向此交换页面的entry。至此，各个进程才真正指向了交换页面
//参见《The Linux Kernel》内存相关章节
static struct vm_operations_struct shm_vm_ops = {
	shm_open,		/* open */
	shm_close,		/* close */
	NULL,			/* unmap */
	NULL,			/* protect */
	NULL,			/* sync */
	NULL,			/* advise */
	NULL,			/* nopage (done with swapin) */
	NULL,			/* wppage */
	NULL,			/* swapout (hardcoded right now) */
	shm_swap_in		/* swapin */
};

/* Insert shmd into the circular list shp->attaches */
//将指定的虚拟地址空间shmd插入相关的共享内存对象shmid_ds的attaches双向循环链表中
static inline void insert_attach (struct shmid_ds * shp, struct vm_area_struct * shmd)
{
	struct vm_area_struct * attaches;

	//attaches指向共享内存对象的attaches链表头
	//如果链表不为空
	if ((attaches = shp->attaches)) {
		//vm_next_share和vm_prev_share，把有关的vm_area_struct结合成一个共享内存时使用的双向链表
		shmd->vm_next_share = attaches;
		shmd->vm_prev_share = attaches->vm_prev_share;
		shmd->vm_prev_share->vm_next_share = shmd;
		attaches->vm_prev_share = shmd;
	//如果链表为空
	} else
		shp->attaches = shmd->vm_next_share = shmd->vm_prev_share = shmd;
}

/* Remove shmd from circular list shp->attaches */
//从共享内存对象的attaches双向链表移除一个虚拟地址空间shmd
static inline void remove_attach (struct shmid_ds * shp, struct vm_area_struct * shmd)
{
	//如果shmd->vm_next_share == shmd，则说明链表中只有一个元素
	if (shmd->vm_next_share == shmd) {
		if (shp->attaches != shmd) {
			printk("shm_close: shm segment (id=%ld) attach list inconsistent\n",
				(shmd->vm_pte >> SHM_ID_SHIFT) & SHM_ID_MASK);
			printk("shm_close: %d %08lx-%08lx %c%c%c%c %08lx %08lx\n",
				shmd->vm_task->pid, shmd->vm_start, shmd->vm_end,
				shmd->vm_flags & VM_READ ? 'r' : '-',
				shmd->vm_flags & VM_WRITE ? 'w' : '-',
				shmd->vm_flags & VM_EXEC ? 'x' : '-',
				shmd->vm_flags & VM_MAYSHARE ? 's' : 'p',
				shmd->vm_offset, shmd->vm_pte);
		}
		shp->attaches = NULL;
	} else {
		//如果要移除的元素是链表中的第一个元素
		if (shp->attaches == shmd)
			shp->attaches = shmd->vm_next_share;
		shmd->vm_prev_share->vm_next_share = shmd->vm_next_share;
		shmd->vm_next_share->vm_prev_share = shmd->vm_prev_share;
	}
}

/*
 * ensure page tables exist
 * mark page table entries with shm_sgn.
 */
static int shm_map (struct vm_area_struct *shmd)
{
	pgd_t *page_dir;
	pmd_t *page_middle;
	pte_t *page_table;
	unsigned long tmp, shm_sgn;

	/* clear old mappings */
	do_munmap(shmd->vm_start, shmd->vm_end - shmd->vm_start);

	/* add new mapping */
	insert_vm_struct(current, shmd);
	merge_segments(current, shmd->vm_start, shmd->vm_end);

	/* map page range */
	shm_sgn = shmd->vm_pte + ((shmd->vm_offset >> PAGE_SHIFT) << SHM_IDX_SHIFT);
	for (tmp = shmd->vm_start; tmp < shmd->vm_end; tmp += PAGE_SIZE,
	     shm_sgn += (1 << SHM_IDX_SHIFT)) {
		page_dir = pgd_offset(shmd->vm_task,tmp);
		page_middle = pmd_alloc(page_dir,tmp);
		if (!page_middle)
			return -ENOMEM;
		page_table = pte_alloc(page_middle,tmp);
		if (!page_table)
			return -ENOMEM;
		pte_val(*page_table) = shm_sgn;
	}
	invalidate();
	return 0;
}

/*
 * Fix shmaddr, allocate descriptor, map shm, add attach descriptor to lists.
 */
 //连接共享内存标识符为shmid的共享内存，连接成功后把共享内存区对象
 //映射到调用进程的地址空间，随后可像本地空间一样访问
 //shmaddr指定共享内存出现在进程内存地址的什么位置，直接指定为NULL让内核自己决定一个合适的地址位置
 /*
	fork后子进程继承已连接的共享内存地址。exec后该子进程与已连接的共享内存地址自动脱离(detach)。
	进程结束后，已连接的共享内存地址会自动脱离(detach)
 */
int sys_shmat (int shmid, char *shmaddr, int shmflg, ulong *raddr)
{
	struct shmid_ds *shp;
	struct vm_area_struct *shmd;
	int err;
	unsigned int id;
	unsigned long addr;

	if (shmid < 0) {
		/* printk("shmat() -> EINVAL because shmid = %d < 0\n",shmid); */
		return -EINVAL;
	}

	shp = shm_segs[id = (unsigned int) shmid % SHMMNI];
	if (shp == IPC_UNUSED || shp == IPC_NOID) {
		/* printk("shmat() -> EINVAL because shmid = %d is invalid\n",shmid); */
		return -EINVAL;
	}

	//如果参数shmaddr取值为NULL，系统将自动确定共享内存链接到进程空间的首地址。
	if (!(addr = (ulong) shmaddr)) {
		//如果指定了SHM_REMAP标志，则返回-EINVAL
/*
		SHM_REMAP flag may be specified in shmflg to indicate that the mapping of the segment
		should replace any existing mapping in the range starting at shmaddr and continuing 
		for the size of the segment. (Normally an EINVAL error would result if a mapping 	
		already exists in this address range.) In this case, shmaddr must not be NULL
*/
		if (shmflg & SHM_REMAP)
			return -EINVAL;
		//获取一个还未进行映射的虚拟地址空间
		if (!(addr = get_unmapped_area(shp->shm_segsz)))
			return -ENOMEM;
/*
	如果参数shmaddr取值不为NULL且参数shmflg没有指定SHM_RND标志，系统将运用地址shmaddr链接共享内存。
	如果参数shmaddr取值不为NULL且参数shmflg指定了SHM_RND标志位，系统将地址shmaddr对齐后链接共享内存。
	其中选项SHM_RND的意思是取整对齐，常数SHMLBA代表了低边界地址的倍数，公式“shmaddr – (shmaddr % SHMLBA)”
	的意思是将地址shmaddr移动到低边界地址的整数倍上。
*/
	} else if (addr & (SHMLBA-1)) {
		if (shmflg & SHM_RND)
			addr &= ~(SHMLBA-1);       /* round down */
		else
			return -EINVAL;
	}
	//检测映射区域是否和进程的堆栈区有交叉:16384/1024 = 16
	if ((addr > current->mm->start_stack - 16384 - PAGE_SIZE*shp->shm_npages)) {
		/* printk("shmat() -> EINVAL because segment intersects stack\n"); */
		return -EINVAL;
	}
	if (!(shmflg & SHM_REMAP))
		//在不指定要覆盖的情况下，也不允许和当前进程的现有虚拟地址区有交叉
		if ((shmd = find_vma_intersection(current, addr, addr + shp->shm_segsz))) {
			/* printk("shmat() -> EINVAL because the interval [0x%lx,0x%lx) intersects an already mapped interval [0x%lx,0x%lx).\n",
				addr, addr + shp->shm_segsz, shmd->vm_start, shmd->vm_end); */
			return -EINVAL;
		}

	if (ipcperms(&shp->shm_perm, shmflg & SHM_RDONLY ? S_IRUGO : S_IRUGO|S_IWUGO))
		return -EACCES;
	if (shp->shm_perm.seq != (unsigned int) shmid / SHMMNI)
		return -EIDRM;

	shmd = (struct vm_area_struct *) kmalloc (sizeof(*shmd), GFP_KERNEL);
	if (!shmd)
		return -ENOMEM;
	//重新验证这些值，是因为kmalloc()可能导致进程睡眠
	if ((shp != shm_segs[id]) || (shp->shm_perm.seq != (unsigned int) shmid / SHMMNI)) {
		kfree(shmd);
		return -EIDRM;
	}

	//SHM_ID_SHIFT=8：若一个pte表项内容为交换出去的物理内存，则bit14...8代表了共享内存段的id
	shmd->vm_pte = (SHM_SWP_TYPE << 1) | (id << SHM_ID_SHIFT);
	shmd->vm_start = addr;
	shmd->vm_end = addr + shp->shm_npages * PAGE_SIZE;
	shmd->vm_task = current;
	shmd->vm_page_prot = (shmflg & SHM_RDONLY) ? PAGE_READONLY : PAGE_SHARED;
	shmd->vm_flags = VM_SHM | VM_MAYSHARE | VM_SHARED
			 | VM_MAYREAD | VM_MAYEXEC | VM_READ | VM_EXEC
			 | ((shmflg & SHM_RDONLY) ? 0 : VM_MAYWRITE | VM_WRITE);
	//会在insert_attach()中进行设置
	shmd->vm_next_share = shmd->vm_prev_share = NULL;
	shmd->vm_inode = NULL;
	shmd->vm_offset = 0;
	shmd->vm_ops = &shm_vm_ops;

	shp->shm_nattch++;            /* prevent destruction */
	if ((err = shm_map (shmd))) {
		if (--shp->shm_nattch <= 0 && shp->shm_perm.mode & SHM_DEST)
			killseg(id);
		kfree(shmd);
		return err;
	}

	insert_attach(shp,shmd);  /* insert shmd into shp->attaches */

	shp->shm_lpid = current->pid;
	shp->shm_atime = CURRENT_TIME;

	*raddr = addr;
	return 0;
}

/* This is called by fork, once for every shm attach. */
static void shm_open (struct vm_area_struct *shmd)
{
	unsigned int id;
	struct shmid_ds *shp;

	//从虚拟地址空间中对应的共享内存对象的虚拟“页表项”中取共享内存id
	id = (shmd->vm_pte >> SHM_ID_SHIFT) & SHM_ID_MASK;
	//根据id取共享内存对象
	shp = shm_segs[id];
	//如果该共享对象处于未使用的状态
	if (shp == IPC_UNUSED) {
		printk("shm_open: unused id=%d PANIC\n", id);
		return;
	}
	insert_attach(shp,shmd);  /* insert shmd into shp->attaches */
	shp->shm_nattch++;
	shp->shm_atime = CURRENT_TIME;
	shp->shm_lpid = current->pid;
}

/*
 * remove the attach descriptor shmd.
 * free memory for segment if it is marked destroyed.
 * The descriptor has already been removed from the current->mm->mmap list
 * and will later be kfree()d.
 */
static void shm_close (struct vm_area_struct *shmd)
{
	struct shmid_ds *shp;
	int id;

	//取消相应的虚拟地址空间映射 清空相应的页表项
	unmap_page_range (shmd->vm_start, shmd->vm_end - shmd->vm_start);

	/* remove from the list of attaches of the shm segment */
	//取共享内存段id
	id = (shmd->vm_pte >> SHM_ID_SHIFT) & SHM_ID_MASK;
	//取共享内存段对象
	shp = shm_segs[id];
	remove_attach(shp,shmd);  /* remove from shp->attaches */
  	shp->shm_lpid = current->pid;
	shp->shm_dtime = CURRENT_TIME;
	//如果当前共享内存的连接数小于等于0，并且指定了SHM_DEST，则调用killseg()释放共享内存
	if (--shp->shm_nattch <= 0 && shp->shm_perm.mode & SHM_DEST)
		killseg (id);
}

/*
 * detach and kill segment if marked destroyed.
 * The work is done in shm_close.
 */
 //与shmat函数相反，是用来断开与共享内存附加点的地址，禁止本进程访问此片共享内存
int sys_shmdt (char *shmaddr)
{
	struct vm_area_struct *shmd, *shmdnext;

	for (shmd = current->mm->mmap; shmd; shmd = shmdnext) {
		shmdnext = shmd->vm_next;
		if (shmd->vm_ops == &shm_vm_ops
		    && shmd->vm_start - shmd->vm_offset == (ulong) shmaddr)
			do_munmap(shmd->vm_start, shmd->vm_end - shmd->vm_start);
	}
	return 0;
}

/*
 * page not present ... go through shm_pages
 */
static pte_t shm_swap_in(struct vm_area_struct * shmd, unsigned long offset, unsigned long code)
{
	pte_t pte;
	struct shmid_ds *shp;
	unsigned int id, idx;

	//取共享内存段的id
	id = (code >> SHM_ID_SHIFT) & SHM_ID_MASK;
	//检查此id的合法性
	if (id != ((shmd->vm_pte >> SHM_ID_SHIFT) & SHM_ID_MASK)) {
		printk ("shm_swap_in: code id = %d and shmd id = %ld differ\n",
			id, (shmd->vm_pte >> SHM_ID_SHIFT) & SHM_ID_MASK);
		return BAD_PAGE;
	}
	//如果id大于共享内存段数组中当前的最大有效id值
	if (id > max_shmid) {
		printk ("shm_swap_in: id=%d too big. proc mem corrupted\n", id);
		return BAD_PAGE;
	}
	//取此共享内存段对象
	shp = shm_segs[id];
	//如果此共享内存段对象处于未被使用的状态或者正在被分配或销毁
	if (shp == IPC_UNUSED || shp == IPC_NOID) {
		printk ("shm_swap_in: id=%d invalid. Race.\n", id);
		return BAD_PAGE;
	}
	//取要swap_in的页面在共享内存段对象中对应的索引id
	idx = (code >> SHM_IDX_SHIFT) & SHM_IDX_MASK;
	if (idx != (offset >> PAGE_SHIFT)) {
		printk ("shm_swap_in: code idx = %u and shmd idx = %lu differ\n",
			idx, offset >> PAGE_SHIFT);
		return BAD_PAGE;
	}
	//如果idx大于此共享内存段中的最大页面数
	if (idx >= shp->shm_npages) {
		printk ("shm_swap_in : too large page index. id=%d\n", id);
		return BAD_PAGE;
	}

	//取“页表项”
	pte_val(pte) = shp->shm_pages[idx];
	//如果“页表项”对应的物理页面不在内存中
	if (!pte_present(pte)) {
		//申请一页新的物理内存页
		unsigned long page = get_free_page(GFP_KERNEL);
		//如果申请失败
		if (!page) {
			oom(current);
			return BAD_PAGE;
		}
		//再次取“页表项”内容，因为在申请物理页面过程中，进程可能睡眠
		pte_val(pte) = shp->shm_pages[idx];
		if (pte_present(pte)) {
			//如果物理页面已在内存中，释放申请的页面
			free_page (page); /* doesn't sleep */
			goto done;
		}
		//如果“页表项”不为空，即代表对应的物理内存交换出去了
		if (!pte_none(pte)) {
			//将对应的物理内存从交换区中swap_in
			read_swap_page(pte_val(pte), (char *) page);
			//因为读取交换页面可能导致进程睡眠，所以再次取“页表项”内容
			pte_val(pte) = shp->shm_pages[idx];
			//如果对应的物理页面已在内存，释放申请的物理页面
			if (pte_present(pte))  {
				free_page (page); /* doesn't sleep */
				goto done;
			}
			//释放交换页面
			swap_free(pte_val(pte));
			//系统中共享内存所对应的交换页面数减一
			shm_swp--;
		}
		//增加系统中共享内存所占用的物理页面数
		shm_rss++;
		//进行相应的映射
		pte = pte_mkdirty(mk_pte(page, PAGE_SHARED));
		shp->shm_pages[idx] = pte_val(pte);
	} else
		--current->mm->maj_flt;  /* was incremented in do_no_page */

done:	/* pte_val(pte) == shp->shm_pages[idx] */
	current->mm->min_flt++;
	mem_map[MAP_NR(pte_page(pte))]++;
	return pte_modify(pte, shmd->vm_page_prot);
}

/*
 * Goes through counter = (shm_rss << prio) present shm pages.
 */
static unsigned long swap_id = 0; /* currently being swapped */
static unsigned long swap_idx = 0; /* next to swap */

//将共享内存页面交换到交换设备上去，在Swap.c中的try_to_free_page()中被调用
/*
	NOTE：函数内对“死机”Bug猜想是对的，在Linux1.3.2内核版本中可以证实，内核修订了此bug
*/
int shm_swap (int prio)
{
	pte_t page;
	struct shmid_ds *shp;
	struct vm_area_struct *shmd;
	unsigned int swap_nr;
	unsigned long id, idx, invalid = 0;
	int counter;

	counter = shm_rss >> prio;
	//如果counter值为0，或者获取交换设备页面失败，则返回0，表示无法
	//将共享内存占用的页面交换出去，从这里看出，共享内存占用的页面数越多
	//prio的值越小，就越有可能发生交换
	if (!counter || !(swap_nr = get_swap_page()))
		return 0;
//这里是要找出系统中可以被交换出去的共享内存段
 check_id:
	//shm_segs数组是系统中共享内存对象数组，每个数组项都代表系统中的一个共享内存段
	shp = shm_segs[swap_id];
	//如果当前共享内存段处于未使用的状态或者正在分配或销毁，或者被锁定
	if (shp == IPC_UNUSED || shp == IPC_NOID || shp->shm_perm.mode & SHM_LOCKED ) {
		//将共享内存段的“页表项”索引设置为0
		swap_idx = 0;
		//如果swap_id自增后大于当前数组中最大有效项的index，则将其设为0，重新遍历
		//如果当前系统中的所有共享内存段都被锁定，那么，不就“死机”了？
		if (++swap_id > max_shmid)
			swap_id = 0;
		goto check_id;
	}
	
	//执行到这里，说明找到一个合适的共享内存段可以被交换出去，则记录其id
	id = swap_id;

//这里是要寻找到当前共享内存段的一个有效的“页表项”，即可以被交换出去的物理页面
 check_table:
	idx = swap_idx++;
	//如果遍历完了此共享内存段的页面数，即没有找到一个合适的物理内存页面可以释放
	if (idx >= shp->shm_npages) {
		//将swap_idx设为0，以便下次从“页面”0开始尝试释放页面
		swap_idx = 0;
		//如果swap_id自增之后大于最大的有效共享内存段id，则将swap_id置为0
		//以便再从0开始遍历共享内存段数组
		if (++swap_id > max_shmid)
			swap_id = 0;
		goto check_id;
	}

	//取此物理页面的“页表项”
	pte_val(page) = shp->shm_pages[idx];
	//如果此页面不在内存中
	if (!pte_present(page))
		//重新寻找“页表项”
		//如果都不在内存中，不就又“死机”了？
		goto check_table;
	swap_attempts++;

	//执行到这里，说明内核找到一个共享内存段中的一页可以交换到交换设备中去的、
	//并且存在于物理内存中的物理页面
	
	//counter值记录了尝试释放一页物理内存的失败次数
	if (--counter < 0) { /* failed */
		if (invalid)
			invalidate();
		//释放申请的交换页面
		swap_free (swap_nr);
		return 0;
	}
	//如果有进程的虚拟地址空间连接（映射）到了此共享内存
	//则要遍历这些进程的相应虚拟地址空间，释放相应的页表项
	if (shp->attaches)
	  //遍历各个进程对应的虚拟地址空间
	  for (shmd = shp->attaches; ; ) {
	    do {
		pgd_t *page_dir;
		pmd_t *page_middle;
		pte_t *page_table, pte;
		unsigned long tmp;

		//检测当前虚拟地址空间的合法性，因为vma->vm_pte中保存了共享内存段的id
		//若不合法，则进行下一个相关进程的处理，即continue对for循环起效
		if ((shmd->vm_pte >> SHM_ID_SHIFT & SHM_ID_MASK) != id) {
			printk ("shm_swap: id=%ld does not match shmd->vm_pte.id=%ld\n", id, shmd->vm_pte >> SHM_ID_SHIFT & SHM_ID_MASK);
			continue;
		}
		tmp = shmd->vm_start + (idx << PAGE_SHIFT) - shmd->vm_offset;
		if (!(tmp >= shmd->vm_start && tmp < shmd->vm_end))
			continue;
		//取连接进程的页目录表项
		page_dir = pgd_offset(shmd->vm_task,tmp);
		if (pgd_none(*page_dir) || pgd_bad(*page_dir)) {
			//进程的页目录表应该常驻内存
			printk("shm_swap: bad pgtbl! id=%ld start=%lx idx=%ld\n",
					id, shmd->vm_start, idx);
			pgd_clear(page_dir);
			continue;
		}
		//取进程的页中间目录项
		page_middle = pmd_offset(page_dir,tmp);
		if (pmd_none(*page_middle) || pmd_bad(*page_middle)) {
			printk("shm_swap: bad pgmid! id=%ld start=%lx idx=%ld\n",
					id, shmd->vm_start, idx);
			pmd_clear(page_middle);
			continue;
		}
		//取进程的页表项
		page_table = pte_offset(page_middle,tmp);
		pte = *page_table;
		//如果相应的页表项不在内存，直接遍历下一个页表项
		//这种情况可能出现的原因是，进程虽然映射了共享内存段
		//但只是做了虚拟映射，还并没有做真正的页表映射
		//只有当进程访问到相应的表项，产生缺页中断，才会做真正的页表映射
		if (!pte_present(pte))
			continue;
		//如果页表项是“年轻的”，即表示页表刚被访问不久，之后还可能被访问
		//则将页表项设置为old，则下次可能会被交换出去。如果之后页表又被访问，
		//则又被置为young
		if (pte_young(pte)) {
			*page_table = pte_mkold(pte);
			continue;
		}
		//如果进程的页表项不等于共享内存段中保存的相应的物理页面的地址
		if (pte_page(pte) != pte_page(page))
			printk("shm_swap_out: page and pte mismatch\n");
		//修改相应进程的页表项
		pte_val(*page_table) = shmd->vm_pte | idx << SHM_IDX_SHIFT;
		//减少相应物理内存页面的引用计数
		mem_map[MAP_NR(pte_page(pte))]--;
		//减少相应进程相的物理内存页面数
		if (shmd->vm_task->mm->rss > 0)
			shmd->vm_task->mm->rss--;
		//表示可以刷新TLB
		invalid++;
	    /* continue looping through circular list */
	    } while (0);
	    if ((shmd = shmd->vm_next_share) == shp->attaches)
		break;
	}

	//如果此页未被完全释放，则返回重新挑选合适的页面
	//从这里也可以看出，本函数只是试图释放一页物理内存
	//这里，页面引用数为1，表示只有共享内存段占用此页面而没有其他进程引用
	//这种情况出现的一个原因可能是，某些进程刚刚访问相应的共享内存段中相应的
	//页面不久，之后可能还要访问，这样的页面不要释放。这种情况对应上面的代码
	//中设置页表项为old的语句
	if (mem_map[MAP_NR(pte_page(page))] != 1)
		goto check_table;
	//将共享内存段的相应“页表项”设置为交换页面号
	shp->shm_pages[idx] = swap_nr;
	if (invalid)
		invalidate();
	//将物理页面写到交换设备中去
	write_swap_page (swap_nr, (char *) pte_page(page));
	//释放此物理内存
	free_page(pte_page(page));
	//增加系统成功交换页面的次数
	swap_successes++;
	//增加系统中共享内存占用的交换页面数
	shm_swp++;
	//减少系统中共享内存端占用的物理内存页面数
	shm_rss--;
	return 1;
}
