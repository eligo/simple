//基于时间轮理论的定时器实现
#include "timer.h"
#include "../global.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <sys/time.h>

struct tobjqueue_t ;
struct tobj_t {						//用户节点(需定时的对象)
	uint32_t id;					//tid
	uint32_t timeout;				//const
	uint32_t timeleft;				//left
	uint32_t repeat;				//0 loop else n
	void* ud;						//user data
	func_timer_callback cb;			//user callback
	struct tobjqueue_t* container;	
	struct tobj_t* prev;
	struct tobj_t* next;
};

struct tobjqueue_t {				//时刻节点
	uint32_t id;
	struct tobj_t * tail;
	struct tobj_t * head;
	uint32_t objn;
};

struct timer_t {					//定时器
	struct tobjqueue_t* ticklist;	//时刻 (非空闲的用户节点会挂到具体的时刻上)
	uint32_t ticklistn;				//时刻节点数量
	uint32_t current;				//当时时刻
	struct tobjqueue_t longlist;	//长时刻
	uint32_t ticking;				//正在触发定时的用户节点
	struct tobj_t** objpool;		//用户节点池
	uint32_t objpooln;				//池数量
	struct tobjqueue_t freelist;	//空闲的用户节点
};

static void expand_objpool(struct timer_t * timer);
static void add_obj_raw (struct timer_t * timer, struct tobj_t * tobj, uint32_t timeleft, uint32_t timeout, uint32_t repeat, void * ud, func_timer_callback cb);
static void uq_addtail(struct tobjqueue_t * queue, struct tobj_t * tobj);
static struct tobj_t * uq_pophead(struct tobjqueue_t * queue);
static void uq_erase(struct tobjqueue_t * queue, struct tobj_t * tobj);

struct timer_t* timer_new(uint32_t tickn) {
	struct timer_t * timer = (struct timer_t *)MALLOC(sizeof(*timer));
	memset(timer, 0, sizeof(*timer));
	timer->ticklistn = tickn;
	timer->ticklist = (struct tobjqueue_t*)MALLOC(sizeof(*timer->ticklist)*tickn);
	memset(timer->ticklist, 0, sizeof(*timer->ticklist)*tickn);
	uint32_t i = 0;
	for (;i<tickn;i++) {
		timer->ticklist[i].id = i + 1;
	}
	return timer;
}

void timer_destroy(struct timer_t * timer) {
	uint32_t i = 0;
	for (; i < timer->objpooln; ++i)
		FREE(timer->objpool[i]);
	FREE(timer->objpool);
	FREE(timer->ticklist);
	FREE(timer);
}

uint32_t timer_add(struct timer_t * timer, uint32_t timeout, void * ud, func_timer_callback cb, uint32_t repeat) {
	struct tobj_t * tobj = uq_pophead(&timer->freelist);
	if (!tobj) {
		expand_objpool(timer);
		tobj = uq_pophead(&timer->freelist);
	}

	assert(tobj);
	add_obj_raw(timer, tobj, timeout, timeout, repeat, ud, cb);
	return tobj->id;
}

int timer_del(struct timer_t * timer, uint32_t tid) {
	if (!tid) return -1;
	if (timer->objpooln < tid) return -2;

	struct tobj_t * tobj = timer->objpool[tid - 1];
	if (timer->ticking == tid) {	//当前正在回调它
		tobj->repeat = 1;			//标记成最后一次, 架设完会释放这个节点
		return 0;
	} 
	assert(tobj->container);
	uq_erase(tobj->container, tobj);		//从时间点上把它移出
	uq_addtail(&timer->freelist, tobj);	//并放回用户池备用
	return 0;
}

