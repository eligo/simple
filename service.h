//业务处理模块
#ifndef __SREVICE_HEADER__
#define __SREVICE__HEADER__

struct service_t;
struct gsq_t;
struct service_t * service_new(struct gsq_t * g2s_queue, struct gsq_t * s2g_queue);	//创建服务
void service_delete(struct service_t * service);									//销毁服务
void service_runonce(struct service_t * service);									//服务驱动
#endif