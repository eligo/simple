//网络收发模块
#ifndef __GATE_HEADER__
#define __GATE_HEADER__

struct gate_t;
struct gsq_t;
struct somgr_t;

struct gate_t * gate_new(int port, struct gsq_t * g2s_queue, struct gsq_t * s2g_queue);	//创建一个网关
void gate_delete(struct gate_t * gate);													//销毁一个网关
void gate_runonce (struct gate_t * gate);												//驱动网关工作
void* gate_notifyer(struct gate_t* gate);												
#endif