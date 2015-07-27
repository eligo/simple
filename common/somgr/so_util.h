#ifndef __SO_UTIL_HEADER__
#define __SO_UTIL_HEADER__
#include <stdint.h>

enum SOSTATE {
	SOS_LISTEN = 1 << 0,
	SOS_WRITABLE = 1 << 1,
	SOS_CONNECTTING = 1 << 2,
	SOS_BAD = 1 << 3,
	SOS_FREE = 1 << 4,
	SOS_READABLE = 1 << 5,
};

struct sbuf_t {
	char* ptr;
	uint32_t cap;
	uint32_t cur;	
};

struct so_t {
	int32_t id;
	int fd;
	uint32_t state;
	struct sbuf_t rbuf;
	struct sbuf_t wbuf;
	struct so_t* prev;
	struct so_t* next;
	struct soqueue_t* curq;
	int32_t ud;
};

struct soqueue_t {
	struct so_t* head;
	struct so_t* tail;
	uint32_t num;	
};

int sbuf_expand(struct sbuf_t* sbuf, uint32_t need);	//扩展缓存
uint32_t sbuf_freesz(struct sbuf_t* sbuf);				//获取缓存空闲的空间大小
int sbuf_readed(struct sbuf_t* sbuf, uint32_t n);		//读取n字节
char* sbuf_cptr(struct sbuf_t* sbuf);					//当前游标指针
uint32_t sbuf_cur(struct sbuf_t* sbuf);					//当前游标(已经使用的空间大小)
int sbuf_writed(struct sbuf_t* sbuf, int n);			//写入n字节
void sbuf_reset(struct sbuf_t* sbuf);					//格式化该缓存
/*soqueue_t op*/
struct so_t* soqueue_pop(struct soqueue_t* q);			//弹出socket队列
void soqueue_push(struct soqueue_t* q, struct so_t* so);//压进socket队列
void soqueue_erase(struct so_t* so);					//把socket从队列里面删除
uint32_t soqueue_num(struct soqueue_t* q);
#endif