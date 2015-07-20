//互斥锁
#ifndef __LOCK_HEADER__
#define __LOCK_HEADER__
struct lock_t;
struct lock_t* lock_new();				//创建一把锁
void lock_delete(struct lock_t * lo);	//销毁一把锁
void lock_lock(struct lock_t * lo);		//上锁
void lock_unlock(struct lock_t * lo);	//解锁
#endif