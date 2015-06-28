#include "tcp_reactor.h"
#include "fd_buffer.h"
#include "fd_queue.h"

#include <stdio.h>  
#include <string.h>  
#include <unistd.h>  
#include <sys/socket.h>  
#include <sys/un.h>  
#include <sys/types.h>  
#include <sys/wait.h>  
#include <errno.h> 
#include <stdlib.h>
#include <assert.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>
#define FD_MAX 10240
#define _EPOLL_ET_//define 之后会选用epoll et模式
struct tcpreactor_t {
	int evfd;
	struct fd_t ** fds;
	int fdsz;
	struct fdq_t f_queue;
	struct fdq_t b_queue;
	struct fdq_t r_queue;
	struct fdq_t p_queue;
	struct fdq_t w_queue;
	struct epoll_event evs[FD_MAX];
	int pingpong;
	int inrun;
	uint32_t bytes_read;
	uint32_t bytes_write;
	uint32_t bytes_parse;
	uint32_t bytes_towrite;
	uint32_t times_read;
	uint32_t times_write;
};

enum FD_STATE {
	FD_FREE = 1,
	FD_LISTENING = 2,
	FD_CONNECTTING = 3,
	FD_BAD = 4,
	FD_CONNECTED = 5,
};

static int _set_nb(int fd);
static struct fd_t * _fd_valid (struct tcpreactor_t * reactor, int id);
static void _in_fq(struct tcpreactor_t * reactor, struct fd_t * fd);
static void _in_rq(struct tcpreactor_t * reactor, struct fd_t * fd);
static void _in_pq(struct tcpreactor_t * reactor, struct fd_t * fd);
static void _in_wq(struct tcpreactor_t * reactor, struct fd_t * fd);
static void _in_bq(struct tcpreactor_t * reactor, struct fd_t * fd);
static struct fd_t * _out_fq(struct tcpreactor_t * reactor);

static int  _ev_add(struct tcpreactor_t * reactor, struct fd_t * fd);
static void _ev_mod(struct tcpreactor_t * reactor, struct fd_t * fd);
static void _process_rq(struct tcpreactor_t * reactor);
static void _process_pq(struct tcpreactor_t * reactor);
static void _process_wq(struct tcpreactor_t * reactor);
static void _process_bq(struct tcpreactor_t * reactor);
static void _flush(struct tcpreactor_t * reactor, struct fd_t * fd);

struct tcpreactor_t * 
tcpreactor_create() {
	int succ = 0;
	struct tcpreactor_t * tcp = (struct tcpreactor_t*)malloc(sizeof(*tcp));
	if (tcp) {
		memset(tcp, 0, sizeof(*tcp));
		tcp->evfd = epoll_create(1024);
		if (tcp->evfd > 0) {
			tcp->fdsz = FD_MAX;
			tcp->fds = (struct fd_t **) malloc(tcp->fdsz * sizeof(struct fd_t*));
			if (tcp->fds) {
				succ = 1;
				memset(tcp->fds, 0, tcp->fdsz * sizeof(struct fd_t*));
				int i;
				for (i = 0; i < tcp->fdsz; ++i) {
					struct fd_t * fd = (struct fd_t *) malloc(sizeof(*fd));
					if (fd) {
						memset(fd, 0, sizeof(*fd));
						fd->state = FD_FREE;
						fd->id = i;
						fd->b_node.fdt = fd;
						fd->f_node.fdt = fd;
						fd->r_node.fdt = fd;
						fd->p_node.fdt = fd;
						fd->w_node.fdt = fd;
						tcp->fds[i] = fd;
						if (i > 0)
							_in_fq(tcp, fd);
					} else {
						succ = 0; 
						break;
					}
				}
			}
		}

		if (!succ) {
			tcpreactor_destroy(tcp);
			tcp = NULL;
		}
	}
	return tcp;
}

