#ifndef __SO_UTIL_HEADER__
#define __SO_UTIL_HEADER__
#include <stdint.h>

enum SOSTATE {
	SOS_LISTEN = 1,
	SOS_WRITABLE,
	SOS_CONNECTTING,
	SOS_BAD,
	SOS_FREE,
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


int sbuf_expand(struct sbuf_t* sbuf, uint32_t need);
uint32_t sbuf_freesz(struct sbuf_t* sbuf);
int sbuf_readed(struct sbuf_t* sbuf, uint32_t n);
char* sbuf_cptr(struct sbuf_t* sbuf);
uint32_t sbuf_cur(struct sbuf_t* sbuf);
int sbuf_writed(struct sbuf_t* sbuf, int n);
void sbuf_reset(struct sbuf_t* sbuf);
/*soqueue_t op*/
struct so_t* soqueue_pop(struct soqueue_t* q);
void soqueue_push(struct soqueue_t* q, struct so_t* so);
void soqueue_erase(struct so_t* so);
#endif