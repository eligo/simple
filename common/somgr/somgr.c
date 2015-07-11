#include "somgr.h"
#include "so_util.h"
#include "../global.h"

#include <stdio.h>  
#include <unistd.h>  
#include <errno.h> 
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>  
#include <sys/un.h>  
#include <sys/types.h>  
#include <sys/wait.h>  
#include <sys/epoll.h>
#include <fcntl.h>

struct somgr_t {
	int ep;
	struct so_t** sos;
	uint32_t sosn;
	struct soqueue_t freesos;	//没用上的
	struct soqueue_t badsos;	//待关闭的socket
	struct soqueue_t writesos;	//待写的socket
	void* ud;
	soacb acb;
	sorcb rcb;
	soecb ecb;
	soccb ccb;
};

static void somgr_expand_sos(struct somgr_t* somgr);
struct so_t* somgr_alloc_so(struct somgr_t* somgr);
void somgr_remove_so(struct somgr_t* somgr, struct so_t* so);
int somgr_add_so(struct somgr_t* somgr, struct so_t* so);
int somgr_mod_so(struct somgr_t* somgr, struct so_t* so, int w);
void somgr_free_so(struct somgr_t* somgr, struct so_t* so);
static int fd_setnoblock(int fd);
static int so_setnoblock(struct so_t* so);
static void so_setstate(struct so_t* so, int sta);
static uint32_t so_hasstate(struct so_t* so, int sta);
static void so_clearstate(struct so_t* so, int sta);

struct somgr_t* somgr_new(void* ud, soacb a, sorcb r, soecb e, soccb c) {
	if (!a || !r || !e || !c) 
		return NULL;
	
	int ep = epoll_create(1024);
	if (ep <= 0) return NULL;
	struct somgr_t* somgr = (struct somgr_t*)MALLOC(sizeof(*somgr));
	if (somgr) {
		memset(somgr, 0, sizeof(*somgr));
		somgr->ep = ep;
		somgr->ud = ud;
		somgr->acb = a;
		somgr->rcb = r;
		somgr->ecb = e;
		somgr->ccb = c;
	}
	return somgr;
}

void somgr_destroy(struct somgr_t* somgr) {
	//TODO
}

int somgr_listen(struct somgr_t* somgr, const char* ip, int port) {
	int err = 0;
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (fd <= 0) 
		return -6;

	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));  
	addr.sin_family = AF_INET;  
	addr.sin_port = htons(port);  
	addr.sin_addr.s_addr = inet_addr(ip);//INADDR_ANY;
	int flag = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) != 0) goto fail;
	if (0 != bind(fd,  (struct sockaddr *)&addr,  sizeof(struct sockaddr))) goto fail;
	if (listen(fd, 128) != 0) goto fail;
	struct so_t* so = somgr_alloc_so(somgr);
	if (!so) goto fail;
	
	so->fd = fd;
	so_setstate(so, SOS_LISTEN);
	if (somgr_add_so(somgr, so)) {
		somgr_free_so(somgr, so);
		return -7;
	}
	return so->id;
fail: 
	err = err !=0 ? err : -1;
	close(fd);
	return err;
}

int somgr_connect(struct somgr_t* somgr, const char* ip, int port, int ud) {
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (0 > fd)	
		return -1;

	if (fd_setnoblock(fd)) 
		goto fail;
	
	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));  
	addr.sin_family = AF_INET;  
	addr.sin_port = htons(port);  
	addr.sin_addr.s_addr = inet_addr(ip);//INADDR_ANY;
	errno = 0;
	int ret = connect(fd, (struct sockaddr *)(&addr), sizeof(struct sockaddr));
	if (ret < 0) {
		if(errno != EINPROGRESS)
			goto fail;
	}
	else if (0 != ret)
		goto fail;
	
	struct so_t* so = somgr_alloc_so(somgr);
	if (!so) 
		goto fail;
	
	so->fd = fd;
	so_setstate(so, SOS_CONNECTTING);
	if (somgr_add_so(somgr, so)) {
		somgr_free_so(somgr, so);
		goto fail;
	}
	so->ud = ud;
	return so->id;