void 
tcpreactor_destroy(struct tcpreactor_t * reactor) {
	if (reactor->fds) {
		int i;
		for (i = 0; i < reactor->fdsz; ++i) {
			struct fd_t * fd = reactor->fds[i];
			if (fd) {
				if (fd->rbuf)
					fdbuf_destroy(fd->rbuf);
				if (fd->wbuf)
					fdbuf_destroy(fd->wbuf);
				if (fd->fd > 0) {
					close(fd->fd);
				}
				free(fd);
			}
		}
		free(reactor->fds);
	}
	free(reactor);
}

int _tcp_runonce(struct tcpreactor_t * reactor, int waitms, void * ud, tcp_wait_cb wcb) {
	const int en = epoll_wait(reactor->evfd, reactor->evs, sizeof(reactor->evs)/sizeof(reactor->evs[0]), waitms);
	if (wcb && waitms > 0)
		wcb(ud);

	if (en > 0) {
		int i;
		for (i = 0; i < en; i++) {
			const unsigned ev = reactor->evs[i].events;
			struct fd_t * fd = (struct fd_t *)reactor->evs[i].data.ptr;
			assert(fd);
			if (ev & EPOLLERR || ev & EPOLLHUP) {
				_in_bq(reactor, fd);
				continue;
			}
			switch (fd->state) {
				case FD_CONNECTED: {
					if (ev & EPOLLIN) {
						fd->readable = 1;
						if (!fd->p_node.inq)
							_in_rq(reactor, fd);
					}
					if (ev & EPOLLOUT) {
						fd->writable = 1;
						#ifndef _EPOLL_ET_
							_ev_mod(reactor, fd);
						#endif
						if (fd->wbuf && fdbuf_datan(fd->wbuf) > 0)
							_in_wq(reactor, fd);
					}
					break;
				}
				case FD_LISTENING: {
					assert(ev & EPOLLIN);
					_in_rq(reactor, fd);
					break;
				}
				case FD_CONNECTTING: {
					assert(ev & EPOLLOUT);
					int err = -1;
					socklen_t len = sizeof(err);
					if (0 == getsockopt(fd->fd, SOL_SOCKET, SO_ERROR, &err, &len) && err == 0) {
						fd->state = FD_CONNECTED;
						fd->ccb(fd->ud, fd->id);
					}
					if (fd->state != FD_CONNECTED)//ccb may abandon fd, so check it!
						_in_bq(reactor, fd);
					else {
						fd->writable = 1;
						_ev_mod(reactor, fd);//r and w, only w before
						if (fd->wbuf && fdbuf_datan(fd->wbuf) > 0)
							_in_wq(reactor, fd);
					}
					break;
				}
			}
		}
	}
	_process_wq(reactor);//flush data from user_w_buf, 有些数据是外部定时器产生的, 先flush一下无伤大雅

	_process_rq(reactor);//read from sys_r_buf
	_process_pq(reactor);//parse data from user_r_buf, flush by the way
	_process_wq(reactor);//flush data from user_w_buf
	_process_bq(reactor);//process bad fds
	reactor->inrun = 0;
	return 0;
}

int 
tcpreactor_runonce_ex(struct tcpreactor_t * reactor, int timeout_ms, void * ud, tcp_wait_cb wcb) {
	return _tcp_runonce(reactor, timeout_ms, ud, wcb);
}

int 
tcpreactor_runonce(struct tcpreactor_t * reactor, int waitms) {//不可重入
	if (reactor->inrun)
		return 0;

	reactor->inrun = 1;
	if (fdq_sz(&reactor->r_queue) || fdq_sz(&reactor->w_queue) || fdq_sz(&reactor->p_queue) || fdq_sz(&reactor->b_queue))
		waitms = 0;

	return _tcp_runonce(reactor, waitms, NULL, NULL);
}

