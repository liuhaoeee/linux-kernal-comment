/*
 *  linux/fs/block_dev.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
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

//本文件主要包含了对块设备文件的读写函数block_write和block_read
//注意理解块设备文件盒普通文件的区别 一旦理解其区别，那么本文件还是很好理解的
//参见我的博客：http://blog.csdn.net/yihaolovem/article/details/39118555
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/locks.h>
#include <linux/fcntl.h>
#include <linux/mm.h>

#include <asm/segment.h>
#include <asm/system.h>

extern int *blk_size[];
extern int *blksize_size[];

#define MAX_BUF_PER_PAGE (PAGE_SIZE / 512)
#define NBUF 64

/*
*	块写函数，就是将buf中的数据写入到文件中(就是文件代表的块设
*	备了)，节点inode中就包含了设备号了而且这里是块设备文件，是
*	肯定存在设备的。这里的写入是buf数据，只要写入到缓冲区中就可
*	以了，具体底层何时写入就是底层设备的问题了
*
*	注意这个函数同早期函数的参数的区别，这里第一个参数提供的是
*	inode，而早期的提供的直接是dev设备号
*/
//通用函数block_read, block_write及block_fsync被用来代替任何针对某个驱动程序的函数
int block_write(struct inode * inode, struct file * filp, char * buf, int count)
{
	int blocksize, blocksize_bits, i, j, buffercount,write_error;
	int block, blocks;
	loff_t offset;
	int chars;
	int written = 0;
	int cluster_list[MAX_BUF_PER_PAGE];
	struct buffer_head * bhlist[NBUF];
	int blocks_per_cluster;
	unsigned int size;
	unsigned int dev;
	struct buffer_head * bh, *bufferlist[NBUF];
	register char * p;
	int excess;

	write_error = buffercount = 0;
	dev = inode->i_rdev;
	//如果此块设备是只读的，返回
	if ( is_read_only( inode->i_rdev ))
		return -EPERM;
	//设置默认的块大小，如果可以查询出设备号，就按需设置
	blocksize = BLOCK_SIZE;
	//blksize_size数组中存储的是以字节为单位，块设备的块大小，定义于\drivers\block\ll_rw_blk.c
	if (blksize_size[MAJOR(dev)] && blksize_size[MAJOR(dev)][MINOR(dev)])
		blocksize = blksize_size[MAJOR(dev)][MINOR(dev)];

	i = blocksize;
	blocksize_bits = 0;
	//这个算法还是蛮有用的，将实际的数值转换为比特位的索引
    //注意的是块大小一般是2^，所以是可以得到准确值的
	while(i != 1) {
		blocksize_bits++;
		i >>= 1;
	}

	blocks_per_cluster = PAGE_SIZE / blocksize;	//内存中的一页物理内存所能包含的块数

	//这里是对文件指针的操作，从文件指针获得所在的文件块号
    //和在块中的偏移位置
    //记住：移位的效率永远要比除法要高的!!
	block = filp->f_pos >> blocksize_bits;	//文件指针指向的当前块所在文件的块号（块设备文件不同于普通文件，其数据在设备上是连续的）
	offset = filp->f_pos & (blocksize-1);	//在块内的偏移位置

	if (blk_size[MAJOR(dev)])
		//blk_size数组可以得到块设备以KB为单位的长度
		//blk_size数组中存储的是以KB为单位的块设备的大小，注意是设备大小，不是块大小，定义于\drivers\block\ll_rw_blk.c
		//BLOCK_SIZE_BITS=10,左移10位得到以字节为单位的设备大小，右移blocksize_bits则得到设备最大块号
		size = ((loff_t) blk_size[MAJOR(dev)][MINOR(dev)] << BLOCK_SIZE_BITS) >> blocksize_bits;
	else
		size = INT_MAX;		//7fffffff 2G???
	//进入写大循环
	while (count>0) {
		if (block >= size)	//写入的块号是否超过了块设备最大的块号
			return written;
		chars = blocksize - offset;	//本文件块中还可以写入的数据的长度，当然一次循环之后offset就是0了
		if (chars > count)
			chars=count;
//一下这两个编译选项，主要决定了两种不同的预读方式，但最终都要产生一个包含有当前要写的块的有效数据的buffer，bh
#if 0
	//预读三块
		if (chars == blocksize)
			//这里是查找制定的缓冲块，如果不存在就分配一块新的（不会含有有效数据的新buffer块）
			bh = getblk(dev, block, blocksize);
		else
			//否则预读取三块，这里的预读可能会在下次循环中被上面的getblk检索到
			bh = breada(dev,block,block+1,block+2,-1);

#else
	//也是预读
		for(i=0; i<blocks_per_cluster; i++) 
			cluster_list[i] = block+i;
		if((block % blocks_per_cluster) == 0)
			generate_cluster(dev, cluster_list, blocksize);
		bh = getblk(dev, block, blocksize);

		//如果要写入当前buffer块的字符数不是一块
		//必然就是小于blocksize了,有两种情况：1.要读写的第一个块，2.要读写的最后一个块
		//并且当前要写的buffer中的数据无效
		if (chars != blocksize && !bh->b_uptodate) {
		//如果文件的预读标志为真，则说明上次已经预读过此块
		//如果read_ahead[MAJOR(dev)]==0，说明设备不可预读？
		  if(!filp->f_reada ||!read_ahead[MAJOR(dev)]) {
		    /* We do this to force the read of a single buffer */
		    brelse(bh);
			//读取指定的一块
		    bh = bread(dev,block,blocksize);
		  } else {	//否则就需要预读几块了
		    /* Read-ahead before write */
		    blocks = read_ahead[MAJOR(dev)] / (blocksize >> 9) / 2;
		    if (block + blocks > size) 
				blocks = size - block;
		    if (blocks > NBUF)
				blocks=NBUF;
		    excess = (block + blocks) % blocks_per_cluster;
		    if ( blocks > excess )
				blocks -= excess;
		    bhlist[0] = bh;	//从当前操作的buffer块开始预读
			//产生将要预读的块的buffer并且将其指针存储到bhlist数组中
		    for(i=1; i<blocks; i++){
		      if(((i+block) % blocks_per_cluster) == 0) {
				for(j=0; j<blocks_per_cluster; j++)
					cluster_list[j] = block+i+j;
				generate_cluster(dev, cluster_list, blocksize);
		      };
		      bhlist[i] = getblk(dev, block+i, blocksize);
		      if(!bhlist[i]){
				while(i >= 0) 
					brelse(bhlist[i--]);
				return written? written: -EIO;
		      };
		    };
			//将bhlist数组中的buffer从设备读入
		    ll_rw_block(READ, blocks, bhlist);
			//释放buffer head结构
		    for(i=1; i<blocks; i++) 
				brelse(bhlist[i]);
		    wait_on_buffer(bh);
		  };
		};
#endif
		//块号自增
		block++;
		//如果失败就返回先前读取的数据长度，如果一个都没有写入就返回错误
		//一个我认为比较重要的问题，就是内核定期将缓冲区中的数据写人到设备
		//并且还定期释放缓冲块及其缓冲头结构，那么就会有一个问题，在上面预读函数或者说
		//当内核将数据从设备读入缓冲块中，但随后释放缓冲头结构，也就是释放了缓冲区
		//当然，此时数据还是有效的存在于缓冲块中，但是这样的缓冲块具备可以被释放的条件
		//一旦内核将其释放，那么bh就是空了，这也算是写错误吗？释放缓冲块及缓冲头参见
		//buffer.c中的try_to_free函数
		if (!bh)
			return written?written:-EIO;
		p = offset + bh->b_data;	//p指向要写入的位置
		//这个offset = 0还是很有讲究的，如果一次就能写完(存在的情
		//况就是写入的数据再加上偏移没有满一块或者刚好一块)，一次
		//就执行完了，这里没有用了，否则就是刚好前面填充了一块，下
		//面的写就全部是整块写入了
		offset = 0;
		filp->f_pos += chars;	//增加文件指针
		written += chars;	//增加写入文件的字符个数
		count -= chars;	//减少要写入的问价字符数
		memcpy_fromfs(p,buf,chars);	//将buf指向的缓冲区数据复制chars个字节到p指向的缓冲区中
		p += chars;	//p指向下次要复制的缓冲区处
		buf += chars;	//buf指向下次要复制的缓冲区处
		bh->b_uptodate = 1;	//标识此缓冲区中包含有效数据
		mark_buffer_dirty(bh, 0);	//将此缓冲区设为脏，表明其需要写入磁盘
		//The file is opened for synchronous I/O. Any write ()s on the resulting file descriptor
		//will block the calling process until the data has been physically written to the underlying hardware.
		if (filp->f_flags & O_SYNC)
			bufferlist[buffercount++] = bh;
		else
			brelse(bh);
		//如果bufferlist数组已满，则调用底层函数，将缓冲区中的数据写入磁盘
		//buffercount数组是函数内声明的数组
		if (buffercount == NBUF){
			ll_rw_block(WRITE, buffercount, bufferlist);
			//遍历数组，释放缓冲区结构（因为已经将缓冲块写入设备）
			for(i=0; i<buffercount; i++){
				//等待缓冲区解锁
				wait_on_buffer(bufferlist[i]);
				//如果缓冲区中包含的不是有效数据，则将write_error设为1
				if (!bufferlist[i]->b_uptodate)
					write_error=1;
				//释放缓冲区
				brelse(bufferlist[i]);
			}
			buffercount=0;
		}
		if(write_error)
			break;
	}
	if ( buffercount ){
		ll_rw_block(WRITE, buffercount, bufferlist);
		for(i=0; i<buffercount; i++){
			wait_on_buffer(bufferlist[i]);
			if (!bufferlist[i]->b_uptodate)
				write_error=1;
			brelse(bufferlist[i]);
		}
	}		
	filp->f_reada = 1;
	if(write_error)
		return -EIO;
	return written;
}

