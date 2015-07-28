#include "somgr.h"
#include "so_util.h"
#include "../global.h"
#include <unistd.h>  
#include <errno.h> 
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/socket.h>  
#include <sys/epoll.h>
#include <fcntl.h>
struct somgr_t {
	int ep;
	struct so_t** sos;
	uint32_t sosn;
	struct soqueue_t freesos;	//备用的socket, 可以重新分配的
	struct soqueue_t badsos;	//待关闭的socket, 已经被踢掉或者已经出错的
	struct soqueue_t writesos;	//待写的socket, 能写并且有数据要写的
	void* ud;	//用户数据
	soacb acb;	//accepted 回调
	sorcb rcb;	//readed 回调
	soecb ecb;	//errored 回调
	soccb ccb;	//connected 回调用
	int notify[2];				//socket pair for job-notify between 2 threads
	volatile int waitnotify;	//1 means other thread is watting for somgr
	volatile int waitting;		//1 means somgr is blocking in epoll_wait
};
static void somgr_expand_sos(struct somgr_t* somgr);	//扩展连接上下文池
struct so_t* somgr_alloc_so(struct somgr_t* somgr);		//从池拿出一个上下文
void somgr_remove_so(struct somgr_t* somgr, struct so_t* so);	//从epoll移出一个连接
int somgr_add_so(struct somgr_t* somgr, struct so_t* so);		//向epoll加入一个连接
int somgr_mod_so(struct somgr_t* somgr, struct so_t* so, int w);//从epoll修改某个连接
void somgr_free_so(struct somgr_t* somgr, struct so_t* so);		//释放某连接
static int fd_setnoblock(int fd);			//把文件描述符设置成非堵塞
static int so_setnoblock(struct so_t* so);	//把连接设置成非堵塞(同上)
static void so_setstate(struct so_t* so, int sta);		//设置连接状态
static uint32_t so_hasstate(struct so_t* so, int sta);	//判断某连接是否有某状态
static void so_clearstate(struct so_t* so, int sta);	//清除某连接某个状态

struct somgr_t* somgr_new(void* ud, soacb a, sorcb r, soecb e, soccb c) {
	int ep = 0;
	int notify[2] = {0,0};
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events |= EPOLLIN;
	if (!a || !r || !e || !c) 
		return NULL;
	
	ep = epoll_create(1024);//创建epoll设备(百度linux epoll)
	if (ep <= 0) 
		goto fail;
	
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, notify) < 0) 
		goto fail;			//创建线程通知用的一对套接字
	
	if (epoll_ctl(ep, EPOLL_CTL_ADD, notify[0], &ev)) 
		goto fail;

	fd_setnoblock(notify[0]);
	fd_setnoblock(notify[1]);
	struct somgr_t* somgr = (struct somgr_t*)MALLOC(sizeof(*somgr));
	memset(somgr, 0, sizeof(*somgr));
	somgr->ep = ep;
	somgr->ud = ud;
	somgr->acb = a;
	somgr->rcb = r;
	somgr->ecb = e;
	somgr->ccb = c;
	somgr->notify[0] = notify[0];
	somgr->notify[1] = notify[1];
	return somgr;
fail:
	if (ep) close(ep);
	if (notify[0]) close(notify[0]);
	if (notify[1]) close(notify[1]);
	return NULL;
}

void somgr_destroy(struct somgr_t* somgr) {
	int i = 0;
	for ( ; i<somgr->sosn; ++i) {
		struct so_t* so = somgr->sos[i];
		if (so) {
			if (so->fd)	close(so->fd);
			sbuf_reset(&so->rbuf);
			sbuf_reset(&so->wbuf);
			FREE(so);
		}
	}
	if (somgr->sos)
		FREE(somgr->sos);
	close(somgr->ep);
	close(somgr->notify[0]);
	close(somgr->notify[1]);
	FREE(somgr);
}

