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

int main() {
	sigset_t old_mask;
	sigset_t new_mask;
	signal(SIGPIPE,SIG_IGN);	//忽略该信号
	sigfillset(&new_mask);
	sigdelset(&new_mask, 2);
	sigdelset(&new_mask, 15);
	sigdelset(&new_mask, SIGSEGV);
	pthread_sigmask(SIG_BLOCK, &new_mask, &old_mask);
	signal(2, signal_handler);//设置进程信号处理
	signal(15, signal_handler);//设置进程信号处理
	signal(SIGTERM, signal_handler);//设置进程信号处理
	signal(SIGSEGV, signal_handler);//设置进程信号处理

	pthread_t gate_thread;
	pthread_t service_thread;
	struct gsq_t * g2s_queue = gsq_new();						//消息队列
	struct gsq_t * s2g_queue = gsq_new();						//消息队列
	struct gate_t * gate = gate_new(9999, g2s_queue, s2g_queue);//创建 gate 模块, 负责用户接入
	if (!gate) {
		fprintf(stderr, "gate fail\n");
		return -1;
	}

	struct service_t * service = service_new(g2s_queue, s2g_queue);		//service 模块, 负责业务处理, gate 把客户端消息通过消息队列 g2s_qeueue 传递给 service 模块处理
	if (!service) {
		fprintf(stderr, "service fail\n");
		return -2;
	}

	pthread_create(&gate_thread, NULL, gate_runable, gate);				//创建一个线程服务于 gate 模块
	pthread_create(&service_thread, NULL, service_runable, service);	//创建一个线程用于 service 模块
	do {
		sleep(1);
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
		usleep(1000*50);	//service 时间精度为 1/10 秒, 这里也睡眠1/10秒
	} 
	while (!_stop);

	return NULL;
}

void signal_handler(int signal) {
	_stop = 1;
}
