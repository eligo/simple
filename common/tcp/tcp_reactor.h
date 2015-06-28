#ifndef _TCP_REACTOR_HEADER_
#define _TCP_REACTOR_HEADER_
#include "tcp.h"
#include <stdint.h>

struct tcpreactor_t;

struct tcpreactor_t * tcpreactor_create();
void tcpreactor_destroy(struct tcpreactor_t * reactor);
int tcpreactor_runonce(struct tcpreactor_t * reactor, int timeout_ms);
int tcpreactor_runonce_ex(struct tcpreactor_t * reactor, int timeout_ms, void * ud, tcp_wait_cb wcb);

int tcp_connect(struct tcpreactor_t * reactor, const char * ip, int port, void * ud, tcp_ev_connectedcb ccb, tcp_ev_errorcb ecb);
int tcp_listen(struct tcpreactor_t * reactor, const char * ip, int port, void * ud, tcp_ev_acceptcb acb, tcp_ev_errorcb ecb);

int tcp_ev_cb(struct tcpreactor_t * reactor, int id, void * ud, tcp_ev_readcb rcb, tcp_ev_errorcb ecb, tcp_ev_expandrcache expandrcb, tcp_ev_expandwcache expandwcb);
int tcp_write(struct tcpreactor_t * reactor, int id, const char * data, int dlen, const char *data2, int dlen2);
int tcp_flush(struct tcpreactor_t * reactor, int id);
int tcp_kick(struct tcpreactor_t * reactor, int id);
int tcp_wsz(struct tcpreactor_t * reactor, int id, int * cap, int * dn);
int tcp_set_rbuf(struct tcpreactor_t * reactor, int id, int cap);
int tcp_set_wbuf(struct tcpreactor_t * reactor, int id, int cap);
const char * tcp_getpeername(struct tcpreactor_t * reactor, int id, int *err, int *port);
const char * tcp_getsockname(struct tcpreactor_t * reactor, int id, int *err, int *port);
uint32_t tcp_bytes_read(struct tcpreactor_t * reactor);
uint32_t tcp_bytes_write(struct tcpreactor_t * reactor);
uint32_t tcp_bytes_parse(struct tcpreactor_t * reactor);
uint32_t tcp_bytes_towrite(struct tcpreactor_t * reactor);
uint32_t tcp_times_read(struct tcpreactor_t * reactor);
uint32_t tcp_times_write(struct tcpreactor_t * reactor);
#endif