#ifndef _TCP_HEADER_
#define _TCP_HEADER_
typedef int (*tcp_ev_acceptcb) (void *ud, int lid, int nid);
typedef int (*tcp_ev_readcb) (void * ud, int id, char * data, int len);
typedef int (*tcp_ev_errorcb) (void * ud, int id);
typedef int (*tcp_ev_connectedcb) (void * ud, int id);
typedef int (*tcp_ev_expandrcache) (void * ud, int fd, int csz);
typedef int (*tcp_ev_expandwcache) (void * ud, int fd, int csz, int need);
typedef void (*tcp_wait_cb) (void * ud);

enum TCPERROR {
	TCPERROR_FD = -8001,
	TCPERROR_FDSTATE = -8002,
	TCPERROR_SETOPT = -8003,
	TCPERROR_CREATEFD = -8004,
	TCPERROR_ALLOCFD = -8005,
	TCPERROR_CONNECT = -8006,
	TCPERROR_BIND = -8007,
	TCPERROR_LISTEN = -8008,
	TCPERROR_NB = -8009,
	TCPERROR_MISSING_CB = -8010,

	TCPERROR_DLEN1_ERROR = -8021,
	TCPERROR_DLEN2_ERROR = -8022,
	TCPERROR_DLEN_OVERFLOW = -8023,
	TCPERROR_WBUF_EXPAND_FAIL = -8024,
	TCPERROR_WBUF_CREATE_FAIL = -8025,
	TCPERROR_FLUSH_ERROR = -8026,
	TCPERROR_NEED_OVERFLOW = -8027,
	TCPERROR_NEWCAP_OVERFLOW = -8028,
	TCPERROR_GETPEERNAME_ERROR = -8029,

	TCPERROR_SMALLER_THAN_OLDCAP = -8051,
	TCPERROR_SYS_ALLOC_FAIL = -8052,
};
#endif