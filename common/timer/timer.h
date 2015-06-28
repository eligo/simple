#ifndef __TIMER_HEADER__
#define __TIMER_HEADER__
#include <stdint.h>

typedef void (*func_timer_callback) (void * ud);

struct timer_t;
struct timer_t * timer_new(uint32_t tickn);
void timer_destroy(struct timer_t * timer);
void timer_add(struct timer_t * timer, uint32_t ticks, void * ud, func_timer_callback cb);
void timer_tick(struct timer_t * timer);
#endif