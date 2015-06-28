#ifndef _TCP_UTIL_
#define _TCP_UTIL_
#include "tcp.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct fd_t;
struct fdbuf_t;
struct fdq_t;

struct fdnode_t {
	struct fd_t * fdt;
	struct fdnode_t * pre;
	struct fdnode_t * next;
	unsigned char inq;
};

struct fdq_t {
	struct fdnode_t * head;
	struct fdnode_t * tail;
	int sz;
};

struct fd_t {
	int id;
	int fd;//file desc
	int state;
	tcp_ev_acceptcb acb;
	tcp_ev_readcb rcb;
	tcp_ev_errorcb ecb;
	tcp_ev_connectedcb ccb;
	tcp_ev_expandrcache can_expendrcb;
	tcp_ev_expandwcache can_expendwcb;
	void * ud;//user data
	unsigned char writable;
	unsigned char readable;
	struct fdbuf_t * rbuf;
	struct fdbuf_t * wbuf;
	struct fdnode_t b_node;
	struct fdnode_t f_node;
	struct fdnode_t r_node;
	struct fdnode_t p_node;
	struct fdnode_t w_node;
	int pingpong;
	int session;
};

void
fdq_push_tail(struct fdq_t * queue, struct fdnode_t * node) {
	assert(!node->pre);
	assert(!node->next);
	if (queue->tail) {
		queue->tail->next = node;
		node->pre = queue->tail;
		queue->tail = node;
		node->next = NULL;
	} else {
		assert(!queue->head);
		queue->head = node;
		queue->tail = node;
		node->pre = NULL;
		node->next = NULL;
	}
	++queue->sz;
	node->inq = 1;
}

void
fdq_erase(struct fdq_t * queue, struct fdnode_t * node) {
	if (node->pre)
		node->pre->next = node->next;
	else {
		assert(queue->head == node);
		queue->head = node->next;
	}

	if (node->next)
		node->next->pre = node->pre;
	else {
		assert(queue->tail == node);
		queue->tail = node->pre;
	}

	node->pre = NULL;
	node->next = NULL;
	--queue->sz;
	node->inq = 0;
}

struct fdnode_t *
fdq_pop_head(struct fdq_t * queue) {
	struct fdnode_t * node = queue->head;
	if (node)
		fdq_erase(queue, node);
	return node;
}

int 
fdq_sz(struct fdq_t* queue) {
	return queue->sz;
}

#endif