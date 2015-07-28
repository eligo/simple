#include "service.h"
#include "gsq.h"
#include "common/timer/timer.h"
#include "common/global.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
struct service_t {
	struct gsq_t* g2s_queue;
	struct gsq_t* s2g_queue;
	struct timer_t* timer;
	uint64_t tick;
	struct lua_State* lparser;	//lua 脚本解释器
	int qflag;
	int idx_accepted;	//accepted 回调方法在栈中的位置
	int idx_connected;
	int idx_closed;
	int idx_listened;
	int idx_data;
	int idx_timer;
};
static void l_on_tcp_accepted(struct service_t* service, struct g2s_tcp_accepted_t * ev);
static void l_on_tcp_closed(struct service_t* service, struct g2s_tcp_closed_t * ev);
static void l_on_tcp_data(struct service_t* service, struct g2s_tcp_data_t * ev);
static void l_on_timer(struct service_t* service, uint32_t tid, int erased);
static void l_on_tcp_connected(struct service_t* service, struct g2s_tcp_connected_t * ev);
static void l_on_tcp_listened(struct service_t* service, struct g2s_tcp_listened_t * ev);
static int c_listen(struct lua_State* lparser);
static int c_connect(struct lua_State* lparser);
static int c_send(struct lua_State* lparser);
static int c_close(struct lua_State* lparser);
static int c_timeout(struct lua_State* lparser);
static int c_unixtime(struct lua_State* lparser);
static int c_unixtime_ms(struct lua_State* lparser);
static void time_cb (void * ud, uint32_t tid, int erased);

struct service_t * service_new(struct gsq_t * g2s_queue, struct gsq_t * s2g_queue, const char* scriptpath) {
	struct service_t * service = (struct service_t*)MALLOC(sizeof(*service));
	if (service) {
		service->g2s_queue = g2s_queue;
		service->s2g_queue = s2g_queue;
		service->timer = timer_new(10*60*5);	//1/10秒精度的定时器, 缓存为5分钟(当然超出5分钟也是可以的)
		service->lparser = lua_open();
		service->tick = time_real_ms()/100;
		luaL_openlibs(service->lparser);
		//注入c接口
		if(luaL_dostring(service->lparser, "local class = require (\"lualib.class\") return class.singleton(\"external\")") != 0) {
			fprintf(stderr, "%s\n", lua_tostring(service->lparser, -1));
			goto fail;
		}
	#define INJECT_C_FUNC(func, name) lua_pushlightuserdata(service->lparser, service); lua_pushcclosure(service->lparser, func, 1); lua_setfield(service->lparser, -2, name);
		INJECT_C_FUNC(c_listen, "listen");
		INJECT_C_FUNC(c_connect, "connect");
		INJECT_C_FUNC(c_send, "send");
		INJECT_C_FUNC(c_close, "close");
		INJECT_C_FUNC(c_timeout, "timeout");
		INJECT_C_FUNC(c_unixtime, "unixtime");
		INJECT_C_FUNC(c_unixtime_ms, "unixms");
		//设置脚本搜索路径
		size_t plen=0;
		lua_getglobal(service->lparser, "package");
		lua_getfield(service->lparser, -1, "path");
		const char* path = luaL_checklstring(service->lparser, -1, &plen);
		char* npath = MALLOC(plen + strlen(scriptpath) + 8);
		sprintf(npath, "%s;%s/?.lua", path, scriptpath);
		lua_pushstring(service->lparser, npath);
		lua_setfield(service->lparser, -3, "path");
		FREE(npath);
		//加载lua脚本的首个文件(文件名已定死)
		char* loadf = MALLOC(strlen(scriptpath) + sizeof("/interface.lua") + 1);
		strcpy(loadf, scriptpath);
		strcat(loadf, "/interface.lua");
		if (luaL_dofile(service->lparser, loadf) != 0) {
			FREE(loadf);
			fprintf(stderr, "%s\n", lua_tostring(service->lparser, -1));
			goto fail;
		}
		FREE(loadf);
		//缓存lua层事件处理方法
	#define CACHE_L_EVHANDLE(name, idx) lua_getglobal(service->lparser, name); if (!lua_isfunction (service->lparser, -1)) {fprintf(stderr, "cannot find event handle'%s'",name);goto fail;} else {*idx=lua_gettop(service->lparser);}
		CACHE_L_EVHANDLE("c_onTcpAccepted", &service->idx_accepted);
		CACHE_L_EVHANDLE("c_onTcpConnected", &service->idx_connected);
		CACHE_L_EVHANDLE("c_onTcpClosed", &service->idx_closed);
		CACHE_L_EVHANDLE("c_onTcpListened", &service->idx_listened);
		CACHE_L_EVHANDLE("c_onTcpData", &service->idx_data);
		CACHE_L_EVHANDLE("c_onTimer", &service->idx_timer);
	}
	return service;
fail:
	if (service) {
		if (service->lparser)
			lua_close(service->lparser);
		FREE(service);
	}
	return NULL;
}