fail:
	close(fd);
	return -2;
}

int somgr_flush_so(struct somgr_t* somgr, struct so_t* so) {
	uint32_t dn = 0;
	int wn = 0;
dowrite:
	dn = sbuf_cur(&so->wbuf);
	if (dn == 0) {
		if (so_hasstate(so, SOS_WRITABLE) && so_hasstate(so, SOS_EV_WRITE)) {
			if (somgr_mod_so(somgr, so, 0))	//没有数据可写 就取消写事件侦听, 否则会一直触发影响性能
				goto fail;
		}
		return 0;
	}
	
	wn = write(so->fd, so->wbuf.ptr, dn);	//调用系统api把数据写到系统缓冲区
	if (wn > 0) {
		assert(dn >= wn);
		sbuf_readed(&so->wbuf, wn);
		goto dowrite;
	} else if (wn < 0) {
		switch (errno) {
			case EAGAIN:	//写不进了, 对方接收过慢会产生这种情况(tcp滑动窗口机制)
				so_clearstate(so, SOS_WRITABLE); //取消可写标志
				if (somgr_mod_so(somgr, so, 1))	//侦听可写事件
					goto fail;
				return 0;
			case EINTR:		//被系统中断打断, 可继续尝试
				goto dowrite;
			default:		//肯定有错误发生了
				goto fail;
		}
	} else goto fail;
fail:
	return -1;
}

void somgr_proc_connected(struct somgr_t* somgr, struct so_t* so) {
	int err = -1;
	socklen_t len = sizeof(err);
	if (0 == getsockopt(so->fd, SOL_SOCKET, SO_ERROR, &err, &len) && err == 0) {
		if (somgr_mod_so(somgr, so, 0))
			goto fail;
	} else 
		goto fail;

	so_setstate(so, SOS_WRITABLE);
	so_clearstate(so, SOS_CONNECTTING);
	somgr->ccb(somgr->ud, so->id, so->ud);
	return;
fail:
	somgr_remove_so(somgr, so);
}

void somgr_proc_rw(struct somgr_t* somgr, struct so_t* so, unsigned ev) {	//处理读写事件
	int rn = 0, pn = 0;
	uint32_t fz = 0;
	if (ev & EPOLLIN) {		//可读
		fz = sbuf_freesz(&so->rbuf);
		if (fz == 0) {
			if (sbuf_expand(&so->rbuf, so->rbuf.cap == 0? 1024 : so->rbuf.cap))	//扩展接收缓冲区
				goto fail;
			fz = sbuf_freesz(&so->rbuf);
		}
		rn = read(so->fd, sbuf_cptr(&so->rbuf), fz);	//操作系统读取调用
		if (rn > 0) {
			assert(rn <= fz);
			sbuf_writed(&so->rbuf, rn);
			pn = somgr->rcb(somgr->ud, so->id, so->rbuf.ptr, so->rbuf.cur);
			if (pn < 0 || pn > so->rbuf.cur)  
				goto fail;
			sbuf_readed(&so->rbuf, pn);
			if (so_hasstate(so, SOS_BAD)) //因为rcb可能会把该socket kick 掉， 所以检查一下是有必要的
				goto fail;
		} else if (rn < 0) {
			switch (errno) {
				case EAGAIN:	//没有内容可读
				case EINTR:		//读的过程中被系统中断, 可以下次再重试操作
					return;
				default:
					goto fail;
			}
		} else goto fail;
	}

	if (ev & EPOLLOUT) {	//可写
		so_setstate(so, SOS_WRITABLE);	//设置状态 标记该socket可写
		if (0 != somgr_flush_so(somgr, so))		//可写的时候把还没有发送的内容刷到系统缓冲区
			goto fail;
	}
			
	return;
fail:
	somgr_remove_so(somgr, so);
}