int somgr_listen(struct somgr_t* somgr, const char* ip, int port) {
	int err = 0;
	struct sockaddr_in addr;
	int flag = 1;
	struct so_t *so = NULL;
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (fd <= 0) 
		return -1;

	bzero(&addr, sizeof(addr));  
	addr.sin_family = AF_INET;  
	addr.sin_port = htons(port);  
	addr.sin_addr.s_addr = inet_addr(ip);//INADDR_ANY;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) != 0) 
		goto fail;
	
	if (0 != bind(fd,  (struct sockaddr *)&addr,  sizeof(struct sockaddr))) 
		goto fail;
	
	if (listen(fd, 128) != 0) 
		goto fail;
	
	so = somgr_alloc_so(somgr);
	if (!so) 
		goto fail;
	
	so->fd = fd;
	so_setstate(so, SOS_LISTEN);	//添加listen标志
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
	struct sockaddr_in addr;
	int ret = 0;
	struct so_t *so = NULL;
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (0 > fd)	
		return -1;
	
	if (fd_setnoblock(fd)) 
		goto fail;		//设置成非堵塞
	
	bzero(&addr, sizeof(addr));  
	addr.sin_family = AF_INET;  
	addr.sin_port = htons(port);  
	addr.sin_addr.s_addr = inet_addr(ip);//INADDR_ANY;
	errno = 0;
	ret = connect(fd, (struct sockaddr *)(&addr), sizeof(struct sockaddr));	//这步不会引起堵塞(因为前面fd_setnoblock)
	if (ret < 0) {
		if(errno != EINPROGRESS) 
			goto fail;	//EINPROGRESS表示连接中
	}
	else if (0 != ret) 
		goto fail;

	so = somgr_alloc_so(somgr);
	if (!so) 
		goto fail;
	
	so->fd = fd;
	so_setstate(so, SOS_CONNECTTING);//添加状态"正在连接"
	if (somgr_add_so(somgr, so)) {	 //加入epoll(仅加入可写事件,事件发生说明可以查询是否连接成功)
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
	if (dn == 0) 
		return 0;
	
	wn = write(so->fd, so->wbuf.ptr, dn);	//调用系统api把数据从本地缓冲区写到系统缓冲区
	if (wn > 0) {
		assert(dn >= wn);
		sbuf_readed(&so->wbuf, wn);			//维护本地缓冲状态
		goto dowrite;
	} else if (wn < 0) {
		switch (errno) {
		case EAGAIN:	//写不进了, 对方接收过慢会产生这种情况(tcp滑动窗口机制)
			return 0;
		case EINTR:		//被系统中断打断, 可继续尝试
			goto dowrite;
		}
		goto fail;		//肯定有错误发生了
	} else 
		goto fail;
fail:
	return -1;
}

void somgr_proc_connected(struct somgr_t* somgr, struct so_t* so) {
	int err = -1;
	socklen_t len = sizeof(err);
	if (0 == getsockopt(so->fd, SOL_SOCKET, SO_ERROR, &err, &len) && err == 0) {	//连接过程中没有错误发生, 说明连接上了
		if (somgr_mod_so(somgr, so, 0))	
			goto fail; 									//重置感兴趣的事件(读事件)
	} else 
		goto fail;

	so_setstate(so, SOS_WRITABLE);			//这个时候可以认为该socket可写
	so_clearstate(so, SOS_CONNECTTING);		//清除'正在连接'状态
	somgr->ccb(somgr->ud, so->id, so->ud);	//通知上层连接成功
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
			if (sbuf_expand(&so->rbuf, so->rbuf.cap == 0? 1024 : so->rbuf.cap))	
				goto fail; //扩展接收缓冲区
			
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
			if (so_hasstate(so, SOS_BAD)) 
				goto fail;	//因为rcb可能会把该socket kick 掉， 所以检查一下是有必要的
		} else if (rn < 0) {
			switch (errno) {
			case EAGAIN:	//没有内容可读
			case EINTR:		//读的过程中被系统中断, 可以下次再重试操作
				break;
			}
			goto fail;
		} else 
			goto fail;
	}

	if (ev & EPOLLOUT) {						//该socket此刻可写
		assert(!so_hasstate(so, SOS_WRITABLE));
		assert(!so->curq);						//肯定不在待写队列
		so_setstate(so, SOS_WRITABLE);			//设置标记该socket可写
		if (0 != somgr_flush_so(somgr, so)) 
			goto fail;		//可写的时候把还没有发送的内容刷到系统缓冲区
		if (sbuf_cur(&so->wbuf) == 0) {			//数据全发出去了
			if (somgr_mod_so(somgr, so, 0)) 
				goto fail;	//重写设置感兴趣的事件(取消可写事件)
		} else {								//依然有数据没推出, 说明状态又变成了不可写
			so_clearstate(so, SOS_WRITABLE);	//设置成不可写, 保留事件侦听
		}
	}
	
	return;
fail:
	somgr_remove_so(somgr, so);
}

