#include "somgr.h"
#include "so_util.h"

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
	struct soqueue_t freesos;	//没用上的so
	struct soqueue_t badsos;	//待关闭的so
	struct soqueue_t writesos;	//待写的so
	void* ud;
	soacb acb;
	sorcb rcb;
	soecb ecb;
};

static void somgr_expand_sos(struct somgr_t* somgr);
struct so_t* somgr_alloc_so(struct somgr_t* somgr);
void somgr_remove_so(struct somgr_t* somgr, struct so_t* so);
int  somgr_add_so(struct somgr_t* somgr, struct so_t* so);
int  somgr_mod_so(struct somgr_t* somgr, struct so_t* so, int w);
void somgr_free_so(struct somgr_t* somgr, struct so_t* so);
static int so_set_noblock(struct so_t* so) ;
static void so_set_state(struct so_t* so, int sta);
static uint32_t so_get_state(struct so_t* so, int sta);

struct somgr_t* somgr_new(void* ud, soacb a, sorcb r, soecb e) {
	if (!a || !r || !e) return NULL;
	int ep = epoll_create(1024);
	if (ep <= 0) return NULL;
	struct somgr_t* somgr = (struct somgr_t*)malloc(sizeof(*somgr));
	if (somgr) {
		memset(somgr, 0, sizeof(*somgr));
		somgr->ep = ep;
		somgr->ud = ud;
		somgr->acb = a;
		somgr->rcb = r;
		somgr->ecb = e;
	}
	return somgr;
}

void somgr_destroy(struct somgr_t* somgr) {
	//TODO
}

int somgr_listen(struct somgr_t* somgr, const char* ip, int port) {
	int err = 0;
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (fd <= 0) return -6;

	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));  
	addr.sin_family = AF_INET;  
	addr.sin_port = htons(port);  
	addr.sin_addr.s_addr = inet_addr(ip);//INADDR_ANY;
	int flag = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) != 0) goto fail1;
	if (0 != bind(fd,  (struct sockaddr *)&addr,  sizeof(struct sockaddr))) goto fail2;
	if (listen(fd, 128) != 0) goto fail3;
	struct so_t* so = somgr_alloc_so(somgr);
	if (!so) goto fail4;
	
	so->fd = fd;
	so_set_state(so, SOS_LISTEN);
	if (somgr_add_so(somgr, so)) {
		somgr_free_so(somgr, so);
		return -7;
	}
	return so->id;
fail1: err = err !=0 ? err : -1;
fail2: err = err !=0 ? err : -2;
fail3: err = err !=0 ? err : -3;
fail4: err = err !=0 ? err : -4;
	close(fd);
	return err;
}

int somgr_flush_so(struct somgr_t* somgr, struct so_t* so) {
	uint32_t dn = 0;
	int wn = 0;
dowrite:
	dn = sbuf_cur(&so->wbuf);
	if (dn == 0) 
		return 0;
	if (!so_get_state(so, SOS_WRITABLE) || 0 == dn) return 0;
	wn = write(so->fd, so->wbuf.ptr, dn);
	if (wn > 0) {
		assert(dn >= wn);
		sbuf_readed(&so->wbuf, dn);
		goto dowrite;
	} else if (wn < 0) {
		switch (errno) {
			case EAGAIN:
				so->state &= ~(1<<SOS_WRITABLE);
				if (somgr_mod_so(somgr, so, 1))
					goto fail3;
				return 0;
			case EINTR:
				goto dowrite;
			default:
				goto fail1;
		}
	} else goto fail2;
fail1:
fail2:
fail3:
	return -1;
}

void somgr_proc_rw(struct somgr_t* somgr, struct so_t* so) {
	int rn = 0, pn = 0;
	uint32_t fz = 0;
	fz = sbuf_freesz(&so->rbuf);
	if (fz == 0) {
		if (sbuf_expand(&so->rbuf, so->rbuf.cap == 0? 1024 : so->rbuf.cap))
			goto fail1;
		fz = sbuf_freesz(&so->rbuf);
	}
	rn = read(so->fd, sbuf_cptr(&so->rbuf), fz);
	if (rn > 0) {
		assert(rn <= fz);
		sbuf_writed(&so->rbuf, rn);
		pn = somgr->rcb(somgr->ud, so->id, so->rbuf.ptr, so->rbuf.cur);
		if (pn < 0 || pn > so->rbuf.cur)  goto fail1;
		sbuf_readed(&so->rbuf, pn);
		if (so_get_state(so, SOS_BAD)) goto fail2;
		goto dowrite;
	} else if (rn < 0) {
		switch (errno) {
			case EAGAIN:
			case EINTR:
				return;
			default:
				goto fail3;
		}
	} else goto fail4;
dowrite:
	if (0 != somgr_flush_so(somgr, so))
		goto fail5;
	return;
fail1:
fail2:
fail3:
fail4:
fail5:
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
	so = somgr_alloc_so(somgr);
	if (!so) goto e_nullso;
	so->fd = fd;
	if (somgr_add_so(somgr, so)) {
		somgr_free_so(somgr, so);
		goto e_adderr;
	}
	somgr->acb(somgr->ud, lso->id, so->id);
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
	do {
		struct so_t* so = soqueue_pop(&somgr->badsos);
		if (!so) break;
		uint32_t soid = so->id;
		somgr->ecb(somgr->ud, soid);
		somgr_free_so(somgr, so);
	} while(1);

	do {
		struct so_t* so = soqueue_pop(&somgr->writesos);
		if (!so) break;
		if (somgr_flush_so(somgr, so)) {
			somgr_remove_so(somgr, so);
		} else {
			if (sbuf_cur(&so->wbuf) > 0) {
				assert(so_get_state(so, SOS_WRITABLE) == 0);
			} else {
				if (somgr_mod_so(somgr, so, 0))
					somgr_remove_so(somgr, so);
			}
		}
	} while (1);

	en = epoll_wait(somgr->ep, evs, 1024, wms);
	for (; i < en; i++) {
		struct so_t* so = evs[i].data.ptr;
		if (evs[i].events & (EPOLLHUP | EPOLLERR)) {
			somgr_remove_so(somgr, so);
		} else if (so_get_state(so, SOS_LISTEN)) {
			somgr_proc_accept(somgr, so);
		} else {
			somgr_proc_rw(somgr, so);
		}
	}
}