void somgr_proc_accept(struct somgr_t* somgr, struct so_t* lso) {
	struct so_t* so = NULL;
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);
	int fd = accept(lso->fd, &addr, &addrlen);//TODO try nore times
	if (fd == -1) {
		switch (errno) {
			case EINTR:
			case EAGAIN:
			case EMFILE:
				return;
			default:
				goto e_fderr1;
		}
	} else if (fd == 0)
		goto e_fderr2;
	so = somgr_alloc_so(somgr);		//尝试分配一个上下文来存放socket信息
	if (!so) goto e_nullso;
	so->fd = fd;
	if (somgr_add_so(somgr, so)) {	//把socket加入epoll
		somgr_free_so(somgr, so);
		goto e_adderr;
	}
	somgr->acb(somgr->ud, lso->id, so->id);	//回调上层
	return;
e_nullso:
	close(fd);
	return;
e_fderr1:
e_fderr2:
e_adderr:
	somgr_remove_so(somgr, so);
}

void somgr_runonce(struct somgr_t* somgr, int wms) {
	struct epoll_event evs[1024];
	int en = 0;
	int i = 0;
	do {	//处理坏掉的socket
		struct so_t* so = soqueue_pop(&somgr->badsos);
		if (!so) break;
		uint32_t soid = so->id;
		somgr->ecb(somgr->ud, soid, so->ud);
		somgr_free_so(somgr, so);
	} while(1);

	do {	//处理有数要发送且当前状态为可写的socket
		struct so_t* so = soqueue_pop(&somgr->writesos);
		if (!so) break;
		if (somgr_flush_so(somgr, so)) {
			somgr_remove_so(somgr, so);
		} else {
			if (sbuf_cur(&so->wbuf) > 0) {
				assert(so_hasstate(so, SOS_WRITABLE) == 0);
			} else {
				if (somgr_mod_so(somgr, so, 0))
					somgr_remove_so(somgr, so);
			}
		}
	} while (1);

	en = epoll_wait(somgr->ep, evs, 1024, wms);	//查询epoll里面所有socket事件(未必是全部,epoll内部会有排队机制,一次拿不完,多次肯定可以拿完)
	for (; i < en; i++) {
		struct so_t* so = evs[i].data.ptr;
		if (evs[i].events & (EPOLLHUP | EPOLLERR)) {
			somgr_remove_so(somgr, so);
		} else if (so_hasstate(so, SOS_LISTEN)) {
			somgr_proc_accept(somgr, so);
		} else {
			if (so_hasstate(so, SOS_CONNECTTING)) {
				assert(evs[i].events & EPOLLOUT);
				somgr_proc_connected(somgr, so);
			} else
				somgr_proc_rw(somgr, so, evs[i].events);
		}
	}
}

int somgr_write(struct somgr_t* somgr, int32_t id, char* data, uint32_t dlen) {
	if (dlen == 0) return 0;
	if (id < 1 || id >= somgr->sosn) return -1;
	struct so_t* so = somgr->sos[id];
	if (so_hasstate(so, SOS_BAD | SOS_LISTEN | SOS_FREE)) return -2;
	uint32_t fz = sbuf_freesz(&so->wbuf);
	if (fz < dlen) {
		if (so_hasstate(so, SOS_WRITABLE)) {
			if (0 != somgr_flush_so(somgr, so))
				goto fail;
		}
		fz = sbuf_freesz(&so->wbuf);
		if (fz < dlen) {
			if (sbuf_expand(&so->wbuf, dlen - fz))
				goto fail;
		}
	}
	memcpy(sbuf_cptr(&so->wbuf), data, dlen);
	sbuf_writed(&so->wbuf, dlen);
	if (so_hasstate(so, SOS_WRITABLE)) {		//当前为可写状态
		if (!so->curq)							//如果不在待写队列,则加入
			soqueue_push(&somgr->writesos, so);
	} else if (!so_hasstate(so, SOS_EV_WRITE)) {	//如果当前不可写且没有侦听可写事件
		if (so->curq)
			soqueue_erase(so);
		if (somgr_mod_so(somgr, so, 1))
			goto fail;
	} 
	return 0;
fail:
	somgr_remove_so(somgr, so);
	return -1;
}

