#include "service.h"
#include "gsq.h"
#include "common/timer/timer.h"
#include "common/global.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

static void l_on_tcp_accepted(struct service_t * service, struct g2s_tcp_accepted_t * ev);
static void l_on_tcp_closed(struct service_t * service, struct g2s_tcp_closed_t * ev);
static void l_on_tcp_data(struct service_t * service, struct g2s_tcp_data_t * ev);
static void l_on_timer(struct service_t * service, uint32_t tid, int erased);
static void l_on_tcp_connected(struct service_t * service, struct g2s_tcp_connected_t * ev);

static int c_connect(struct lua_State * lparser);
static int c_send(struct lua_State * lparser);
static int c_close(struct lua_State * lparser);
static int c_timeout(struct lua_State * lparser);

static void time_cb (void * ud, uint32_t tid, int erased);

struct service_t {
	struct gsq_t * g2s_queue;
	struct gsq_t * s2g_queue;
	struct timer_t * timer;
	uint64_t tick;
	struct lua_State * lparser;	//lua 脚本解释器
};

struct service_t * service_new(struct gsq_t * g2s_queue, struct gsq_t * s2g_queue) {
	struct service_t * service = (struct service_t*)MALLOC(sizeof(*service));
	if (service) {
		service->g2s_queue = g2s_queue;
		service->s2g_queue = s2g_queue;
		service->timer = timer_new(10*60*5);	//1/10秒精度的定时器, 缓存为5分钟(当然超出30分钟也是可以的)
		service->lparser = lua_open();
		service->tick = time_real_ms()/100;
		luaL_openlibs(service->lparser);
		lua_pushstring(service->lparser, "service_ptr");
		lua_pushlightuserdata (service->lparser, (void *)service);
		lua_settable(service->lparser, LUA_REGISTRYINDEX);
		struct luaL_Reg c_interface[] = {
			{"c_connect", c_connect},
			{"c_send", c_send},
			{"c_close", c_close},
			{"c_timeout", c_timeout},
			{NULL, NULL}
		};
		luaL_register(service->lparser, "c_interface", c_interface);
		if (luaL_dofile(service->lparser, "scripts/interface.lua") != 0) {
			fprintf(stderr, "%s\n", lua_tostring(service->lparser, -1));
			lua_close(service->lparser);
			FREE (service);
			return NULL;
		}
	}
	return service;
}

void service_delete(struct service_t * service) {
	if (service) {
		if (service->lparser)
			lua_close(service->lparser);
		FREE(service);
	}
}

void service_tick(struct service_t* service, uint64_t ctick) {
	if (ctick < service->tick)	//被意外修改过系统时间(向后修改)
		service->tick = ctick;

	uint64_t cost = ctick - service->tick;
	if (cost > 0) {
		while (cost-- > 0) {
			timer_tick(service->timer);
		}
		service->tick = ctick;
	}
}

void service_runonce(struct service_t * service) {
	uint32_t count = 0;
	service_tick(service, time_ms()/100);	//处理定时器
	while (1) {	//处理业务
		int type = 0;
		void * packet = gsq_pop(service->g2s_queue, &type);
		if (!packet) break;
		switch (type) {
			case G2S_TCP_ACCEPTED: {
				struct g2s_tcp_accepted_t * ev = (struct g2s_tcp_accepted_t*)packet;
				l_on_tcp_accepted(service, ev);
				break;
			}
			case G2S_TCP_CLOSED: {
				struct g2s_tcp_closed_t * ev = (struct g2s_tcp_closed_t*)packet;
				l_on_tcp_closed(service, ev);
				break;
			}
			case G2S_TCP_DATA: {
				struct g2s_tcp_data_t * ev = (struct g2s_tcp_data_t*)packet;
				l_on_tcp_data(service, ev);
				FREE(ev->data);
				break;
			}
			case G2S_TCP_CONNECTED: {
				struct g2s_tcp_connected_t* ev = (struct g2s_tcp_connected_t*)packet;
				l_on_tcp_connected(service, ev);
				break;
			}
			default: {
				assert(0);
			}
		}
		FREE (packet);
		if (++count%100 == 0)	//100是经验值可换成别的, 每处理100个业务包就处理一下定时器
			service_tick(service, time_ms()/100);	//处理定时器	
	}

	if (count) service_tick(service, time_ms()/100);
}

static int lua_error_cb (lua_State* L);

void l_on_tcp_accepted (struct service_t * service, struct g2s_tcp_accepted_t * ev) {
	struct lua_State * lparser = service->lparser;
	int st = lua_gettop(lparser);
	lua_pushcfunction(lparser, lua_error_cb);
	lua_getglobal(lparser, "c_onTcpAccepted");
	lua_pushnumber(lparser, ev->sid);
	lua_pcall(lparser, 1, 0, -3);
	lua_settop(lparser, st);
}