void service_delete(struct service_t * service) {
	if (service) {
		if (service->lparser) lua_close(service->lparser);
		if (service->timer) timer_destroy(service->timer);
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

void service_check_notify_gate_to_work(struct service_t* service) {
	if (service->qflag) {
		gsq_notify_g(service->g2s_queue);
		service->qflag = 0;
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
			break;
		}
		case G2S_TCP_CONNECTED: {
			struct g2s_tcp_connected_t* ev = (struct g2s_tcp_connected_t*)packet;
			l_on_tcp_connected(service, ev);
			break;
		}
		case G2S_TCP_LISTENED: {
			struct g2s_tcp_listened_t* ev = (struct g2s_tcp_listened_t*)packet;
			l_on_tcp_listened(service, ev);
			break;
		}
		default: {
			assert(0);
		}
		}
		FREE (packet);
		if (++count%100 == 0)	{					//100是经验值可换成别的, 每处理100个业务包就处理一下定时器
			service_tick(service, time_ms()/100);	//处理定时器	
		}
		service_check_notify_gate_to_work(service);
	}

	if (count) service_tick(service, time_ms()/100);
	service_check_notify_gate_to_work(service);
	gsq_notify_wait_g(service->g2s_queue, 100);
}

static int lua_error_cb (lua_State* L);

void l_on_tcp_accepted (struct service_t * service, struct g2s_tcp_accepted_t * ev) {
	struct lua_State * lparser = service->lparser;
	int st = lua_gettop(lparser);
	lua_pushcfunction(lparser, lua_error_cb);
	lua_pushvalue(lparser, service->idx_accepted);
	lua_pushnumber(lparser, ev->sid);
	lua_pcall(lparser, 1, 0, -3);
	lua_settop(lparser, st);
}

void l_on_tcp_connected (struct service_t * service, struct g2s_tcp_connected_t * ev) {
	struct lua_State * lparser = service->lparser;
	int st = lua_gettop(lparser);
	lua_pushcfunction(lparser, lua_error_cb);
	lua_pushvalue(lparser, service->idx_connected);
	lua_pushnumber(lparser, ev->sid);
	lua_pushnumber(lparser, ev->ud);
	lua_pcall(lparser, 2, 0, -4);
	lua_settop(lparser, st);
}

void l_on_tcp_listened(struct service_t* service, struct g2s_tcp_listened_t * ev) {
	struct lua_State * lparser = service->lparser;
	int st = lua_gettop(lparser);
	lua_pushcfunction(lparser, lua_error_cb);
	lua_pushvalue(lparser, service->idx_listened);
	lua_pushnumber(lparser, ev->sid);
	lua_pushnumber(lparser, ev->ud);
	lua_pcall(lparser, 2, 0, -4);
	lua_settop(lparser, st);
}

void l_on_tcp_closed (struct service_t * service, struct g2s_tcp_closed_t * ev) {
	struct lua_State * lparser = service->lparser;
	int st = lua_gettop(lparser);
	lua_pushcfunction(lparser, lua_error_cb);
	lua_pushvalue(lparser, service->idx_closed);
	lua_pushnumber(lparser, ev->sid);
	lua_pushnumber(lparser, ev->ud);
	lua_pcall(lparser, 2, 0, -4);
	lua_settop(lparser, st);
}

