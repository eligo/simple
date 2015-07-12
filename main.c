#include "gate.h"
#include "service.h"
#include "gsq.h"
#include <pthread.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "common/timer/timer.h"

static void * gate_runable(void * ptr);		//
static void * service_runable(void * ptr);	//
static void signal_handler(int signal); 	//进程信号回调
static int _stop = 0;	//是否结束标志

int main (int nargs, char** args) {
	struct sigaction sa1;
	sa1.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa1, 0);		//屏蔽SIGPIPE信号

	struct sigaction sa2;
	sa2.sa_handler = signal_handler;
	sigaction(SIGINT, &sa2, 0);			//添加SIGINT(ctrl + c) SIGTERM (kill pid) 信号处理
	sigaction(SIGTERM, &sa2, 0);

	pthread_t gate_thread;
	pthread_t service_thread;
	struct gsq_t * g2s_queue = gsq_new();						//消息队列
	struct gsq_t * s2g_queue = gsq_new();						//消息队列
	int port = 9999;
	if (nargs > 1)
		port = atoi(args[1]);

	time_global_reset();
	struct gate_t * gate = gate_new(port, g2s_queue, s2g_queue);//创建 gate 模块, 负责用户接入
	if (!gate) {
		fprintf(stderr, "gate fail\n");
		return -1;
	} else {
		if (port)
			fprintf(stderr, "server listen on port:%d\n", port);
	}

	struct service_t * service = service_new(g2s_queue, s2g_queue);		//service 模块, 负责业务处理, gate 把客户端消息通过消息队列 g2s_qeueue 传递给 service 模块处理
	if (!service) {
		fprintf(stderr, "service fail\n");
		return -2;
	}

	pthread_create(&gate_thread, NULL, gate_runable, gate);				//创建一个线程服务于 gate 模块
	pthread_create(&service_thread, NULL, service_runable, service);	//创建一个线程用于 service 模块
	do {
		usleep(1000);
		time_global_reset();
						//TODO 定时监控一下 gate service 运行是否健康
	} while(!_stop);	//收到关闭信号会让_stop变成1

	pthread_join(gate_thread, NULL);	//等待线程结束
	pthread_join(service_thread, NULL);	//等待线程结束
	
	service_delete(service);			//销毁 service 模块
	gate_delete(gate);					//销毁 gate 模块
	gsq_delete(g2s_queue);				//销毁消息队列
	gsq_delete(s2g_queue);				//销毁消息队列
	return 0;							//程序结束
}

void* gate_runable(void * ptr) {				//驱动 gate 进行工作
	struct gate_t * gate = (struct gate_t *) ptr;
	do {
		gate_runonce(gate);
	} 
	while (!_stop);
	
	return NULL;
}

void* service_runable(void * ptr) {			//驱动 service 进行工作
	struct service_t * service = (struct service_t *) ptr;
	do {
		service_runonce(service);
		usleep(1000*50);
	} 
	while (!_stop);

	return NULL;
}

void signal_handler(int signal) {
	_stop = 1;
}