void l_on_tcp_connected (struct service_t * service, struct g2s_tcp_connected_t * ev) {
	struct lua_State * lparser = service->lparser;
	int st = lua_gettop(lparser);
	lua_pushcfunction(lparser, lua_error_cb);
	lua_getglobal(lparser, "c_onTcpConnected");
	lua_pushnumber(lparser, ev->sid);
	lua_pushnumber(lparser, ev->ud);
	lua_pcall(lparser, 2, 0, -4);
	lua_settop(lparser, st);
}

void l_on_tcp_closed (struct service_t * service, struct g2s_tcp_closed_t * ev) {
	struct lua_State * lparser = service->lparser;
	int st = lua_gettop(lparser);
	lua_pushcfunction(lparser, lua_error_cb);
	lua_getglobal(lparser, "c_onTcpClosed");
	lua_pushnumber(lparser, ev->sid);
	lua_pushnumber(lparser, ev->ud);
	lua_pcall(lparser, 2, 0, -4);
	lua_settop(lparser, st);
}

void l_on_tcp_data (struct service_t * service, struct g2s_tcp_data_t * ev) {
	struct lua_State * lparser = service->lparser;
	int st = lua_gettop(lparser);
	lua_pushcfunction(lparser, lua_error_cb);
	lua_getglobal(lparser, "c_onTcpData");
	lua_pushnumber(lparser, ev->sid);
	lua_pushlstring(lparser, ev->data, ev->dlen);
	lua_pcall(lparser, 2, 0, -4);
	lua_settop(lparser, st);
}

void l_on_timer(struct service_t * service, uint32_t tid, int erased) {
	struct lua_State * lparser = service->lparser;
	int st = lua_gettop(lparser);
	lua_pushcfunction(lparser, lua_error_cb);
	lua_getglobal(lparser, "c_onTimer");
	lua_pushnumber(lparser, tid);
	lua_pushnumber(lparser, erased);
	lua_pcall(lparser, 2, 0, -4);
	lua_settop(lparser, st);
}

static struct service_t * get_service(struct lua_State * lparser) {
	lua_pushstring(lparser, "service_ptr");
	lua_gettable(lparser, LUA_REGISTRYINDEX);
	assert(lua_islightuserdata(lparser, -1));
	struct service_t * service = (struct service_t *)lua_touserdata(lparser, -1);
	assert(service != NULL);
	return service;
}

int c_connect(struct lua_State * lparser) {
	size_t len = 0;
	struct service_t * service = get_service(lparser);

	int ui = luaL_checkinteger(lparser, 1);
	const char* ip = luaL_checklstring(lparser, 2, &len);
	int port = luaL_checkinteger(lparser, 3);
	struct s2g_tcp_connect* ev = (struct s2g_tcp_connect*)MALLOC(sizeof(*ev));
	ev->ud = ui;
	ev->ip = (char*)MALLOC(len+1);
	strcpy(ev->ip, ip);
	ev->port = port;
	gsq_push (service->s2g_queue, S2G_TCP_CONNECT, ev);
	return 0;
}

int c_send (struct lua_State * lparser) {
	size_t len = 0;
	struct service_t * service = get_service(lparser);
	const int sid = luaL_checkinteger(lparser, 1); 
	const char * data = luaL_checklstring(lparser, 2, &len);
	struct s2g_tcp_data_t * ev = (struct s2g_tcp_data_t*)MALLOC(sizeof(*ev));
	ev->sid = sid;
	ev->data = (char*)MALLOC(len);
	ev->dlen = len;
	memcpy (ev->data, data, len);
	gsq_push (service->s2g_queue, S2G_TCP_DATA, ev);
	return 0;
}

int c_close (struct lua_State * lparser) {
	struct service_t * service = get_service(lparser);
	const int sid = luaL_checkinteger(lparser, 1); 
	struct s2g_tcp_close_t * ev = (struct s2g_tcp_close_t*)MALLOC(sizeof(*ev));
	ev->sid = sid;
	gsq_push (service->s2g_queue, S2G_TCP_CLOSE, ev);
	return 0;
}

void time_cb (void * ud, uint32_t tid, int erased) {
	l_on_timer((struct service_t*)ud, tid, erased);
}

int c_timeout (struct lua_State * lparser) {
	struct service_t * service = get_service(lparser);
	uint32_t timeout = luaL_checkinteger(lparser, 1);
	int32_t  repeate = luaL_checkinteger(lparser, 2);
	uint32_t tid = timer_add(service->timer, timeout, service, time_cb, repeate);
	lua_pushinteger(lparser, tid);
	return 1;
}

int lua_error_cb(lua_State *L) {
    lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    lua_getfield(L, -1, "traceback");
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    fprintf(stderr, "\n%s\n\n", lua_tostring(L, -1));
    return 1;
}