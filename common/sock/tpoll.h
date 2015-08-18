#ifndef __TPOLL_HEADER__
#define __TPOLL_HEADER__
#include <stdint.h>
#define uint32 uint32_t
#define int32 int32_t
struct tpoll_t;
int  tpoll_new();
void tpoll_delete(struct tpoll_t *poll);
int  tpoll_listen(struct tpoll_t *poll, const char *ip, int port);

//int  tpoll_write(struct tpoll_t *poll, int32 id, const char *data, size_t len);
//int  tpoll_read(stuct tpoll_t *poll, int32 id, char *buffer, size_t len);
#endif