static int _process_accept(struct tcpreactor_t * reactor, struct fd_t * fd) {
	struct sockaddr in_addr;
	socklen_t in_len = sizeof(in_addr);
	const int nfd = accept(fd->fd,&in_addr,&in_len);//TODO try nore times
	if(nfd == -1) {
		switch (errno) {
			case EINTR:
				////printf("accept EINTR\n");
				//_in_rq(reactor, fd);
				return 0;
			case EAGAIN:
				////printf("accept EAGAIN\n");
				return 1;//wait ev next time
			//case EWOULDBLOCK:
			case EMFILE:
				////printf("accept EMFILE\n");
				//_in_rq(reactor, fd);//file max
				return 0;//break;
			default:
				////printf("accept ERRNO :%d\n", errno);
				//_in_bq(reactor, fd);
				return -1;
		}
	} else if (nfd > 0) {
		if (_set_nb(nfd) != 0) {
			close(nfd);	
			return 0;//_in_rq(reactor, fd);
		} else {
			struct fd_t * nfdd = _out_fq(reactor);//fdq_pop_head(&reactor->f_queue);
			if (!nfdd) {
				close(nfd);
				return 0;
			} else {
				nfdd->fd = nfd;
				nfdd->state = FD_CONNECTED;
				_ev_add(reactor, nfdd);
				fd->acb(fd->ud, fd->id, nfdd->id);
				if (!nfdd->rcb || !nfdd->ecb)
					_in_bq(reactor, nfdd);
				if (fd->state == FD_BAD)//listen fd may close in acb(...)
					return -2;
				else
					return 0;//_in_rq(reactor, fd);
					
			}
		}
	} else
		return -3;//_in_bq(reactor, fd);
}

static void 
_process_rq(struct tcpreactor_t * reactor) {
	const int pingpong = ++reactor->pingpong;
	for (;;) {
		struct fdnode_t * fdnode = fdq_pop_head(&reactor->r_queue);
		if (!fdnode)
			break;
		
		struct fd_t * const fd = fdnode->fdt;
		assert(fd);
		if (fd->pingpong == pingpong) {
			_in_rq(reactor, fd);
			break;
		}

		assert(fdnode == &fd->r_node);
		assert(!fdnode->inq);
		fd->pingpong = pingpong;
		if (fd->state == FD_CONNECTED) {//普通连接
			if (!fd->rbuf) {
				fd->rbuf = fdbuf_create(1024);
				if (!fd->rbuf) {
					_in_bq(reactor, fd);
					continue;
				}
			} else if (0 >= fdbuf_freen(fd->rbuf)) {
				int cap = fdbuf_cap(fd->rbuf);
				if (fd->can_expendrcb) {
					int can = fd->can_expendrcb(fd->ud, fd->id, cap);
					if (fd->state == FD_BAD)
						continue;
					if (can != 0) {
						_in_bq(reactor, fd);
						continue;
					}
				} 
				cap = cap > 0? cap * 2 : 1024;
				if(0 != fdbuf_expand(fd->rbuf, cap)) {
					_in_bq(reactor, fd);
					continue;
				}
			} 

			const int rn = read(fd->fd, fdbuf_freebegin(fd->rbuf), fdbuf_freen(fd->rbuf));
			if (rn > 0) {
				assert(fdbuf_freen(fd->rbuf) >= rn);
				fdbuf_writen(fd->rbuf, rn);
				_in_pq(reactor, fd);
				reactor->bytes_read += rn;
				reactor->times_read ++;
			} else if (rn < 0) {
				switch (errno) {
					case EAGAIN:
						fd->readable = 0;
						break;//do not in q, wait next ev
					case EINTR:
						_in_rq(reactor, fd);
						break;
					default: {
						_in_bq(reactor, fd);
						break;
					}
				}
			} else {
				_in_bq(reactor, fd);
			}
		} else {
			assert(fd->state == FD_LISTENING);
			int i = 0;
			int e = 0;
			for (;++i<1024;) {
				e = _process_accept(reactor, fd);
				if (e != 0) 
					break;
			}
			if (e == 0) 
				_in_rq(reactor, fd);
			else if(e > 0)
				;//do nothing , eagin
			else if(e < 0)
				_in_bq(reactor, fd);
		}
	}
}

