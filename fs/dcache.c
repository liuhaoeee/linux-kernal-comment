/*
 *  linux/fs/dcache.c
 *
 *  (C) Copyright 1994 Linus Torvalds
 */
 
 //Linux操作系统内核就是这样 曾经风云变幻 到最后却被千万行的代码淹没的无影无踪  
//幸好 他有时又会留下一点蛛丝马迹 引领我们穿越操作系统内部所有的秘密
/*
	The PDF documents about understanding Linux Kernel 1.2 is made by Liu Yihao
	这份学习Linux内核的PDF文档由刘以浩同学整理完成
	包含了作者在学习内核过程中搜集到的大量有关Linux
	内核的知识和自己的一些理解以及注释
	当然，最宝贵的还是这份原生的内核代码
	NOTE：如果在阅读过程中遇到什么问题，欢迎和我交流讨论
	Email：liuyihaolovem@163.com
*/

/*
						目录项对象
	引入目录项的概念主要是出于方便查找文件的目的。一个路径的各个组成部分，不管是目录还是
	普通的文件，都是一个目录项对象。如，在路径/home/source/test.c中，目录 /, home, source
	和文件 test.c都对应一个目录项对象。不同于前面的两个对象，目录项对象没有对应的磁盘数据结构，
	VFS在遍 历路径名的过程中现场将它们逐个地解析成目录项对象。
*/

/*
 * The directory cache is a "two-level" cache, each level doing LRU on
 * its entries.  Adding new entries puts them at the end of the LRU
 * queue on the first-level cache, while the second-level cache is
 * fed by any cache hits.
 *
 * The idea is that new additions (from readdir(), for example) will not
 * flush the cache of entries that have really been used.
 *
 * There is a global hash-table over both caches that hashes the entries
 * based on the directory inode number and device as well as on a
 * string-hash computed over the name. 
 */

#include <stddef.h>

#include <linux/fs.h>
#include <linux/string.h>

/*
 * Don't bother caching long names.. They just take up space in the cache, and
 * for a name cache you just want to cache the "normal" names anyway which tend
 * to be short.
 */
 //只有短的目录条目（最多 15 字符）被缓存，不过这是合理的，因为较短的目录名称是最常用的。
 //例如：当 X 服务器启动的时候，/usr/X11R6/bin 非常频繁地被访问。
#define DCACHE_NAME_LEN	15
#define DCACHE_SIZE 128

struct hash_list {
	struct dir_cache_entry * next;
	struct dir_cache_entry * prev;
};

/*
 * The dir_cache_entry must be in this order: we do ugly things with the pointers
 */
 /*
	和超级块和索引节点不同，目录项并不是实际存在于磁盘上的。
	在使用的时候在内存中创建目录项对象，其实通过索引节点已经可以定位到指定的文件，
	但是索引节点对象的属性非常多，在查找，比较文件时，直接用索引节点效率不高，所以引入了目录项的概念。
 */
struct dir_cache_entry {
	struct hash_list h;	//hash_list结构 注意，不是指针 一个相当于将本结构中的next_lru和prev_lru指针封装在一起的一个数据结构
	unsigned long dev;
	unsigned long dir;
	unsigned long version;
	unsigned long ino;
	unsigned char name_len;
	char name[DCACHE_NAME_LEN];
	struct dir_cache_entry ** lru_head;	//指向lru链表（level1_head、level2_head）头指针的指针 二级指针 参见update_lru()函数
	struct dir_cache_entry * next_lru,  * prev_lru;		//形成lru链表
};

#define COPYDATA(de, newde) \
memcpy((void *) &newde->dev, (void *) &de->dev, \
4*sizeof(unsigned long) + 1 + DCACHE_NAME_LEN)

//LRU数组队列
static struct dir_cache_entry level1_cache[DCACHE_SIZE];
static struct dir_cache_entry level2_cache[DCACHE_SIZE];

