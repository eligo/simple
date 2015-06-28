#include "lock.h"
#include <stdlib.h>

struct lock_t {
	int m;
};

struct lock_t * lock_new() {
	struct lock_t * lo = (struct lock_t*) malloc (sizeof(*lo));
	if (lo)
		lo->m = 0;
	return lo;
}

void lock_delete(struct lock_t * lo) {
	if (lo)
		free(lo);
}

void lock_lock(struct lock_t * lo) {
	while (__sync_lock_test_and_set(&lo->m,1)) 
	{}
}

void lock_unlock(struct lock_t * lo) {
	__sync_lock_release(&lo->m);
}
