#include "gate.h"
#include "gsq.h"
#include "common/somgr/somgr.h"
#include "common/timer/timer.h"
#include "common/global.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
struct gate_t {
	struct gsq_t* g2s_queue;
	struct gsq_t* s2g_queue;
	struct somgr_t* somgr;
	int qflag;
};

static int tcp_accepted(void *ud, int lid, int nid);
static int tcp_errored (void * ud, int id, int ui);
static int tcp_readed (void * ud, int id, char * data, int len);
static int tcp_connected (void* ud, int id, int ui); 
static void notify_so_error(struct gate_t* gate, int id, int ud);

struct gate_t * gate_new(int port, struct gsq_t * g2s_queue, struct gsq_t * s2g_queue) {
	struct gate_t* gate = (struct gate_t *) MALLOC (sizeof(*gate));
	struct somgr_t* somgr = somgr_new(gate, tcp_accepted, tcp_readed, tcp_errored, tcp_connected);
	if (port && 0 >= somgr_listen(somgr, "0.0.0.0", port)) {	//端口侦听失败
		fprintf(stderr, "listen fail\n");
		somgr_destroy(somgr);
		FREE (gate);
		return NULL;
	}
	
	gate->g2s_queue = g2s_queue;
	gate->s2g_queue = s2g_queue;
	gate->somgr = somgr;
	return gate;
}

void gate_delete(struct gate_t * gate) {
	if (gate) {
		somgr_destroy(gate->somgr);
		FREE(gate);
	}
}

struct somgr_t* gate_get_somgr(struct gate_t* gate) {
	return gate->somgr;
}

void gate_runonce (struct gate_t * gate) {
	uint64_t stm = time_ms();
	uint64_t ctm = 0;
	uint32_t count = 0;
	int sleepms = 100;
	do {					//处理service递交过来的请求
		int type = 0;
		void * packet = gsq_pop(gate->s2g_queue, &type);
		if (!packet) break;
		switch (type) {
			case S2G_TCP_DATA: {
				struct s2g_tcp_data_t* ev = (struct s2g_tcp_data_t*)packet;
				somgr_write(gate->somgr, ev->sid, (char*)ev+sizeof(*ev), ev->dlen);
				break;
			}
			case S2G_TCP_CLOSE: {
				struct s2g_tcp_close_t* ev = (struct s2g_tcp_close_t*)packet;
				somgr_kick(gate->somgr, ev->sid);
				break;
			}
			case S2G_TCP_CONNECT: {
				struct s2g_tcp_connect* ev = (struct s2g_tcp_connect*)packet;
				int id = somgr_connect(gate->somgr, ev->ip, ev->port, ev->ud);
				if (id <= 0)
					notify_so_error(gate, 0, ev->ud);
				FREE(ev->ip);
				break;
			}
			default: {
				assert(0);
			}
		}
		FREE (packet);
		if (++count%1000 == 0) {
			ctm = time_ms();
			if (ctm - stm >= 50) {
				sleepms = 0;
				break;
			}
		}
	} while(1);
	somgr_runonce(gate->somgr, sleepms);	//处理网络读写
	if (gate->qflag) {
		gsq_notify_s(gate->s2g_queue);
		gate->qflag = 0;
	}
}

int tcp_accepted(void *ud, int lid, int nid) {	//tcp 建立时回调
	struct gate_t * gate = (struct gate_t *)ud;
	struct g2s_tcp_accepted_t * ev = (struct g2s_tcp_accepted_t*)MALLOC(sizeof(*ev));
	ev->sid = nid;
	gsq_push(gate->g2s_queue, G2S_TCP_ACCEPTED, ev);
	gate->qflag = 1;
	return 0;
}

void notify_so_error(struct gate_t* gate, int id, int ui) {
	struct g2s_tcp_closed_t * ev = (struct g2s_tcp_closed_t*)MALLOC(sizeof(*ev));
	ev->sid = id;
	ev->ud = ui;
	gsq_push(gate->g2s_queue, G2S_TCP_CLOSED, ev);
	gate->qflag = 1;
}

int tcp_errored(void * ud, int id, int ui) {	//tcp 连接断开时回调
	notify_so_error(ud, id, ui);
	return 0;
}

int tcp_connected (void* ud, int id, int ui) {
	struct gate_t * gate = (struct gate_t *)ud;
	struct g2s_tcp_connected_t * ev = (struct g2s_tcp_connected_t*)MALLOC(sizeof(*ev));
	ev->sid = id;
	ev->ud = ui;
	gsq_push(gate->g2s_queue, G2S_TCP_CONNECTED, ev);
	gate->qflag = 1;
	return 0;
}

int tcp_readed (void * ud, int id, char * data, int len) {	//收到数据时回调, id, tcp连接的id(不是fd), data, 数据开始的指针, len 数据长度
	struct gate_t * gate = (struct gate_t *)ud;
	int start = 0;
	int cur = 0;
	int readed = 0;
	for (;cur < len;) {
		if (data[cur] == '\r') {	//tcp 是流式数据, 所以要双端约定好业务包的分割方式,暂时用\r\n进行分割
			if (cur == len - 1)
				return readed;
			if (data[cur+1] != '\n')
				return -1;	//error occur
			int plen = cur - start;
			struct g2s_tcp_data_t * ev = (struct g2s_tcp_data_t*)MALLOC(sizeof(*ev)+plen);
			ev->sid = id;
			ev->dlen = plen;
			memcpy((char*)ev+sizeof(*ev), data + start, ev->dlen);
			readed += cur - start + 2;
			start = cur + 2;
			cur += 2;
			gsq_push(gate->g2s_queue, G2S_TCP_DATA, ev);	//把业务数据包通过队列传给 service 模块
			gate->qflag = 1;
		} else
			cur ++;
	}
	return readed;	//返回读取了的字节数
}

void gate_notify_s(struct gate_t* gate) {			//唤醒service
	somgr_notify_s(gate->somgr);
}

void gate_notify_g(struct gate_t* gate) {			//唤醒gate
	somgr_notify_g(gate->somgr);
}

void gate_wait_g(struct gate_t* gate, int ms) {		//service模块调来于等待gate事件, gate模块可以随时调用somgr_notify_s来唤醒它
	somgr_wait_g(gate->somgr, ms);
}