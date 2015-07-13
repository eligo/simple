//互斥锁
#ifndef __LOCK_HEADER__
#define __LOCK_HEADER__
struct lock_t;
struct lock_t * lock_new();
void lock_delete(struct lock_t * lo);
void lock_lock(struct lock_t * lo);
void lock_unlock(struct lock_t * lo);

#endif