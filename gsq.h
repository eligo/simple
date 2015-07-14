/*连接gate与service纽带*/
#ifndef __GSQ_HEADER__
#define __GSQ_HEADER__
enum GS_EV {
	G2S_TCP_ACCEPTED,
	G2S_TCP_CLOSED,
	G2S_TCP_DATA,
	G2S_TCP_CONNECTED,

	S2G_TCP_DATA,
	S2G_TCP_CLOSE,
	S2G_TCP_CONNECT,
};

struct g2s_tcp_accepted_t {
	int sid;
};

struct g2s_tcp_closed_t {
	int sid;
	int ud;
};

struct g2s_tcp_data_t {
	int sid;
	int dlen;
};

struct g2s_tcp_connected_t {
	int sid;
	int ud;
};

struct s2g_tcp_data_t {
	int sid;
	int dlen;
	char * data;
};

struct s2g_tcp_close_t {
	int sid;
};

struct s2g_tcp_connect {
	char* ip;
	int port;
	int ud;
};

struct gsq_t;
struct service_t;
struct gate_t;
struct gsq_t* gsq_new();								//创建一条线程安全队列
void gsq_delete(struct gsq_t * gsq);					//销毁一条线程安全队列
int gsq_push(struct gsq_t * q, int type, void * ev);	//向队列压数据, 线程安全(内部已上锁)
void* gsq_pop(struct gsq_t * q, int * type);			//从队列弹数据, 线程安全(内部已上锁)

void gsq_set_gs(struct gsq_t * gsq, struct gate_t* gate, struct service_t* service);
void gsq_notify_s(struct gsq_t * gsq);					//唤醒service
void gsq_notify_g(struct gsq_t * gsq);					//唤醒gate
void gsq_notify_wait_g(struct gsq_t * gsq, int ms);		//service模块调来于等待gate事件, gate模块可以随时调用somgr_notify_s来唤醒它
#endif