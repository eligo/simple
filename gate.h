#ifndef __GATE_HEADER__
#define __GATE_HEADER__

struct gate_t;
struct gsq_t;

struct gate_t * gate_new(int port, struct gsq_t * g2s_queue, struct gsq_t * s2g_queue);
void gate_delete(struct gate_t * gate);
void gate_runonce (struct gate_t * gate);
#endif