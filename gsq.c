#include "gsq.h"
#include "common/lock.h"
#include "common/global.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

struct node_t {
	int type;
	void * ev;
	struct node_t* next;
};

struct queue_t {
	struct node_t* head;
	struct node_t* tail;
	int count;
};

struct gsq_t {
	struct lock_t* lock_i;
	struct lock_t* lock_o;
	struct queue_t q0;
	struct queue_t q1;
	int current;
	struct gate_t* gate;
	struct service_t* service;
};

static int queue_push(struct queue_t* queue, int type, void* ev) {
	struct node_t* node = (struct node_t*)MALLOC(sizeof(*node));
	if (node) {
		node->type = type;
		node->ev = ev;
		node->next = NULL;
		if (queue->head == NULL) {
			assert(queue->tail == NULL);
			queue->head = node;
			queue->tail = node;	
		} else {
			assert(queue->tail);
			assert(queue->tail->next == NULL);
			queue->tail->next = node;
			queue->tail = node;
		}
		++queue->count;
		return 0;
	}
	return -1;
};

static void * queue_pop(struct queue_t* queue, int* type) {
	struct node_t* node = queue->head;
	if (node) {
		if (node == queue->tail) {
			assert(node->next == NULL);
			queue->head = NULL;
			queue->tail = NULL;
		} else {
			queue->head = queue->head->next;
		}
		--queue->count;
		void* ev = node->ev;
		*type = node->type;
		free (node);
		return ev;
	}
	return NULL;
}

static void queue_clear(struct queue_t * queue) {
	if (queue) {
		do {
			struct node_t * node = queue->head;
			if (node) {
				queue->head = node->next;
				free (node);
			} else {
				break;
			}
		} while (queue->head);
	}
}

struct gsq_t * gsq_new() {
	struct gsq_t * gsq = (struct gsq_t *)MALLOC(sizeof(*gsq));
	memset(gsq, 0, sizeof(*gsq));
	gsq->lock_i = lock_new();
	gsq->lock_o = lock_new();
	return gsq;
}

void gsq_delete(struct gsq_t * gsq) {
	if (gsq){
		queue_clear(&gsq->q0);
		queue_clear(&gsq->q1);
		lock_delete(gsq->lock_i);
		lock_delete(gsq->lock_o);
		free (gsq);
	}
}

int gsq_push(struct gsq_t* q, int type, void* ev) {
	lock_lock(q->lock_i);
	struct queue_t * queue = q->current == 0 ? &q->q1 : &q->q0;
	int ret = queue_push(queue, type, ev);
	lock_unlock(q->lock_i);
	return ret;
}

void* gsq_pop(struct gsq_t* q, int* type) {
	lock_lock(q->lock_o);
	struct queue_t * queue = q->current == 0 ? &q->q0 : &q->q1;
	void * ev = queue_pop(queue, type);
	if (ev == NULL) {
		lock_lock(q->lock_i);
		q->current = q->current == 0 ? 1 : 0;
		lock_unlock(q->lock_i);
		queue = q->current == 0 ? &q->q0 : &q->q1;
		ev = queue_pop(queue, type);
	}
	lock_unlock(q->lock_o);
	return ev;
}

#include "gate.h"
#include "service.h"
#include "common/somgr/somgr.h"
void gsq_set_gs(struct gsq_t* gsq, struct gate_t* gate, struct service_t* service) {
	gsq->gate = gate;
	gsq->service = service;
}

void gsq_notify_s(struct gsq_t* gsq) {				//唤醒service
	struct somgr_t * somgr = gate_notifyer(gsq->gate);
	somgr_notify_s(somgr);
}

void gsq_notify_g(struct gsq_t* gsq) {				//唤醒gate
	struct somgr_t * somgr = gate_notifyer(gsq->gate);
	somgr_notify_g(somgr);
}

void gsq_notify_wait_g(struct gsq_t* gsq, int ms) {	//service模块调来于等待gate事件, gate模块可以随时调用somgr_notify_s来唤醒它
	struct somgr_t * somgr = gate_notifyer(gsq->gate);
	somgr_notify_wait_g(somgr, ms);
}