static void 
_process_pq(struct tcpreactor_t * reactor) {
	const int pingpong = ++reactor->pingpong;
	for (;;) {
		struct fdnode_t * fdnode = fdq_pop_head(&reactor->p_queue);
		if (!fdnode)
			break;
		
		struct fd_t * const fd = fdnode->fdt;
		assert(fd);
		if (fd->pingpong == pingpong) {
			_in_pq(reactor, fd);
			break;
		}
		assert(fdnode == &fd->p_node);
		assert(!fdnode->inq);
		assert(fd->state == FD_CONNECTED);
		fd->pingpong = pingpong;
		int parsen = fd->rcb(fd->ud, fd->id, fdbuf_databegin(fd->rbuf), fdbuf_datan(fd->rbuf));
		if (fd->state != FD_CONNECTED) {//may close in rcb
			assert(fd->state == FD_BAD);
			continue;
		}
		if (parsen < 0) {
			_in_bq(reactor, fd);
			continue;
		}
		if (parsen > 0) {
			if (parsen > fdbuf_datan(fd->rbuf)) {
				_in_bq(reactor, fd);
				continue;
			}
			fdbuf_readn(fd->rbuf, parsen);
			if (fdbuf_datan(fd->rbuf) > 0)
				_in_pq(reactor, fd);
			else if(fd->readable)
				_in_rq(reactor, fd);
			reactor->bytes_parse += parsen;
			if (fd->w_node.inq)
				_flush(reactor, fd);//提高响应的措施（可以注释掉）
		} else if (parsen == 0) {
			if (fd->readable)
				_in_rq(reactor, fd);
		} else
			_in_bq(reactor, fd);
	}
}

static void 
_flush(struct tcpreactor_t * reactor, struct fd_t * fd) {
	if (fd->wbuf == NULL)
		return;
	
	if (fdbuf_datan(fd->wbuf) > 0) {
		const int wn = write(fd->fd, fdbuf_databegin(fd->wbuf), fdbuf_datan(fd->wbuf));
		if (wn > 0) {
			assert(wn <= fdbuf_datan(fd->wbuf));
			fdbuf_readn(fd->wbuf, wn);
			if (fdbuf_datan(fd->wbuf) > 0)//remain data in cache, in q
				_in_wq(reactor, fd);
			reactor->bytes_write += wn;
			reactor->bytes_towrite -= wn;
			reactor->times_write ++;
		} else if(wn < 0) {
			switch (errno) {
				case EAGAIN: {
					fd->writable = 0;
					#ifndef _EPOLL_ET_
						_ev_mod(reactor, fd);//re watch w ev
					#endif
					break;
				}
				case EINTR:
					_in_wq(reactor, fd);
					break;
				default:
					_in_bq(reactor, fd);
					break;
			}
		}  else
			_in_bq(reactor, fd);
	}
}

static void 
_process_wq(struct tcpreactor_t * reactor) {
	const int pingpong = ++reactor->pingpong;
	for (;;) {
		struct fdnode_t * fdnode = fdq_pop_head(&reactor->w_queue);
		if (!fdnode) 
			break;

		struct fd_t * const fd = fdnode->fdt;
		assert(fd);
		if (fd->pingpong == pingpong) {
			_in_wq(reactor, fd);
			break;
		}
		fd->pingpong = pingpong;
		assert(fd->state == FD_CONNECTED);
		_flush(reactor, fd);
	}
}

static void 
_process_bq(struct tcpreactor_t * reactor) {
	int sz = fdq_sz(&reactor->b_queue);
	for (;sz-->0;) {
		struct fdnode_t * fdnode = fdq_pop_head(&reactor->b_queue);
		if (!fdnode)
			break;

		struct fd_t * const fd = fdnode->fdt;
		assert(fd);
		assert(fd->state == FD_BAD);
		assert(fd->ecb);
		fd->ecb(fd->ud, fd->id);
		close(fd->fd);
		
		fd->fd = 0;
		fd->state = FD_FREE;
		fd->writable = 0;
		fd->acb = NULL;
		fd->ecb = NULL;
		fd->rcb = NULL;
		fd->ccb = NULL;
		fd->can_expendrcb = NULL;
		fd->can_expendwcb = NULL;
		fd->ud = NULL;
		++fd->session;
		if (fd->rbuf) {
			fdbuf_destroy(fd->rbuf);
			fd->rbuf = NULL;
		}
		if (fd->wbuf) {
			reactor->bytes_towrite -= fdbuf_datan(fd->wbuf);
			fdbuf_destroy(fd->wbuf);
			fd->wbuf = NULL;
		}
		_in_fq(reactor, fd);
	}
}

