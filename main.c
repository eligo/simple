//工作模式(半同步半异步,一线程收发,一线程处理业务)
#include "gate.h"
#include "service.h"
#include "gsq.h"
#include <pthread.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "common/timer/timer.h"
//#include "common/net/net.h"
static void* _gate(void * ptr);		//gate线程入口
static void* _serv(void * ptr);		//service线程入口
static void _sighdl(int signal); 	//进程信号回调
static int _stop = 0;				//是否结束标志
int main(int nargs, char** args) {
	pthread_t gate_thread;			//gate线程句柄
	pthread_t service_thread;		//service线程句柄
	struct sigaction sa1, sa2;
	sigemptyset(&sa1.sa_mask);
	sigemptyset(&sa2.sa_mask);
	sa1.sa_handler = SIG_IGN;		//忽略该信号
	sa2.sa_handler = _sighdl;		//设置信号处理方法
	sigaction(SIGPIPE, &sa1, 0);	//屏蔽SIGPIPE信号, 这种信号相当容易发生, 写一个对方已经close掉的socket时会产生, 俗称管道破裂
	sigaction(SIGHUP, &sa1, 0);		//crt异常关闭时会产生这个信号
	sigaction(SIGINT, &sa2, 0);		//添加SIGINT(ctrl + c) SIGTERM (kill pid) 信号处理
	sigaction(SIGTERM, &sa2, 0);
	
	if (nargs < 2) {
		fprintf(stderr, "missing script path, such as : ./simple test\n");
		return -1;
	}
	struct gsq_t * g2s_queue = gsq_new();	//消息队列 gate -> service
	struct gsq_t * s2g_queue = gsq_new();	//消息队列 service -> gate
	time_global_reset();					//初始化框架时间
	struct gate_t * gate = gate_new(g2s_queue, s2g_queue);					//创建gate模块(数据收发)
	if (!gate) {
		fprintf(stderr, "gate fail\n");
		return -2;
	}
	struct service_t * service = service_new(g2s_queue, s2g_queue, args[1]);//创建service模块(业务逻辑处理)
	if (!service) {
		fprintf(stderr, "service fail\n");
		return -3;
	}
	gsq_set_gs(g2s_queue, gate, service);
	gsq_set_gs(s2g_queue, gate, service);
	pthread_create(&gate_thread, NULL, _gate, gate);	  //创建一个线程服务于 gate 模块
	pthread_create(&service_thread, NULL, _serv, service);//创建一个线程用于 service 模块
	for(;!_stop;) {				//收到关闭信号会让_stop变成1
		usleep(1000);			//TODO 定时监控一下 gate service 运行是否健康
		fflush(stdout);
		time_global_reset();	//主线程1毫秒重置一下全局当前时间,以便业务可以拿到更真实的时间
	}							//TODO 应该向业务发一条关闭通知,以便业务做好数据保存之类的工作
	pthread_join(gate_thread, NULL);	//等待线程结束
	pthread_join(service_thread, NULL);	//等待线程结束
	service_delete(service);			//销毁service模块
	gate_delete(gate);					//销毁gate模块
	gsq_delete(g2s_queue);				//销毁消息队列
	gsq_delete(s2g_queue);				//销毁消息队列
	return 0;							//程序结束
}

void* _gate(void * ptr) {		 //驱动 gate 进行工作
	struct gate_t * gate = (struct gate_t *) ptr;
	for (;!_stop;)
		gate_runonce(gate);		 //TODO: break while until gate safe exit
	return NULL;
}

void* _serv(void * ptr) {		 //驱动 service 进行工作
	struct service_t * service = (struct service_t *) ptr;
	for (;!_stop;)
		service_runonce(service);//TODO: break while until service safe exit
	return NULL;
}

void _sighdl(int signal) {
	printf("recv signal %d\n", signal);
	_stop = 1;
}
