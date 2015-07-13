//网络收发模块
#ifndef __GATE_HEADER__
#define __GATE_HEADER__

struct gate_t;
struct gsq_t;
struct somgr_t;

struct gate_t * gate_new(int port, struct gsq_t * g2s_queue, struct gsq_t * s2g_queue);
void gate_delete(struct gate_t * gate);
void gate_runonce (struct gate_t * gate);

struct somgr_t* gate_get_somgr(struct gate_t* gate);
void gate_notify_s(struct gate_t* gate);			//唤醒service
void gate_notify_g(struct gate_t* gate);			//唤醒gate
void gate_wait_g(struct gate_t* gate, int ms);		//service模块调来于等待gate事件, gate模块可以随时调用somgr_notify_s来唤醒它
#endif