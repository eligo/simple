//基于时间轮理论的定时器实现
#include "timer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

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
	uint8_t ticking;				//是否正在触发定时
	struct tobj_t** objpool;		//用户节点池
	uint32_t objpooln;				//池数量
	struct tobjqueue_t freelist;	//空闲的用户节点
};

static void DIE (const char * msg) {
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

static void *REALLOC(void *p, size_t size) {
	void * ptr = realloc(p, size);
	if (!ptr) DIE("TIMER REALLOC FAIL\n");
	return ptr;
}

static void *MALLOC(size_t size) {
	void * ptr = malloc(size);
	if (!ptr) DIE("TIMER MALLOC FAIL\n");
	return ptr;
}

static void expand_objpool(struct timer_t * timer);
static void timer_add_raw (struct timer_t * timer, struct tobj_t * tobj, uint32_t timeleft, uint32_t timeout, uint32_t repeat, void * ud, func_timer_callback cb);
static void tick_addtail(struct tobjqueue_t * tick, struct tobj_t * tobj);
static struct tobj_t * tick_pophead(struct tobjqueue_t * tick);
static void tick_erase(struct tobjqueue_t * tick, struct tobj_t * tobj);

struct timer_t * timer_new(uint32_t tickn) {
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
		free(timer->objpool[i]);

	free(timer->objpool);
	free(timer->ticklist);
	free(timer);
}

uint32_t timer_add(struct timer_t * timer, uint32_t timeout, void * ud, func_timer_callback cb, uint32_t repeat) {
	struct tobj_t * tobj = tick_pophead(&timer->freelist);
	if (!tobj) {
		expand_objpool(timer);
		tobj = tick_pophead(&timer->freelist);
	}

	assert(tobj);
	timer_add_raw(timer, tobj, timeout, timeout, repeat, ud, cb);
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
	tick_erase(tobj->container, tobj);		//从时间点上把它移出
	tick_addtail(&timer->freelist, tobj);	//并放回用户池备用
	return 0;
}

void timer_tick(struct timer_t * timer) {		
	uint32_t slot = timer->current%timer->ticklistn;
	struct tobjqueue_t * tick = &timer->ticklist[slot];
	uint32_t num = tick->objn;
	while (num-- > 0) {
		struct tobj_t * tobj = tick_pophead(tick);
		if (NULL == tobj) break;
		timer->ticking = tobj->id;						//设置为正在触发状态
		tobj->cb(tobj->ud);
		if (tobj->repeat > 0 && --tobj->repeat == 0) {	//使用结束回收该用户节点
			tobj->cb = NULL;
			tick_addtail(&timer->freelist, tobj);
		} else {											//暂不回收
			timer_add_raw(timer, tobj, tobj->timeout, tobj->timeout, tobj->repeat, tobj->ud, tobj->cb);
		}
	}
	timer->current = (++timer->current)%timer->ticklistn;
	timer->ticking = 0;
	if (timer->current == 0) {
		struct tobjqueue_t tmptick;
		memset(&tmptick, 0, sizeof(tmptick));
		struct tobj_t * tobj = tick_pophead(&timer->longlist);
		while (tobj) {
			if (tobj->timeleft < timer->ticklistn) {
				timer_add_raw(timer, tobj, tobj->timeleft, tobj->timeout, tobj->repeat, tobj->ud, tobj->cb);
			} else {
				tobj->timeleft -= timer->ticklistn;
				tick_addtail(&tmptick, tobj);
			}
			tobj = tick_pophead(&timer->longlist);
		}

		tobj = tick_pophead(&tmptick);
		while (tobj) {
			tick_addtail(&timer->longlist, tobj);
			tobj = tick_pophead(&tmptick);
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

void tick_addtail(struct tobjqueue_t * tick, struct tobj_t * tobj) {
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

struct tobj_t * tick_pophead(struct tobjqueue_t * tick) {
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

void tick_erase(struct tobjqueue_t * tick, struct tobj_t * tobj) {
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

void timer_add_raw (struct timer_t * timer, struct tobj_t * tobj, uint32_t timeleft, uint32_t timeout, uint32_t repeat, void * ud, func_timer_callback cb) {
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
		tick_addtail(&timer->longlist, tobj);
	} else {
		uint32_t slot = (timer->current + timeleft) % timer->ticklistn;
		struct tobjqueue_t * tick = &timer->ticklist[slot];
		tick_addtail(tick, tobj);
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
		tick_addtail(&timer->freelist, tobj);
	}
	timer->objpooln = pooln;
}




struct timer_t * timer = NULL;
uint32_t id = 0;
static void _cb(void *ud) {
	printf("...cb %d %d\n", (int)ud, id);
	//timer_del(timer, id);
}

void timer_test() {
	timer = timer_new(5);
	timer_add(timer, 2, (void*)2, _cb, 3);
	
	id = timer_add(timer, 10, (void*)10, _cb, 2);
	printf("ddddd %d\n", id);
	/*
	timer_add(timer, 3, (void*)3, _cb, 1);
	timer_add(timer, 5, (void*)5, _cb, 1);
	timer_add(timer, 6, (void*)6, _cb, 1);
	timer_add(timer, 8, (void*)8, _cb, 1);
	timer_add(timer, 9, (void*)9, _cb, 1);
	
	timer_add(timer, 11, (void*)11, _cb, 1);
	timer_add(timer, 12, (void*)12, _cb, 1);
	timer_add(timer, 13, (void*)13, _cb, 1);
	timer_add(timer, 18, (void*)18, _cb, 1);
	timer_add(timer, 19, (void*)19, _cb, 1);
	timer_add(timer, 20, (void*)20, _cb, 1);
	timer_add(timer, 30, (void*)30, _cb, 1);
	timer_add(timer, 60, (void*)60, _cb, 1);*/
	int i= 0;
	for (;i<80;i++) {
		printf("current tick %d\n", i);
		timer_tick(timer);
	}
			
}