int somgr_kick(struct somgr_t* somgr, int32_t id) {
	if (id < 1 || id >= somgr->sosn) return -1;
	struct so_t* so = somgr->sos[id];
	if (so_hasstate(so, SOS_BAD)) return -2;
	if (so_hasstate(so, SOS_FREE)) return -3;
	somgr_flush_so(somgr, so);
	somgr_remove_so(somgr, so);
	return 0;
}

void somgr_expand_sos(struct somgr_t* somgr) {
	uint32_t sosn = somgr->sosn == 0? 2 : somgr->sosn * 2;
	if (sosn > 0x0fffffff) return;
	struct so_t** sos = (struct so_t**)realloc(somgr->sos, sizeof(*sos)*sosn);
	if (!sos) return;

	uint32_t i = somgr->sosn;
	for (; i < sosn; ++i) {
		if (i == 0) {
			sos[i] = NULL;
		} else {
			struct so_t* so = (struct so_t*)MALLOC(sizeof(*so));
			memset(so, 0, sizeof(*so));
			so->id = i;
			so_setstate(so, SOS_FREE);
			soqueue_push(&somgr->freesos, so);
			sos[i] = so;
		}
	}
	somgr->sos = sos;
	somgr->sosn = sosn;
}

struct so_t* somgr_alloc_so(struct somgr_t* somgr) {
	struct so_t* so = soqueue_pop(&somgr->freesos);
	if (!so) {
		somgr_expand_sos(somgr);
		so = soqueue_pop(&somgr->freesos);
	}
	so->state = 0;
	return so;
}

void somgr_remove_so(struct somgr_t* somgr, struct so_t* so) {
	if (so_hasstate(so, SOS_BAD)) return;
	if (so->curq) {
		assert(so->curq == &somgr->writesos);
		soqueue_erase(so);
	}
	so_setstate(so, SOS_BAD);
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	epoll_ctl(somgr->ep, EPOLL_CTL_DEL, so->fd, &ev);
	soqueue_push(&somgr->badsos, so);
}

int somgr_add_so(struct somgr_t* somgr, struct so_t* so) {
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events |= EPOLLERR | EPOLLHUP;
	if (so_hasstate(so, SOS_CONNECTTING)) {
		ev.events |= EPOLLOUT;
	} else {
		ev.events |= EPOLLIN;
		so_setstate(so, SOS_WRITABLE);
		if (so_setnoblock(so)) 
			return -1;
	}
	ev.data.ptr = so;
	if (epoll_ctl(somgr->ep, EPOLL_CTL_ADD, so->fd, &ev))
		return -2;
	return 0;
}

int somgr_mod_so(struct somgr_t* somgr, struct so_t* so, int w) {
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events |= EPOLLERR | EPOLLHUP | EPOLLIN;
	if (w) {
		ev.events |= EPOLLOUT;
	}
	ev.data.ptr = so;
	if (epoll_ctl(somgr->ep, EPOLL_CTL_MOD, so->fd, &ev))
		return -1;
	if (w)
		so_setstate(so, SOS_EV_WRITE);
	else 
		so_clearstate(so, SOS_EV_WRITE);
	return 0;
}

void somgr_free_so(struct somgr_t* somgr, struct so_t* so) {
	if (so->fd)
		close(so->fd);
	so->fd = 0;
	so->state = 0;
	so->ud = 0;
	so_setstate(so, SOS_FREE);
	sbuf_reset(&so->rbuf);
	sbuf_reset(&so->wbuf);
	soqueue_push(&somgr->freesos, so);
}

int fd_setnoblock(int fd) {
	int flag = fcntl(fd, F_GETFL, 0);
	if (-1 == flag) return -1;
	fcntl(fd, F_SETFL, flag | O_NONBLOCK);
	return 0;
}

int so_setnoblock(struct so_t* so) {
	return fd_setnoblock(so->fd);
	return 0;
}

void so_setstate(struct so_t* so, int sta) {
	so->state |= 1<<sta;
}

uint32_t so_hasstate(struct so_t* so, int sta) {
	return so->state & 1<<sta;
}

void so_clearstate(struct so_t* so, int sta) {
	so->state &= ~(1<<sta);
}