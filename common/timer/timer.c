#include "timer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
struct tobj_t {
	uint32_t id;
	uint32_t ticks;
	uint32_t tickb;
	void * ud;
	func_timer_callback cb;
	struct tobj_t * next;
};

struct tick_t {
	uint32_t id;
	struct tobj_t * tail;
	struct tobj_t * head;
	uint32_t objn;
};

struct timer_t {
	struct tick_t * list;
	uint32_t listn;
	uint32_t current;
	struct tick_t long_ticks;
	uint8_t ticking;
	//struct tobj_t * pool;
	//uint32_t pooln;
};

struct timer_t * timer_new(uint32_t tickn) {
	struct timer_t * timer = (struct timer_t *)malloc(sizeof(*timer));
	timer->listn = tickn;
	timer->list = (struct tick_t*)malloc(sizeof(*timer->list)*tickn);
	memset(timer->list, 0, sizeof(*timer->list)*tickn);
	uint32_t i = 0;
	for (;i<tickn;i++) {
		timer->list[i].id = i;
	}

	return timer;
}

void timer_destroy(struct timer_t * timer) {
	uint32_t i = 0;
	for (;i < timer->listn; ++i) {
		struct tick_t * tick = &timer->list[i];
		while (tick->head) {
			struct tobj_t * tobj = tick->head;
			tick->head = tobj->next;
			free(tobj);
		}
	}
	struct tick_t * tick = &timer->long_ticks;
	while (tick->head) {
		struct tobj_t * tobj = tick->head;
		tick->head = tobj->next;
		free(tobj);
	}
}

static void tick_addobj(struct timer_t * timer, struct tick_t * tick, uint32_t ticks, void * ud, func_timer_callback cb) {
	struct tobj_t * tobj = (struct tobj_t*)malloc(sizeof(*tobj));
	tobj->ticks = ticks;
	tobj->tickb = timer->current;
	tobj->ud = ud;
	tobj->cb = cb;
	tobj->next = NULL;
	if (tick->head == NULL) {
		assert(tick->tail == NULL);
		tick->head = tobj;
		tick->tail = tobj;
	} else {
		assert(tick->tail);
		tick->tail->next = tobj;
		tick->tail = tobj;
	}
	++tick->objn;
}

void timer_add(struct timer_t * timer, uint32_t ticks, void * ud, func_timer_callback cb) {
	if (ticks >= timer->listn) {
		tick_addobj(timer, &timer->long_ticks, ticks, ud, cb);
		return;
	} 

	uint32_t slot = (timer->current + ticks) % timer->listn;
	struct tick_t * tick = &timer->list[slot];
	tick_addobj(timer, tick, ticks, ud, cb);
}

void timer_tick(struct timer_t * timer) {
	timer->ticking = 1;
	uint32_t slot = timer->current%timer->listn;
	timer->current = (++timer->current)%timer->listn;
	struct tick_t * tick = &timer->list[slot];
	uint32_t num = tick->objn;
	while (tick->head) {
		struct tobj_t * tobj = tick->head;
		tobj->cb(tobj->ud);
		tick->head = tobj->next;
		--tick->objn;
		free (tobj);
		if (NULL == tick->head) {
			assert(tick->tail == tobj);
			tick->tail = NULL;
			break;
		}
		if (--num == 0)
			break;
	}
	timer->ticking = 0;
	if (timer->current == 0) {
		struct tick_t * longtick = &timer->long_ticks;
		struct tobj_t * tobj = longtick->head;
		struct tobj_t * prev = NULL;
		for (;tobj;) {
			assert(tobj->ticks >= timer->listn);
			uint32_t passed = timer->listn - tobj->tickb;
			tobj->tickb = 0;
			assert(tobj->ticks >= passed);
			tobj->ticks -= passed;//timer->listn;
			if (tobj->ticks < timer->listn) {
				timer_add(timer, tobj->ticks, tobj->ud, tobj->cb);
				struct tobj_t * tmp = tobj;
				if (prev) {
					assert(tobj != longtick->head);
					assert(prev->next == tobj);
					prev->next = tobj->next;
				} else if (tobj == longtick->head) {
					longtick->head = tobj->next;
				}
				tobj = tobj->next;
				free(tmp);
			} else {
				prev = tobj;
				tobj = tobj->next;
			}
		}
		longtick->tail = prev;
	}
}

uint32_t timer_nearest(struct timer_t * timer) {
	uint32_t current = timer->current;
	uint32_t n = 0;
	for (; current < timer->listn; ++current, ++n) {
		if (timer->list[current].head)
			break;
	}
	return n;
}

/* test
struct timer_t * timer = NULL;
static void test_cb(void *ud) {
	printf("on_timer %d\n", (int)ud);
}

void timer_test() {
	timer = timer_new(5);
	timer_add(timer, 1, (void*)1, test_cb);
	timer_add(timer, 5, (void*)5, test_cb);
	timer_add(timer, 6, (void*)6, test_cb);
	timer_add(timer, 8, (void*)8, test_cb);
	timer_add(timer, 9, (void*)9, test_cb);
	timer_add(timer, 15, (void*)15, test_cb);
	timer_add(timer, 18, (void*)18, test_cb);
	timer_add(timer, 19, (void*)19, test_cb);
	timer_add(timer, 20, (void*)20, test_cb);
	timer_add(timer, 30, (void*)30, test_cb);
	timer_add(timer, 60, (void*)60, test_cb);
	int i= 0;
	for (;i<80;i++) {
		printf("tick %d\n", i);
		timer_tick(timer);
	}
			
}*/