/*
 * The LRU-lists are doubly-linked circular lists, and do not change in size
 * so these pointers always have something to point to (after _init)
 */
 /*
	为了保持这些 cache 有效和最新，VFS 保存了一个最近最少使用（LRU）目录缓存条目的列表。
	当一个目录条目第一次被放到了缓存，就是当它第一次被查找的时候，它被加到了第一级 LRU 
	列表的最后。对于充满的 cache，这会移去 LRU 列表前面存在的条目。当这个目录条目再一次
	被访问的时候，它被移到了第二个 LRU cache 列表的最后。同样，这一次它移去了第二级 LRU cache 
	列表前面的二级缓存目录条目。这样从一级和二级 LRU 列表中移去目录条目是没有问题的。这些条目之
	所以在列表的前面只是因为它们最近没有被访问。如果被访问，它们会在列表的最后。在二级 LRU
	缓存列表中的条目比在一级 LRU 缓存列表中的条目更加安全。因为这些条目不仅被查找而且曾经重复引用。
 */
static struct dir_cache_entry * level1_head;	//level1_cache数组队列头指针
static struct dir_cache_entry * level2_head;	//level2_cache数组队列头指针

/*
 * The hash-queues are also doubly-linked circular lists, but the head is
 * itself on the doubly-linked list, not just a pointer to the first entry.
 */
#define DCACHE_HASH_QUEUES 19
#define hash_fn(dev,dir,namehash) (((dev) ^ (dir) ^ (namehash)) % DCACHE_HASH_QUEUES)

//目录缓存中包含一个 hash_table，每一个条目都指向一个具有相同的 hash value 的目录缓存条目的列表。
//Hash 函数使用存放这个文件系统的设备的设备编号和目录的名称来计算在 hash_table 中的偏移量或索引。
//它允许快速找到缓存的目录条目。如果一个缓存在查找的时候花费时间太长，或根本找不到，这样的缓存是没有用的。
static struct hash_list hash_table[DCACHE_HASH_QUEUES];

static inline void remove_lru(struct dir_cache_entry * de)
{
	de->next_lru->prev_lru = de->prev_lru;
	de->prev_lru->next_lru = de->next_lru;
}

//将de链入LRU尾部 head是LRU头指针，在函数中并没有改变head的值，所以是将de链入了尾部，即head之前
static inline void add_lru(struct dir_cache_entry * de, struct dir_cache_entry *head)
{
	de->next_lru = head;
	de->prev_lru = head->prev_lru;
	de->prev_lru->next_lru = de;
	head->prev_lru = de;
}

//更新LRU中指定的dcache 将其移到LRU头部
static inline void update_lru(struct dir_cache_entry * de)
{
	//如果de是LRU头部 则将头指针直接指向其后续节点即可
	//在这可见二级指针的妙用
	if (de == *de->lru_head)
		*de->lru_head = de->next_lru;
	else {
		remove_lru(de);
		add_lru(de,*de->lru_head);
	}
}

/*
 * Stupid name"hash" algorithm. Write something better if you want to,
 * but I doubt it matters that much
 */
 //由name得到一个散列值
static inline unsigned long namehash(const char * name, int len)
{
	return len * *(unsigned char *) name;
}

/*
 * Hash queue manipulation. Look out for the casts..
 */
static inline void remove_hash(struct dir_cache_entry * de)
{
	if (de->h.next) {
		de->h.next->h.prev = de->h.prev;
		de->h.prev->h.next = de->h.next;
		de->h.next = NULL;
	}
}

//将de加入到hash链表（头部）
static inline void add_hash(struct dir_cache_entry * de, struct hash_list * hash)
{
	de->h.next = hash->next;
	de->h.prev = (struct dir_cache_entry *) hash;
	hash->next->h.prev = de;
	hash->next = de;
}

/*
 * Find a directory cache entry given all the necessary info.
 */
static struct dir_cache_entry * find_entry(struct inode * dir, const char * name, int len, struct hash_list * hash)
{
	struct dir_cache_entry * de = hash->next;