void timer_tick(struct timer_t * timer) {		
	uint32_t slot = timer->current%timer->ticklistn;
	struct tobjqueue_t * tick = &timer->ticklist[slot];
	uint32_t num = tick->objn;
	while (num-- > 0) {
		struct tobj_t * tobj = uq_pophead(tick);
		if (NULL == tobj) break;	
		timer->ticking = tobj->id;						//设置为正在触发状态
		int erased = 0;
		if (tobj->repeat > 0 && --tobj->repeat == 0) 
			erased = 1;
		tobj->cb(tobj->ud, tobj->id, erased);
		if (erased) {
			tobj->cb = NULL;
			uq_addtail(&timer->freelist, tobj);
		} else {
			add_obj_raw(timer, tobj, tobj->timeout, tobj->timeout, tobj->repeat, tobj->ud, tobj->cb);
		}
	}
	timer->current = (++timer->current)%timer->ticklistn;
	timer->ticking = 0;
	if (timer->current == 0) {
		struct tobjqueue_t tmptick;
		memset(&tmptick, 0, sizeof(tmptick));
		struct tobj_t * tobj = uq_pophead(&timer->longlist);
		while (tobj) {
			if (tobj->timeleft < timer->ticklistn) {
				add_obj_raw(timer, tobj, tobj->timeleft, tobj->timeout, tobj->repeat, tobj->ud, tobj->cb);
			} else {
				tobj->timeleft -= timer->ticklistn;
				uq_addtail(&tmptick, tobj);
			}
			tobj = uq_pophead(&timer->longlist);
		}

		tobj = uq_pophead(&tmptick);
		while (tobj) {
			uq_addtail(&timer->longlist, tobj);
			tobj = uq_pophead(&tmptick);
		}
	}
}

uint32_t timer_nearest(struct timer_t * timer) {
	uint32_t current = timer->current;
	uint32_t n = 0;
	for (; current < timer->ticklistn; ++current, ++n) {
		if (timer->ticklist[current].head)
			break;
	}
	return n;
}

void uq_addtail(struct tobjqueue_t * tick, struct tobj_t * tobj) {
	assert(tick);
	assert(tobj->container == NULL);
	tobj->container = tick;
	if (tick->tail) {
		assert(tick->head);
		tick->tail->next = tobj;
		tobj->prev = tick->tail;
		tobj->next = NULL;
		tick->tail = tobj;
	} else {
		assert(NULL == tick->head);
		tick->head = tobj;
		tick->tail = tobj;
		tobj->prev = NULL;
		tobj->next = NULL;
	}
	++tick->objn;
}

struct tobj_t * uq_pophead(struct tobjqueue_t * tick) {
	struct tobj_t * tobj = tick->head;
	if (tobj) {
		assert(tick == tobj->container);
		tick->head = tobj->next;
		if (tick->head == NULL) {
			assert(tobj == tick->tail);
			tick->tail = NULL;
		} else {
			assert(tick->tail);
			tick->head->prev = NULL;
		}
		tobj->next = NULL;
		tobj->prev = NULL;
		tobj->container = NULL;
		--tick->objn;
	}
	return tobj;
}

void uq_erase(struct tobjqueue_t * tick, struct tobj_t * tobj) {
	assert(tobj->container == tick);
	tick->head = tobj->next;
	if (tick->head)
		tick->head->prev = NULL;
	else {
		assert(tobj == tick->tail);
		tick->head = NULL;
		tick->tail = NULL;
	}

	tobj->next = NULL;
	tobj->prev = NULL;
	tobj->container = NULL;
}

void add_obj_raw (struct timer_t * timer, struct tobj_t * tobj, uint32_t timeleft, uint32_t timeout, uint32_t repeat, void * ud, func_timer_callback cb) {
	assert(timeleft <= timeout);
	if (timer->ticking) {
		timeleft = timeleft == 0 ? 1 : timeleft;
	}
	tobj->ud = ud;
	tobj->timeout = timeout;
	tobj->timeleft = timeleft;
	tobj->repeat = repeat;
	tobj->cb = cb;
	tobj->next = NULL;
	if (timeleft >= timer->ticklistn) {
		tobj->timeleft -= (timer->ticklistn - timer->current);
		uq_addtail(&timer->longlist, tobj);
	} else {
		uint32_t slot = (timer->current + timeleft) % timer->ticklistn;
		struct tobjqueue_t * tick = &timer->ticklist[slot];
		uq_addtail(tick, tobj);
	}
}

void expand_objpool(struct timer_t * timer) {	//扩展用户节点数量
	uint32_t pooln = timer->objpooln == 0? 8 : timer->objpooln * 2;
	timer->objpool = (struct tobj_t**)REALLOC(timer->objpool, pooln*sizeof(struct tobj_t*));
	uint32_t i = timer->objpooln;
	for (; i < pooln; ++i) {
		struct tobj_t * tobj = (struct tobj_t*)MALLOC(sizeof(*tobj));
		memset(tobj, 0, sizeof(*tobj));
		tobj->id = i + 1;
		timer->objpool[i] = tobj;
		uq_addtail(&timer->freelist, tobj);
	}
	timer->objpooln = pooln;
}

uint64_t time_currentms() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}