int 
tcp_ev_cb(struct tcpreactor_t * reactor, int id, void * ud, tcp_ev_readcb rcb, tcp_ev_errorcb ecb, tcp_ev_expandrcache expandrcb, tcp_ev_expandwcache expandwcb) {
	struct fd_t * fd = _fd_valid(reactor, id);
	if (!fd) 
		return TCPERROR_FD;

	if (fd->state != FD_CONNECTED){
		//printf("TCP err _FDSTATE tcp_ev_cb d->state = %d != FD_CONNECTED\n", fd->state);
		return TCPERROR_FDSTATE;
	}

	fd->ud = ud;
	fd->rcb = rcb;
	fd->ecb = ecb;
	fd->can_expendrcb = expandrcb;
	fd->can_expendwcb = expandwcb;
	return 0;
}

int 
tcp_listen(struct tcpreactor_t * reactor, const char * ip, int port, void * ud, tcp_ev_acceptcb acb, tcp_ev_errorcb ecb) {
	if (!acb || !ecb)
		return TCPERROR_MISSING_CB;

	const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (0 >= fd) 
		return TCPERROR_CREATEFD;
	
	struct sockaddr_in my_addr;
	bzero(&my_addr, sizeof(my_addr));  
	my_addr.sin_family = AF_INET;  
	my_addr.sin_port = htons(port);  
	my_addr.sin_addr.s_addr = inet_addr(ip);//INADDR_ANY;
	int flag = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) != 0) {
		close(fd);
		return TCPERROR_SETOPT;
	}
	if (0 != bind(fd,  (struct sockaddr *)&my_addr,  sizeof(struct sockaddr))) {
		close(fd);
		return TCPERROR_BIND;
	}
	if (listen(fd, 128) != 0)	{
		close(fd);
		return TCPERROR_LISTEN;
	}
	if (0 != _set_nb(fd)) {
		close(fd);
		return TCPERROR_NB;
	}
	struct fd_t * fdt = _out_fq(reactor);
	if (!fdt) {
		close(fd);
		return TCPERROR_ALLOCFD;
	}
	fdt->fd = fd;
	fdt->state = FD_LISTENING;
	fdt->ud = ud;
	fdt->acb = acb;
	fdt->ecb = ecb;
	_ev_add(reactor, fdt);
	return fdt->id;
}

int 
tcp_connect(struct tcpreactor_t * reactor, const char * ip, int port, void * ud, tcp_ev_connectedcb ccb, tcp_ev_errorcb ecb) {
	if (!ccb || !ecb)
		return -11111;

	const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if(0 > fd)
		return TCPERROR_CREATEFD;
	
	if (0 != _set_nb(fd)) {
		close(fd);
		return TCPERROR_NB;
	}
	struct sockaddr_in s_add;
	bzero(&s_add, sizeof(s_add));  
	s_add.sin_family = AF_INET;  
	s_add.sin_port = htons(port);  
	s_add.sin_addr.s_addr = inet_addr(ip);//INADDR_ANY;

	unsigned char suc = 0;
	errno = 0;
	const int ret = connect(fd,(struct sockaddr *)(&s_add), sizeof(struct sockaddr));
	if (ret < 0) {
		if(errno == EINPROGRESS)
			suc = 1;//in progress
	}
	else if (0 == ret)
		suc = 1;//amazing, succ with no waitting
	
	if (suc == 1) {
		struct fd_t * fdt = _out_fq(reactor);
		if (!fdt) {
			close(fd);
			return TCPERROR_ALLOCFD;
		}
		fdt->fd = fd;
		fdt->state = FD_CONNECTTING;
		fdt->ud = ud;
		fdt->ccb = ccb;
		fdt->ecb = ecb;
		_ev_add(reactor, fdt);
		return fdt->id;
	}
	
	return TCPERROR_CONNECT;
}