int block_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	unsigned int block;
	loff_t offset;
	int blocksize;
	int blocksize_bits, i;
	unsigned int blocks, rblocks, left;
	int bhrequest, uptodate;
	int cluster_list[MAX_BUF_PER_PAGE];
	int blocks_per_cluster;
	struct buffer_head ** bhb, ** bhe;	//buffer begin，buffer end，用于指向下面的buflist数组
	//记录要读取或者说已经含有有效数据的buffer块指针的数组，当其buffer中不含有有效数据时，
	//会利用下面的bhreq数组，记录下不含有有效数据的buffer块，然后请求底层函数读取磁盘设备块
	//这就在很大程度上保证了buflist中的buffer块总是含有要读取的有效数据，除非底层产生了读错误
	struct buffer_head * buflist[NBUF];	
	//请求缓冲区结构数组，记录要请求磁盘设备读入的块缓冲信息，上面的bhrequest是此数组下标变量
	struct buffer_head * bhreq[NBUF];	
	unsigned int chars;
	loff_t size;
	unsigned int dev;
	int read;
	int excess;

	dev = inode->i_rdev;
	//先设置默认的块大小
	blocksize = BLOCK_SIZE;	//BLOCK_SIZE=1024
	//blksize_size数组中记录了块设备以字节为单位的块大小，如果能查询出此设备的块大小，
	//则重新设置blocksize
	if (blksize_size[MAJOR(dev)] && blksize_size[MAJOR(dev)][MINOR(dev)])
		blocksize = blksize_size[MAJOR(dev)][MINOR(dev)];
	i = blocksize;
	blocksize_bits = 0;
	while (i != 1) {
		blocksize_bits++;
		i >>= 1;
	}

	offset = filp->f_pos;	//注意这里的offset首先被设置成了文件指针的位置了！！！
	//blk_size数组中记录了设备以KB为单位的大小，主要是设备大小，而不是块大小
	if (blk_size[MAJOR(dev)])
		size = (loff_t) blk_size[MAJOR(dev)][MINOR(dev)] << BLOCK_SIZE_BITS;	//size是块设备以字节为单位的大小
	else
		size = INT_MAX;

	blocks_per_cluster = PAGE_SIZE / blocksize;	//一页物理内存总能容纳的最大块数

	if (offset > size)	//如果文件指针超过了块设备的最大值
		left = 0;
	else
		left = size - offset;	//文件可以读取的数据
	if (left > count)	//如果可以读取的数据大于要读取的数据
		left = count;
	if (left <= 0)	//如果要读取的数据不大于0，则直接返回
		return 0;
	read = 0;
	block = offset >> blocksize_bits;	//取得要读取的第一个设备文件块
	offset &= blocksize-1;		//offset &= (blocksize-1);???在上面取得的文件块内的偏移量
	size >>= blocksize_bits;	//块设备的最大文件块号
	rblocks = blocks = (left + offset + blocksize - 1) >> blocksize_bits;	//最终需要读取的块号数量
	bhb = bhe = buflist;	//初始化bhb和bhe
	//如果文件有预读标志，程序现在还未进入读循环，那么可以看出，文件预读标志的作用在于，
	//标志文件之前执行过读取操作或者预读过，那么就要进行下面的
	if (filp->f_reada) {	
		//read_ahead是定义于ll_rw_blk.c中的一个全局变量：int read_ahead[MAX_BLKDEV] = {0, };
		//It specifies how many sectors to read ahead on the disk.  
		//blocksize >> 9 得到块设备的一块对应磁盘上一块（扇区）（512B）的数，比如当然默认块大小为1024（1K）
		//则blocksize >> 9得到2
		//read_ahead[MAJOR(dev)] / (blocksize >> 9)得到此块设备可以预读的最大块数（不是扇区数）
		//如果要读取的块数小于可以预读的块数，则将blocks设为可以预读的最大块数
		//这可能是注重了效率和提高命中率，既然一样的效率，那么为什么不读取最多的块呢？
		if (blocks < read_ahead[MAJOR(dev)] / (blocksize >> 9))
			blocks = read_ahead[MAJOR(dev)] / (blocksize >> 9);	//从这里可以看出blocks是要预读的块数，而rblocks是要读取的总块数
		//excess:超过;超额量;额外的 block:本次要读的块 blocks:本次要预读的块数 blocks_per_cluster:一页物理内存所能容纳的块数
		//excess:块数按页对齐之后，所剩余的块数，也就是不足以占用一页物理内存的块数
		//block + blocks 这个很有技巧，得到了从块号0开始的块号，这也保证了取模运算后，excess可能比block的值大，也可能小，也可能相等
		//比如，当block与一页物理内存的开始处对齐时，并且预读的块数不超过blocks_per_cluster时 blocks == excess...以此类推
		excess = (block + blocks) % blocks_per_cluster;
		//我认为这个判断的意义在于，当预读时若读取进内存的块数超过一个页面时，但又有剩余的块最终不能占满一页内存时，舍弃之
		if ( blocks > excess )
			blocks -= excess;
		//若预读的块数小于实际要读的块数
		if (rblocks > blocks)
			blocks = rblocks;
		
	}
	//如果预读之后的文件块大于总的文件块
	if (block + blocks > size)
		blocks = size - block;

	/* We do this in a two stage process.  We first try and request
	   as many blocks as we can, then we wait for the first one to
	   complete, and then we try and wrap up as many as are actually
	   done.  This routine is rather generic, in that it can be used
	   in a filesystem by substituting the appropriate function in
	   for getblk.

	   This routine is optimized to make maximum use of the various
	   buffers and caches. */
	//进入读取大循环
	do {
		bhrequest = 0;
		uptodate = 1;
		//blocks是要读取的块数 block是本次要读取的块号
		//此while循环是要做一些尽可能做的预读操作，从而使buflist中尽可能多的含有有效buffer数据块
		while (blocks) {
			--blocks;
#if 1
			if((block % blocks_per_cluster) == 0) {
			  for(i=0; i<blocks_per_cluster; i++) 
				cluster_list[i] = block+i;
			//generate_cluster()函数的作用在于，产生了一簇（一页物理内存大小）的buffer
			//此页面内的buffer块号是连续的，这也为下面的 getblk函数提供了buffer块
			//由于generate_cluster函数产生的是一页块号连续的buffer，所以上面的判断需要(block % blocks_per_cluster) == 0
			//将块号连续的块放在同一个页面，可能效率要高？
			  generate_cluster(dev, cluster_list, blocksize);
			}
#endif
			//bhb是指向buffer指针的指针，这里用来将buflist数组中buffer指针指向一个buffer
			//getblk函数总是能返回一个缓冲块，但如果当前缓冲区中没有要获取的指定的缓冲块，
			//则返回一个不含有有效数据的缓冲块
			*bhb = getblk(dev, block++, blocksize);	
			if (*bhb && !(*bhb)->b_uptodate) {	//不是有效数据，需要重新从磁盘设备读取
				uptodate = 0;	//这个标志表示下面需要读取了
				bhreq[bhrequest++] = *bhb;	//记录要重新读取的缓冲块头结构请求信息
			}

			if (++bhb == &buflist[NBUF])
				bhb = buflist;	//到数组的结尾了，回卷到头部

			/* If the block we have on hand is uptodate, go ahead
			   and complete processing. */
			//说明本次读取的块缓冲中的数据是有效的
			if (uptodate)
				break;
			if (bhb == bhe)
				break;
		}

		/* Now request them all */
		//如果bhrequest不为0，则说明有需要从磁盘读入的请求数据块
		if (bhrequest) {
			//调用底层块读写函数，从而在一定程度上保证了buflist数组中总是含有具有有效数据的缓冲块
			ll_rw_block(READ, bhrequest, bhreq);
			//因为读入块后可能占用buffer head结构过多，从而可用buffer head结构过少，所以需要扩充
			refill_freelist(blocksize);
		}

		//此循环的工作是将buflist中含有的有效buffer数据块真正的读入buf中去
		//只能赞叹，内核用了两个数组，三个while循环，将事情做的是多么的精巧！
		do { /* Finish off（结束） all I/O that has actually completed */
			if (*bhe) {
				wait_on_buffer(*bhe);
				//如果缓冲区中的数据是无效的，说明产生了读错误（底层读写函数ll_rw_block产生的）
				if (!(*bhe)->b_uptodate) {	/* read error? */
				        brelse(*bhe);	//释放此缓冲区头结构
					if (++bhe == &buflist[NBUF])	//如果bhe到达了数组末尾，则回卷到头部
					  bhe = buflist;
					left = 0;	//将要读取的字节数设为0（从而也终止外层while大循环）
					break;	//并且退出循环
				}
			}			
			if (left < blocksize - offset)
				chars = left;
			else
				chars = blocksize - offset;
			filp->f_pos += chars;
			left -= chars;
			read += chars;
			//执行到这里，如果*bhe不为空，则其中含有的是有效数据
			if (*bhe) {
				memcpy_tofs(buf,offset+(*bhe)->b_data,chars);
				brelse(*bhe);	//已经将数据读入用户空间buf，所以要释放buffer
				buf += chars;
			//否则，将0复制到用户空间中去
			} else {
				while (chars-->0)
					put_fs_byte(0,buf++);
			}
			offset = 0;
			if (++bhe == &buflist[NBUF])
				bhe = buflist;
		//bhe != bhb：说明buflist数组中还有需要读入buf的缓冲块
		//left > 0：说明还没有全部读入要读取的字节数
		//(!*bhe || !(*bhe)->b_lock)：说明要读取的缓冲块没有加锁
		} while (left > 0 && bhe != bhb && (!*bhe || !(*bhe)->b_lock));
	} while (left > 0);

/* Release the read-ahead blocks */
	//释放本函数中用到的缓冲区块，因为bhrequest数组只是用来辅助当buflist数组中的一项buffer
	//中的数据无效时，用来向请求底层读取所需的数据块的，bhrequest数组中引用的指针和buflist
	//中引用的指针是一样的，都是指向同一个缓冲块（当当前buflist中的buffer不含有有效数据时）
	//所以，只要释放buflist数组中的缓冲块就可以了
	while (bhe != bhb) {
		brelse(*bhe);
		if (++bhe == &buflist[NBUF])
			bhe = buflist;
	};
	if (!read)
		return -EIO;
	filp->f_reada = 1;
	return read;
}

int block_fsync(struct inode *inode, struct file *filp)
{
	return fsync_dev (inode->i_rdev);
}
