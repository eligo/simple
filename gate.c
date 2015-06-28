#include "gate.h"
#include "gsq.h"
#include "common/tcp/tcp_reactor.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
struct gate_t {
	struct gsq_t * g2s_queue;
	struct gsq_t * s2g_queue;
	struct tcpreactor_t * reactor;
};

static int tcp_accepted(void *ud, int lid, int nid);
static int tcp_errored (void * ud, int id);
static int tcp_readed (void * ud, int id, char * data, int len);

struct gate_t * gate_new(int port, struct gsq_t * g2s_queue, struct gsq_t * s2g_queue) {
	struct gate_t * gate = (struct gate_t *) malloc (sizeof(*gate));
	struct tcpreactor_t * reactor = tcpreactor_create();
	if (0 >= tcp_listen(reactor, "0.0.0.0", port, gate, tcp_accepted, tcp_errored)) {	//端口侦听失败
		fprintf(stderr, "listen fail\n");
		tcpreactor_destroy(reactor);
		free (gate);
		return NULL;
	}
	
	gate->g2s_queue = g2s_queue;
	gate->s2g_queue = s2g_queue;
	gate->reactor = reactor;
	return gate;
}

void gate_delete(struct gate_t * gate) {
	if (gate) {
		tcpreactor_destroy(gate->reactor);
		free(gate);
	}
}

void gate_runonce (struct gate_t * gate) {
	tcpreactor_runonce(gate->reactor, 50);
	do {
		int type = 0;
		void * packet = gsq_pop(gate->s2g_queue, &type);
		if (!packet) break;
		switch (type) {
			case S2G_TCP_DATA: {
				struct s2g_tcp_data_t * ev = (struct s2g_tcp_data_t*)packet;
				tcp_write(gate->reactor, ev->sid, ev->data, ev->dlen, NULL, 0);
				free(ev->data);
				break;
			}
			case S2G_TCP_CLOSE: {
				struct s2g_tcp_data_t * ev = (struct s2g_tcp_data_t*)packet;
				tcp_flush(gate->reactor, ev->sid);
				tcp_kick(gate->reactor, ev->sid);
				break;
			}
			default: {
				assert(0);
			}
		}
		free (packet);
	} while(1);
}

int tcp_accepted(void *ud, int lid, int nid) {	//tcp 建立时回调
	struct gate_t * gate = (struct gate_t *)ud;
	tcp_ev_cb(gate->reactor, nid, gate, tcp_readed, tcp_errored, NULL, NULL);
	struct g2s_tcp_accepted_t * ev = (struct g2s_tcp_accepted_t*) malloc(sizeof(*ev));
	ev->sid = nid;
	gsq_push(gate->g2s_queue, G2S_TCP_ACCEPTED, ev);
	return 0;
}

int tcp_errored (void * ud, int id) {	//tcp 连接断开时回调
	struct gate_t * gate = (struct gate_t *)ud;
	struct g2s_tcp_closed_t * ev = (struct g2s_tcp_closed_t*) malloc(sizeof(*ev));
	ev->sid = id;
	gsq_push(gate->g2s_queue, G2S_TCP_CLOSED, ev);
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
			struct g2s_tcp_data_t * ev = (struct g2s_tcp_data_t*) malloc(sizeof(*ev));
			ev->sid = id;
			ev->dlen = cur - start;
			ev->data = (char*) malloc (len);
			memcpy(ev->data, data + start, ev->dlen);
			readed += cur - start + 2;
			start = cur + 2;
			cur += 2;
			gsq_push(gate->g2s_queue, G2S_TCP_DATA, ev);	//把业务数据包通过队列传给 service 模块
		} else
			cur ++;
	}
	return readed;	//返回读取了的字节数
}