int 
tcp_write(struct tcpreactor_t * reactor, int id, const char * data, int dlen, const char * data2, int dlen2) {
	struct fd_t * fd = _fd_valid(reactor, id);
	if (!fd)
		return TCPERROR_FD;

	if (fd->state != FD_CONNECTED){

		//printf("TCP err _FDSTATE tcp_write d->state = %d != FD_CONNECTED\n", fd->state);
		return TCPERROR_FDSTATE;
	}

	int need = 0;
	if (data) {
		if (dlen < 0)//==0 is ok
			return TCPERROR_DLEN1_ERROR;
		else
			need += dlen;
	}
	if (data2) {
		if (dlen2 < 0)// ==0 is ok
			return TCPERROR_DLEN2_ERROR;
		else
			need += dlen2;
	}
	if (need < 0)//too large
		return TCPERROR_DLEN_OVERFLOW;
	if (need == 0)
		return 0;//send no data
	if (!fd->wbuf) {
		if (fd->can_expendwcb && 0 != fd->can_expendwcb(fd->ud, fd->id, 0, need)) 
			return TCPERROR_WBUF_EXPAND_FAIL;

		struct fdbuf_t * buffer = fdbuf_create((need/1024+1)*1024);
		if (!buffer) {
			_in_bq(reactor, fd);
			return TCPERROR_WBUF_CREATE_FAIL;
		}

		fd->wbuf = buffer;
	}

	int fn = fdbuf_freen(fd->wbuf);
	assert(fd >= 0);
	if (fn < need) {
		_flush(reactor, fd);
		if (fd->state != FD_CONNECTED)
			return TCPERROR_FLUSH_ERROR;//error occur
		fn = fdbuf_freen(fd->wbuf);
	}

	assert(fd >= 0);
	if (fn < need) {
		int expand = (need - fn + 1024)/1024*1024;
		if (expand < 0)//too tlarge
			return TCPERROR_NEED_OVERFLOW;

		if (fd->can_expendwcb && 0 != fd->can_expendwcb(fd->ud, fd->id, fdbuf_cap(fd->wbuf), expand)) 
			return TCPERROR_WBUF_EXPAND_FAIL;

		int newcap = expand + fdbuf_cap(fd->wbuf);
		if (newcap < 0)//tootlarge
			return TCPERROR_NEWCAP_OVERFLOW;

		int err = fdbuf_expand(fd->wbuf, newcap);
		if (0 != err) {
			_in_bq(reactor, fd);
			return err;
		}
		fn = fdbuf_freen(fd->wbuf);
	}
	assert(fn >= need);
	if (data) {
		memcpy(fdbuf_freebegin(fd->wbuf), data, dlen);
		fdbuf_writen(fd->wbuf, dlen);
	}
	if (data2) {
		memcpy(fdbuf_freebegin(fd->wbuf), data2, dlen2);
		fdbuf_writen(fd->wbuf, dlen2);
	}
	if (fd->writable && !fd->w_node.inq)
		_in_wq(reactor, fd);

	reactor->bytes_towrite += dlen + dlen2;
	return 0;
}

int 
tcp_flush(struct tcpreactor_t * reactor, int id) {
	struct fd_t * fd = _fd_valid(reactor, id);
	if (!fd)
		return TCPERROR_FD;

	if (fd->state != FD_CONNECTED){
		//printf("TCP err _FDSTATE tcp_flush d->state = %d != FD_CONNECTED\n", fd->state);
		return TCPERROR_FDSTATE;

	}

	_flush(reactor, fd);
	return 0;
}

int 
tcp_kick(struct tcpreactor_t * reactor, int id) {
	struct fd_t * fd = _fd_valid(reactor, id);
	if (!fd)
		return TCPERROR_FD;

	if (fd->state == FD_FREE){
		//printf("TCP err _FDSTATE tcp_kick d->state = %d != FD_FREE\n", fd->state);
		return TCPERROR_FDSTATE;
	}
	if (fd->state == FD_BAD)
		return 0;

	_in_bq(reactor, fd);
	return 0;
}