void somgr_proc_accept(struct somgr_t* somgr, struct so_t* lso) {
	struct so_t* so = NULL;
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);
	int fd = accept(lso->fd, &addr, &addrlen);//TODO try more times
	if (fd == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
		case EMFILE:
			return;					//可以留到下一次尝试
		}
		goto errfd;
	} else if (fd == 0)
		goto errfd;
	
	so = somgr_alloc_so(somgr);		//尝试分配一个上下文来存放socket信息
	if (!so) 
		goto errso;
	
	so->fd = fd;
	if (somgr_add_so(somgr, so)) {	//把新socket加入epoll
		somgr_free_so(somgr, so);
		goto errfd;
	}

	somgr->acb(somgr->ud, lso->id, so->id);	//回调上层有新连接到达
	return;
errso:
	close(fd);
	return;
errfd:
	somgr_remove_so(somgr, so);
}

void somgr_runonce(struct somgr_t* somgr, int wms) {
	struct epoll_event evs[1024];
	int en = 0;
	int i = 0;
	for(;;){	//处理坏掉的socket
		struct so_t* so = soqueue_pop(&somgr->badsos);
		if (!so) break;
		uint32_t soid = so->id;
		somgr->ecb(somgr->ud, soid, so->ud);
		somgr_free_so(somgr, so);
	}

	for(;;) {	//处理有数据要发送且当前状态为可写的socket
		struct so_t* so = soqueue_pop(&somgr->writesos);
		if (!so) break;
		if (somgr_flush_so(somgr, so)) {	 //发送(也就把数据拷贝到系统缓冲区,系统啥时候发就啥时候发,用户程序无法干预)
			somgr_remove_so(somgr, so);
		} else if (sbuf_cur(&so->wbuf) > 0) {//还有数据没推出去说明该socket变成不可写了
			so_clearstate(so, SOS_WRITABLE); //设置成不可写
			if (somgr_mod_so(somgr, so, 1))	 //重写设置感兴趣的事件(加入可写事件)
				somgr_remove_so(somgr, so);
		}
	}
	
	somgr->waitting = 1;						//标志somgr当前正在查询, 这个状态不用非常严格, 仅在唤醒这一块有一点用
	en = epoll_wait(somgr->ep, evs, 1024, wms);	//查询epoll里面所有socket事件(最多1024个,epoll内部会有排队机制,一次拿不完,多次肯定可以拿完)
	somgr->waitting = 0;						//取消正在查询状态
	for (; i < en; i++) {
		struct so_t* so = evs[i].data.ptr;
		if (!so) {
			char data[1];
			read(somgr->notify[0], data, sizeof(data));
		} else if (evs[i].events & (EPOLLHUP | EPOLLERR)) {
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
	uint32_t fz = 0;
	if (dlen == 0) 
		return 0;
	
	if (id < 1 || id >= somgr->sosn) 
		return -1;

	struct so_t* so = somgr->sos[id];
	if (so_hasstate(so, SOS_BAD | SOS_LISTEN | SOS_FREE | SOS_CONNECTTING)) 
		return -2;
	
	fz = sbuf_freesz(&so->wbuf);
	if (fz < dlen) {													//本地缓存放不下
		if (so_hasstate(so, SOS_WRITABLE) && sbuf_cur(&so->wbuf) > 0) {	//如果有机会发送一些
			if (0 != somgr_flush_so(somgr, so))	
				goto fail;				        //尝试发送一些，好挪出一点本地缓存
			
			if (sbuf_cur(&so->wbuf) > 0) {		//还有数据没推完, 说明变成不可写了
				so_clearstate(so, SOS_WRITABLE);//置成不可写状态
				if (somgr_mod_so(somgr, so, 1)) 
					goto fail;				    //重新设置该so感兴趣的事件(read write)
			}
			
			fz = sbuf_freesz(&so->wbuf);
		}

		if (fz < dlen) {	//空间还是不够
			if (sbuf_expand(&so->wbuf, dlen - fz)) 
				goto fail;	//只好扩展本地缓存空间了, TODO 扩展内存上限
		}
	}
	memcpy(sbuf_cptr(&so->wbuf), data, dlen);	//仅拷贝到本地缓存而不立刻发送(累多点一次性发, 是为了优化调用write的次数)
	sbuf_writed(&so->wbuf, dlen);				//维护本地缓存
	if (so_hasstate(so, SOS_WRITABLE)) {		//如果so可写
		if (!so->curq)							//又不在待写队列
			soqueue_push(&somgr->writesos, so); //则加入待写队列, 待写队列会在下一帧再真正发送这些数据
		else
			assert(&somgr->writesos == so->curq);
	} else {									
		if (so->curq) {							//有可能本来是可写的又在待写队列, 现在不可写了, 要拿出队列
			assert(&somgr->writesos == so->curq);
			soqueue_erase(so);
		}
	}
	return 0;
fail:
	if (so->curq) {
		assert(&somgr->writesos == so->curq);
		soqueue_erase(so);
	}
	somgr_remove_so(somgr, so);
	return -1;
}

int somgr_kick(struct somgr_t* somgr, int32_t id) {
	struct so_t *so = NULL;
	if (id < 1 || id >= somgr->sosn) 
		return -1;
	
	so = somgr->sos[id];
	if (so_hasstate(so, SOS_BAD)) 
		return -2;

	if (so_hasstate(so, SOS_FREE)) 
		return -3;
	
	somgr_flush_so(somgr, so);	//踢之前尽量刷一下数据
	somgr_remove_so(somgr, so);
	return 0;
}

void somgr_expand_sos(struct somgr_t* somgr) {
	uint32_t i = 0;
	struct so_t **sos = NULL;
	uint32_t sosn = somgr->sosn == 0? 2 : somgr->sosn * 2;
	if (sosn > 0x0fffffff) 
		return;

	sos = (struct so_t**)realloc(somgr->sos, sizeof(*sos)*sosn);
	if (!sos) 
		return;

	i = somgr->sosn;
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
	struct so_t *so = soqueue_pop(&somgr->freesos);
	if (!so) {
		somgr_expand_sos(somgr);
		so = soqueue_pop(&somgr->freesos);
	}
	so->state = 0;
	return so;
}

void somgr_remove_so(struct somgr_t* somgr, struct so_t* so) {
	struct epoll_event ev;
	if (so_hasstate(so, SOS_BAD)) 
		return;
	
	if (so->curq) {
		assert(so->curq == &somgr->writesos);
		soqueue_erase(so);
	}

	memset(&ev, 0, sizeof(ev));
	so_setstate(so, SOS_BAD);
	epoll_ctl(somgr->ep, EPOLL_CTL_DEL, so->fd, &ev);//从epoll移出(之后epoll不会查询到任何关于该socket的事件)
	soqueue_push(&somgr->badsos, so);
}

int somgr_add_so(struct somgr_t* somgr, struct so_t* so) {
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events |= EPOLLERR | EPOLLHUP;
	if (so_hasstate(so, SOS_CONNECTTING)) {	//对于正在连接中的socket, 只对可写感兴趣
		ev.events |= EPOLLOUT;
	} else {
		ev.events |= EPOLLIN;
		so_setstate(so, SOS_WRITABLE);		//对于别的正常socket， 只对可读取感兴趣
		if (so_setnoblock(so)) 
			return -1;
	}
	
	ev.data.ptr = so;
	if (epoll_ctl(somgr->ep, EPOLL_CTL_ADD, so->fd, &ev)) 
		return -2;

	return 0;
}

int somgr_mod_so(struct somgr_t* somgr, struct so_t* so, int w) {//修改感兴趣的事件, w是否对可写事件感兴趣
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events |= EPOLLERR | EPOLLHUP | EPOLLIN;
	if (w) 
		ev.events |= EPOLLOUT;

	ev.data.ptr = so;
	if (epoll_ctl(somgr->ep, EPOLL_CTL_MOD, so->fd, &ev)) 
		return -1;
	
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
	if (-1 == flag) 
		return -1;
	
	fcntl(fd, F_SETFL, flag | O_NONBLOCK);
	return 0;
}

int so_setnoblock(struct so_t* so) {			//设置socket描述符为非阻塞(read, write, connect 操作不会堵塞线程)
	return fd_setnoblock(so->fd);
}

void so_setstate(struct so_t* so, int sta) {	//设置状态
	so->state |= sta;
}

uint32_t so_hasstate(struct so_t* so, int sta) {//是否具备某个状态
	return so->state & sta;
}

void so_clearstate(struct so_t* so, int sta) {	//清除某个状态
	so->state &= ~(sta);
}

void somgr_notify_s(struct somgr_t* somgr) {	//唤醒
	if (somgr->waitnotify)
		write(somgr->notify[0], "a", 1);
}

void somgr_notify_g(struct somgr_t* somgr) {	//唤醒
	if (somgr->waitting)
		write(somgr->notify[1], "b", 1);
}

void somgr_notify_wait_g(struct somgr_t* somgr, int ms) {//等待超时/唤醒
	char data[1];
	struct timeval timeout={0,ms*1000};
	fd_set fds;
	int fd = somgr->notify[1];
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	somgr->waitnotify = 1;
	if (0 < select(fd+1, &fds, NULL, NULL, &timeout))
		read(fd, data, sizeof(data));
	somgr->waitnotify = 0;
}