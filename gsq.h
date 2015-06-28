#ifndef __GSQ_HEADER__
#define __GSQ_HEADER__

enum GS_EV {
	G2S_TCP_ACCEPTED,
	G2S_TCP_CLOSED,
	G2S_TCP_DATA,

	S2G_TCP_DATA,
	S2G_TCP_CLOSE,
};

struct g2s_tcp_accepted_t {
	int sid;
};

struct g2s_tcp_closed_t {
	int sid;
};

struct g2s_tcp_data_t {
	int sid;
	int dlen;
	char * data;
};

struct s2g_tcp_data_t {
	int sid;
	int dlen;
	char * data;
};

struct s2g_tcp_close_t {
	int sid;
};

struct gsq_t;

struct gsq_t * gsq_new();
void gsq_delete(struct gsq_t * gsq);
int gsq_push(struct gsq_t * q, int type, void * ev);	//线程安全(内部已上锁)
void * gsq_pop(struct gsq_t * q, int * type);			//线程安全(内部已上锁)
#endif