int 
tcp_wsz(struct tcpreactor_t * reactor, int id, int * cap, int * dn) {
	struct fd_t * fd = _fd_valid(reactor, id);
	if (!fd)
		return TCPERROR_FD;

	if (fd->state == FD_FREE){
		//printf("TCP err _FDSTATE tcp_wsz d->state = %d != FD_FREE\n", fd->state);
		return TCPERROR_FDSTATE;

	}

	if (fd->state == FD_BAD)
		return 0;

	if (!fd->wbuf) {
		*cap = 0;
		*dn = 0;
	} else {
		*cap = fdbuf_cap(fd->wbuf);
		*dn = fdbuf_datan(fd->wbuf);
	}
	return 0;
}

static int 
_set_nb(int fd) {
	int flag = fcntl(fd, F_GETFL, 0);
	if (-1 == flag) return -1;
	fcntl(fd, F_SETFL, flag | O_NONBLOCK);
	return 0;
}

static struct fd_t * 
_fd_valid (struct tcpreactor_t * reactor, int id) {
	int idx = id;//%FD_MAX;
	if (idx > 0 && idx < reactor->fdsz) {
		if (reactor->fds[idx]->id == id)
			return reactor->fds[idx];
	}

	return NULL;
}

static void 
_in_fq(struct tcpreactor_t * reactor, struct fd_t * fd) {
	assert (!fd->f_node.inq);
	fdq_push_tail (&reactor->f_queue, &fd->f_node);
}

static void 
_in_bq(struct tcpreactor_t * reactor, struct fd_t * fd) {
	assert (!fd->b_node.inq);
	fd->state = FD_BAD;

	if (fd->r_node.inq)
		fdq_erase(&reactor->r_queue, &fd->r_node);
	assert(!fd->r_node.inq);
	if (fd->p_node.inq)
		fdq_erase(&reactor->p_queue, &fd->p_node);
	assert(!fd->p_node.inq);
	if (fd->w_node.inq)
		fdq_erase(&reactor->w_queue, &fd->w_node);
	assert (!fd->w_node.inq);
	assert (!fd->f_node.inq);
	fdq_push_tail(&reactor->b_queue, &fd->b_node);
	epoll_ctl(reactor->evfd, EPOLL_CTL_DEL, fd->fd, NULL);
}

static void 
_in_rq(struct tcpreactor_t * reactor, struct fd_t * fd) {
	if (!fd->r_node.inq)
		fdq_push_tail (&reactor->r_queue, &fd->r_node);
}

static void 
_in_pq(struct tcpreactor_t * reactor, struct fd_t * fd) {
	if (!fd->p_node.inq)
		fdq_push_tail (&reactor->p_queue, &fd->p_node);
}

static void 
_in_wq(struct tcpreactor_t * reactor, struct fd_t * fd) {
	if (!fd->w_node.inq)
		fdq_push_tail (&reactor->w_queue, &fd->w_node);
}

static struct fd_t * 
_out_fq(struct tcpreactor_t * reactor) {
	struct fdnode_t * node = fdq_pop_head(&reactor->f_queue);
	if (node) {
		struct fd_t * fd = node->fdt;
		assert(fd);
/*		int id = fd->id + FD_MAX;
		if (id < 0) {
			fd->id = fd->id % FD_MAX;
		} else
			fd->id = id;
*/
		return fd;
	}
	return NULL;
}

static struct epoll_event 
_fillev(struct fd_t * fd) {
	struct epoll_event ev;
	ev.events = 0;
	#ifdef _EPOLL_ET_
		ev.events |= EPOLLET;
	#endif
	ev.events |= EPOLLERR;
	ev.events |= EPOLLHUP;
	ev.data.ptr = fd;
	switch (fd->state) {
		case FD_LISTENING: {
			#ifdef _EPOLL_ET_
				ev.events |= EPOLLIN;
			#else
				ev.events |= EPOLLIN;
			#endif
			break;
		}
		case FD_CONNECTED: {
			#ifdef _EPOLL_ET_
				ev.events |= EPOLLIN;
				ev.events |= EPOLLOUT;
			#else
				ev.events |= EPOLLIN;
				if (!fd->writable)
					ev.events |= EPOLLOUT;
			#endif
			break;
		}
		case FD_CONNECTTING: {
			ev.events |= EPOLLOUT;
			break;
		}
		default: {
			assert(0);
		}
	}
	return ev;
}

