#ifndef __TIMER_HEADER__
#define __TIMER_HEADER__
#include <stdint.h>

typedef void (*func_timer_callback) (void * ud);	//定时回调用方法格式

struct timer_t;
struct timer_t * timer_new(uint32_t tickn);	//创建定时器集合
void timer_destroy(struct timer_t * timer);	//销毁定时器集合
uint32_t timer_add(struct timer_t * timer, uint32_t ticks, void * ud, func_timer_callback cb, uint32_t repeat);	//向集合添加一个定时
int timer_del(struct timer_t * timer, uint32_t tid);	//删除一个定时
void timer_tick(struct timer_t * timer);	//触发一个时刻的定时
uint32_t timer_nearest(struct timer_t * timer);	//找到最近需要定时的时刻

void tiemr_test();	//测试用
#endif