	for (de = hash->next ; de != (struct dir_cache_entry *) hash ; de = de->h.next) {
		if (de->dev != dir->i_dev)
			continue;
		if (de->dir != dir->i_ino)
			continue;
		if (de->version != dir->i_version)
			continue;
		if (de->name_len != len)
			continue;
		if (memcmp(de->name, name, len))
			continue;
		return de;
	}
	return NULL;
}

/*
 * Move a successfully used entry to level2. If already at level2,
 * move it to the end of the LRU queue..
 */
static inline void move_to_level2(struct dir_cache_entry * old_de, struct hash_list * hash)
{
	struct dir_cache_entry * de;

	//If already at level2
	if (old_de->lru_head == &level2_head) {
		update_lru(old_de);	//move it to the end of the LRU queue
		return;
	}
	//将其链入首部
	de = level2_head;
	level2_head = de->next_lru;	//移除链表头部的一个元素
	remove_hash(de);	//将此移除的元素从hash表中移除
	COPYDATA(old_de, de);	//将此移除的de结构内容复制为old_de,体现了内核结构的重复利用（old_de扔存在于一集缓存中）
	add_hash(de, hash);	//重新加入到hash表，由于被移除的头元素的内容和被添加的元素内容可能不一样，所以需要重新计算hash值
}

int dcache_lookup(struct inode * dir, const char * name, int len, unsigned long * ino)
{
	struct hash_list * hash;
	struct dir_cache_entry *de;

	if (len > DCACHE_NAME_LEN)
		return 0;
	hash = hash_table + hash_fn(dir->i_dev, dir->i_ino, namehash(name,len));
	de = find_entry(dir, name, len, hash);
	if (!de)
		return 0;
	*ino = de->ino;
	move_to_level2(de, hash);
	return 1;
}

void dcache_add(struct inode * dir, const char * name, int len, unsigned long ino)
{
	struct hash_list * hash;
	struct dir_cache_entry *de;
	//从这里限制了被缓存的目录项名不能超过DCACHE_NAME_LEN个字节
	if (len > DCACHE_NAME_LEN)
		return;
	hash = hash_table + hash_fn(dir->i_dev, dir->i_ino, namehash(name,len));
	if ((de = find_entry(dir, name, len, hash)) != NULL) {
		de->ino = ino;
		update_lru(de);
		return;
	}
	de = level1_head;
	level1_head = de->next_lru;
	remove_hash(de);
	de->dev = dir->i_dev;
	de->dir = dir->i_ino;
	de->version = dir->i_version;
	de->ino = ino;
	de->name_len = len;
	memcpy(de->name, name, len);
	add_hash(de, hash);
}

unsigned long name_cache_init(unsigned long mem_start, unsigned long mem_end)
{
	int i;
	struct dir_cache_entry * p;

	/*
	 * Init level1 LRU lists..
	 */
	p = level1_cache;
	do {
		p[1].prev_lru = p;
		p[0].next_lru = p+1;
		p[0].lru_head = &level1_head;	//初始化每个dir_cache_entry的二级指针lru_head，时期都指向level1_head
	} while (++p < level1_cache + DCACHE_SIZE-1);	//形成双向链表
	level1_cache[0].prev_lru = p;
	p[0].next_lru = &level1_cache[0];
	p[0].lru_head =	 &level1_head;
	level1_head = level1_cache;

	/*
	 * Init level2 LRU lists..
	 */
	p = level2_cache;
	do {
		p[1].prev_lru = p;
		p[0].next_lru = p+1;
		p[0].lru_head = &level2_head;
	} while (++p < level2_cache + DCACHE_SIZE-1);
	level2_cache[0].prev_lru = p;
	p[0].next_lru = &level2_cache[0];
	p[0].lru_head = &level2_head;
	level2_head = level2_cache;

	/*
	 * Empty hash queues..
	 */
	for (i = 0 ; i < DCACHE_HASH_QUEUES ; i++)
		hash_table[i].next = hash_table[i].next =	//bug？？？
			(struct dir_cache_entry *) &hash_table[i];
	return mem_start;
}
