#ifndef __SOMGR_HEADER__
#define __SOMGR_HEADER__
#include <stdint.h>

typedef int (*soacb) (void* ud, int lid, int nid);					//accept callback
typedef int (*sorcb) (void* ud, int id, char * data, int len);		//read callback
typedef int (*soecb) (void* ud, int id);							//error callback

struct somgr_t;

struct somgr_t* somgr_new(void* ud, soacb a, sorcb r, soecb e);
void somgr_destroy(struct somgr_t* somgr);
void somgr_runonce(struct somgr_t* somgr, int wms);
int somgr_listen(struct somgr_t* somgr, const char* ip, int port);
int somgr_write(struct somgr_t* somgr, int32_t id, char* data, uint32_t dlen);
int somgr_kick(struct somgr_t* somgr, int32_t id);
#endif