void l_on_tcp_data (struct service_t * service, struct g2s_tcp_data_t * ev) {
	struct lua_State * lparser = service->lparser;
	int st = lua_gettop(lparser);
	lua_pushcfunction(lparser, lua_error_cb);
	lua_pushvalue(lparser, service->idx_data);
	lua_pushnumber(lparser, ev->sid);
	lua_pushlstring(lparser, (char*)ev+sizeof(*ev), ev->dlen);
	lua_pcall(lparser, 2, 0, -4);
	lua_settop(lparser, st);
}

void l_on_timer(struct service_t * service, uint32_t tid, int erased) {
	struct lua_State * lparser = service->lparser;
	int st = lua_gettop(lparser);
	lua_pushcfunction(lparser, lua_error_cb);
	lua_pushvalue(lparser, service->idx_timer);
	lua_pushnumber(lparser, tid);
	lua_pushnumber(lparser, erased);
	lua_pcall(lparser, 2, 0, -4);
	lua_settop(lparser, st);
}

int c_listen(struct lua_State * lparser) {
	size_t len = 0;
	struct service_t * service = lua_touserdata(lparser,lua_upvalueindex(1));
	int ui = luaL_checkinteger(lparser, 1);
	const char* ip = luaL_checklstring(lparser, 2, &len);
	int port = luaL_checkinteger(lparser, 3);
	struct s2g_tcp_listen* ev = (struct s2g_tcp_listen*)MALLOC(sizeof(*ev));
	ev->ud = ui;
	ev->ip = (char*)MALLOC(len+1);
	strcpy(ev->ip, ip);
	ev->port = port;
	gsq_push (service->s2g_queue, S2G_TCP_LISTEN, ev);
	service->qflag = 1;
	return 0;
}

int c_connect(struct lua_State * lparser) {
	size_t len = 0;
	struct service_t * service = lua_touserdata(lparser,lua_upvalueindex(1));
	int ui = luaL_checkinteger(lparser, 1);
	const char* ip = luaL_checklstring(lparser, 2, &len);
	int port = luaL_checkinteger(lparser, 3);
	struct s2g_tcp_connect* ev = (struct s2g_tcp_connect*)MALLOC(sizeof(*ev));
	ev->ud = ui;
	ev->ip = (char*)MALLOC(len+1);
	strcpy(ev->ip, ip);
	ev->port = port;
	gsq_push (service->s2g_queue, S2G_TCP_CONNECT, ev);
	service->qflag = 1;
	return 0;
}

int c_send (struct lua_State * lparser) {
	size_t len = 0;
	struct service_t * service = lua_touserdata(lparser,lua_upvalueindex(1));
	const int sid = luaL_checkinteger(lparser, 1); 
	const char * data = luaL_checklstring(lparser, 2, &len);
	struct s2g_tcp_data_t * ev = (struct s2g_tcp_data_t*)MALLOC(sizeof(*ev) + len);
	ev->sid = sid;
	ev->dlen = len;
	memcpy ((char*)ev+sizeof(*ev), data, len);
	gsq_push (service->s2g_queue, S2G_TCP_DATA, ev);
	service->qflag = 1;
	return 0;
}

int c_close (struct lua_State * lparser) {
	struct service_t * service = lua_touserdata(lparser,lua_upvalueindex(1));
	const int sid = luaL_checkinteger(lparser, 1); 
	struct s2g_tcp_close_t * ev = (struct s2g_tcp_close_t*)MALLOC(sizeof(*ev));
	ev->sid = sid;
	gsq_push (service->s2g_queue, S2G_TCP_CLOSE, ev);
	service->qflag = 1;
	return 0;
}

void time_cb (void * ud, uint32_t tid, int erased) {
	l_on_timer((struct service_t*)ud, tid, erased);
}

int c_timeout (struct lua_State * lparser) {
	struct service_t * service = lua_touserdata(lparser,lua_upvalueindex(1));
	uint32_t timeout = luaL_checkinteger(lparser, 1);
	int32_t  repeate = luaL_checkinteger(lparser, 2);
	uint32_t tid = timer_add(service->timer, timeout, service, time_cb, repeate);
	lua_pushinteger(lparser, tid);
	return 1;
}

int c_unixtime(struct lua_State* lparser) {
	uint32_t unixtime = time_unixtime();
	lua_pushinteger(lparser, unixtime);
	return 1;
}

int c_unixtime_ms(struct lua_State* lparser) {
	double ms = (double)time_ms();
	lua_pushnumber(lparser, ms);
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