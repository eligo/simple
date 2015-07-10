#include "so_util.h"
#include "../global.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

int sbuf_expand(struct sbuf_t* sbuf, uint32_t need) {
	uint32_t cap = sbuf->cap + need + 1024;
	if (cap < sbuf->cap) return -1;	//回绕了
	cap = cap/1024*1024;
	char* ptr = (char*)REALLOC(sbuf->ptr, cap);
	if (!ptr) return -2;
	sbuf->ptr = ptr;
	sbuf->cap = cap;
	return 0;
}

uint32_t sbuf_freesz(struct sbuf_t* sbuf) {
	return sbuf->cap - sbuf->cur;
}

int sbuf_readed(struct sbuf_t* sbuf, uint32_t n) {
	if (n > sbuf->cur) return -1;
	uint32_t left = sbuf->cur - n;
	memcpy(sbuf->ptr, sbuf->ptr + n, left);
	sbuf->cur = left;
	return 0;
}

char* sbuf_cptr(struct sbuf_t* sbuf) {
	return sbuf->ptr + sbuf->cur;
}

uint32_t sbuf_cur(struct sbuf_t* sbuf) {
	return sbuf->cur;
}

int sbuf_writed(struct sbuf_t* sbuf, int n) {
	uint32_t fz = sbuf_freesz(sbuf);
	if (fz < n) return -1;
	sbuf->cur += n;
	return 0;
}

void sbuf_reset(struct sbuf_t* sbuf) {
	if (sbuf->ptr)
		FREE(sbuf->ptr);
	sbuf->ptr = NULL;
	sbuf->cur = 0;
	sbuf->cap = 0;	
}

/*soqueue_t op*/
struct so_t* soqueue_pop(struct soqueue_t* q) {
	struct so_t* so = q->head;
	if (so) {
		q->head = so->next;
		if (!q->head) {
			assert(q->tail == so);
			q->tail = NULL;
		} else {
			q->head->prev = NULL;
		}
		--q->num;
		so->prev = NULL;
		so->next = NULL;
		so->curq = NULL;
	}
	return so;
}

void soqueue_push(struct soqueue_t* q, struct so_t* so) {
	assert(!so->curq);
	if (q->tail) {
		assert(q->head);
		q->tail->next = so;
		so->next = NULL;
		so->prev = q->tail;
		q->tail = so;
	} else {
		assert(!q->head);
		q->head = so;
		q->tail = so;
		so->next = NULL;
		so->prev = NULL;
	}
	so->curq = q;
	++q->num;
}

void soqueue_erase(struct so_t* so) {
	struct soqueue_t* q = so->curq;
	if (!q) return;
	if (so->prev) {
		so->prev->next = so->next;
	} else {
		assert(so == q->head);
		q->head = so->next;
	}
	if (so->next) {
		so->next->prev = so->prev;
	} else {
		assert(so == q->tail);
		q->tail = so->prev;
	}
	so->next = NULL;
	so->prev = NULL;
	so->curq = NULL;
	--q->num;
}