static int 
_ev_add(struct tcpreactor_t * reactor, struct fd_t * fd) {
	struct epoll_event ev = _fillev(fd);
	//errno = 0;
	return epoll_ctl(reactor->evfd, EPOLL_CTL_ADD, fd->fd, &ev);
}

static void 
_ev_mod(struct tcpreactor_t * reactor, struct fd_t * fd) {
	struct epoll_event ev = _fillev(fd);
	//errno = 0;
	epoll_ctl(reactor->evfd, EPOLL_CTL_MOD, fd->fd, &ev);
}

int tcp_set_rbuf(struct tcpreactor_t * reactor, int id, int cap) {
	struct fd_t * fd = _fd_valid(reactor, id);
	if (!fd) return -1;
	if (fd->state != FD_CONNECTED) return -2;
	if (cap <= 0) return -3;

	if (!fd->rbuf) {
		fd->rbuf = fdbuf_create(cap);
		if (!fd->rbuf) return -4; 
	}

	if (fdbuf_cap(fd->rbuf) >= cap) return 0;
	if (fdbuf_expand(fd->rbuf, cap) == 0) return 0;
	return -5; 
}

int tcp_set_wbuf(struct tcpreactor_t * reactor, int id, int cap) {
	struct fd_t * fd = _fd_valid(reactor, id);
	if (!fd) return -1;
	if (fd->state != FD_CONNECTED) return -2;
	if (cap <= 0) return -3;

	if (!fd->wbuf) {
		fd->wbuf = fdbuf_create(cap);
		if (!fd->wbuf) return -4; 
	}

	if (fdbuf_cap(fd->wbuf) >= cap) return 0;
	if (fdbuf_expand(fd->wbuf, cap) == 0) return 0;
	return -5; 
}

const char * tcp_getpeername(struct tcpreactor_t * reactor, int id, int *err, int *port) {
	struct fd_t * fd = _fd_valid(reactor, id);
	if (!fd) {
		*err = TCPERROR_FD;
		return NULL;
	}
	if (fd->state != FD_CONNECTED) {
		//printf("TCP err _FDSTATE tcp_getpeername d->state = %d != FD_CONNECTED\n", fd->state);
		*err = TCPERROR_FDSTATE;
		return NULL;
	}
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	if (0 != getpeername(fd->fd, (struct sockaddr*)&addr, &addrlen)) {
		*err = TCPERROR_GETPEERNAME_ERROR;
		return NULL;
	}
	*err = 0;
	*port = (int)(ntohs(addr.sin_port));
	return inet_ntoa(addr.sin_addr);
}

const char * tcp_getsockname(struct tcpreactor_t * reactor, int id, int *err, int *port) {
	struct fd_t * fd = _fd_valid(reactor, id);
	if (!fd) {
		*err = TCPERROR_FD;
		return NULL;
	}
	if (fd->state != FD_CONNECTED) {
		//printf("TCP err _FDSTATE tcp_getsockname d->state = %d != FD_CONNECTED\n", fd->state);
		*err = TCPERROR_FDSTATE;
		return NULL;
	}
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	if (0 != getsockname(fd->fd, (struct sockaddr*)&addr, &addrlen)) {
		*err = TCPERROR_GETPEERNAME_ERROR;
		return NULL;
	}
	*err = 0;
	*port = (int)(ntohs(addr.sin_port));
	return inet_ntoa(addr.sin_addr);
}

uint32_t tcp_bytes_read(struct tcpreactor_t * reactor) {return reactor->bytes_read;}
uint32_t tcp_bytes_write(struct tcpreactor_t * reactor) {return reactor->bytes_write;}
uint32_t tcp_bytes_parse(struct tcpreactor_t * reactor) {return reactor->bytes_parse;}
uint32_t tcp_bytes_towrite(struct tcpreactor_t * reactor) {return reactor->bytes_towrite;}
uint32_t tcp_times_read(struct tcpreactor_t * reactor) {return reactor->times_read;}
uint32_t tcp_times_write(struct tcpreactor_t * reactor) {return reactor->times_write;}