int somgr_write(struct somgr_t* somgr, int32_t id, char* data, uint32_t dlen) {
	if (dlen == 0) return 0;
	if (id < 1 || id >= somgr->sosn) return -1;
	struct so_t* so = somgr->sos[id];
	if (so_get_state(so, SOS_BAD | SOS_LISTEN | SOS_FREE)) return -2;
	uint32_t fz = sbuf_freesz(&so->wbuf);
	if (fz < dlen) {
		if (so_get_state(so, SOS_WRITABLE)) {
			if (0 != somgr_flush_so(somgr, so))
				goto fail1;
		}
		fz = sbuf_freesz(&so->wbuf);
		if (fz < dlen) {
			if (sbuf_expand(&so->wbuf, dlen - fz))
				goto fail2;
		}
	}
	memcpy(so->wbuf.ptr, data, dlen);
	sbuf_writed(&so->wbuf, dlen);
	if (!so_get_state(so, SOS_EV_WRITE)) 
		if (somgr_mod_so(somgr, so, 1))
			goto fail3;
	if (!so->curq)
		soqueue_push(&somgr->writesos, so);
	return 0;
fail1:
fail2:
fail3:
	somgr_remove_so(somgr, so);
	return -1;
}

int somgr_kick(struct somgr_t* somgr, int32_t id) {
	if (id < 1 || id >= somgr->sosn) return -1;
	struct so_t* so = somgr->sos[id];
	if (so_get_state(so, SOS_BAD)) return -2;
	if (so_get_state(so, SOS_FREE)) return -3;
	somgr_flush_so(somgr, so);
	somgr_remove_so(somgr, so);
	return 0;
}

/********************************************************************************************************************/
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
			struct so_t* so = (struct so_t*)malloc(sizeof(*so));
			memset(so, 0, sizeof(*so));
			so->id = i;
			so_set_state(so, SOS_FREE);
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
	if (so_get_state(so, SOS_BAD)) return;
	if (so->curq) {
		assert(so->curq == &somgr->writesos);
		soqueue_erase(so);
	}
	so_set_state(so, SOS_BAD);
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	int err = epoll_ctl(somgr->ep, EPOLL_CTL_DEL, so->fd, &ev);
	assert(err == 0);
	soqueue_push(&somgr->badsos, so);
}

int somgr_add_so(struct somgr_t* somgr, struct so_t* so) {
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events |= EPOLLERR | EPOLLHUP | EPOLLIN;
	so_set_state(so, SOS_WRITABLE);
	if (so_set_noblock(so)) 
		return -1;
	ev.data.ptr = so;
	if (epoll_ctl(somgr->ep, EPOLL_CTL_ADD, so->fd, &ev))
		return -2;
	return 0;
}

int somgr_mod_so(struct somgr_t* somgr, struct so_t* so, int w) {
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events |= EPOLLERR | EPOLLHUP | EPOLLIN;
	if (w) {//0 == so_get_state(so, SOS_WRITABLE)) {
		ev.events |= EPOLLOUT;
	}
	ev.data.ptr = so;
	if (epoll_ctl(somgr->ep, EPOLL_CTL_MOD, so->fd, &ev))
		return -1;
	if (w)
		so_set_state(so, SOS_EV_WRITE);
	else 
		so->state &= ~(1<<SOS_EV_WRITE);
	return 0;
}

void somgr_free_so(struct somgr_t* somgr, struct so_t* so) {
	if (so->fd)
		close(so->fd);
	so->fd = 0;
	so->state = 0;
	so_set_state(so, SOS_FREE);
	sbuf_reset(&so->rbuf);
	sbuf_reset(&so->wbuf);
	soqueue_push(&somgr->freesos, so);
}
/**/
int so_set_noblock(struct so_t* so) {
	int flag = fcntl(so->fd, F_GETFL, 0);
	if (-1 == flag) return -1;
	fcntl(so->fd, F_SETFL, flag | O_NONBLOCK);
	return 0;
}

void so_set_state(struct so_t* so, int sta) {
	so->state |= 1<<sta;
}

uint32_t so_get_state(struct so_t* so, int sta) {
	return so->state & 1<<sta;
}
