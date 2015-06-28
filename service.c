#include "service.h"
#include "gsq.h"
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

static int c_sendsocket (struct lua_State * lparser);
static int c_closesocket (struct lua_State * lparser);

struct service_t {
	struct gsq_t * g2s_queue;
	struct gsq_t * s2g_queue;
	struct lua_State * lparser;	//lua 脚本解释器
};

struct service_t * service_new(struct gsq_t * g2s_queue, struct gsq_t * s2g_queue) {
	struct service_t * service = (struct service_t*) malloc(sizeof(*service));
	if (service) {
		service->g2s_queue = g2s_queue;
		service->s2g_queue = s2g_queue;
		service->lparser = lua_open();
		luaL_openlibs(service->lparser);
		lua_pushstring(service->lparser, "service_ptr");
		lua_pushlightuserdata (service->lparser, (void *)service);
		lua_settable(service->lparser, LUA_REGISTRYINDEX);
		if(luaL_dofile(service->lparser, "scripts/interface.lua") != 0) {
			fprintf(stderr, "%s\n", lua_tostring(service->lparser, -1));
			lua_close(service->lparser);
			free (service);
			return NULL;
		}

		struct luaL_Reg c_interface[] = {
			{"c_sendsocket", c_sendsocket},
			{"c_closesocket", c_closesocket},
			{NULL, NULL}
		};
		luaL_register(service->lparser, "c_interface", c_interface);
	}

	return service;
}

void service_delete(struct service_t * service) {
	if (service) {
		if (service->lparser)
			lua_close(service->lparser);
		free(service);
	}
}

void service_runonce(struct service_t * service) {
	do {
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
				free(ev->data);
				break;
			}
			default: {
				assert(0);
			}
		}
		free (packet);
	} while (1);
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

void l_on_tcp_closed (struct service_t * service, struct g2s_tcp_closed_t * ev) {
	struct lua_State * lparser = service->lparser;
	int st = lua_gettop(lparser);
	lua_pushcfunction(lparser, lua_error_cb);
	lua_getglobal(lparser, "c_onTcpClosed");
	lua_pushnumber(lparser, ev->sid);
	lua_pcall(lparser, 1, 0, -3);
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

int c_sendsocket (struct lua_State * lparser) {
	int st = lua_gettop(lparser);
	size_t len = 0;
	lua_pushstring(lparser, "service_ptr");
	lua_gettable(lparser, LUA_REGISTRYINDEX);
	assert(lua_islightuserdata(lparser, -1));
	struct service_t * service = (struct service_t *)lua_touserdata(lparser, -1);
	assert(service != NULL);
	const int sid = luaL_checkinteger(lparser, 1); 
	const char * data = luaL_checklstring(lparser, 2, &len);
	struct s2g_tcp_data_t * ev = (struct s2g_tcp_data_t*) malloc (sizeof(*ev));
	ev->sid = sid;
	ev->data = (char*) malloc (len);
	ev->dlen = len;
	memcpy (ev->data, data, len);
	gsq_push (service->s2g_queue, S2G_TCP_DATA, ev);
	lua_settop(lparser, st);
	return 0;
}

int c_closesocket (struct lua_State * lparser) {
	int st = lua_gettop(lparser);
	lua_pushstring(lparser, "service_ptr");
	lua_gettable(lparser, LUA_REGISTRYINDEX);
	assert(lua_islightuserdata(lparser, -1));
	struct service_t * service = (struct service_t *)lua_touserdata(lparser, -1);
	assert(service != NULL);
	const int sid = luaL_checkinteger(lparser, 1); 
	struct s2g_tcp_close_t * ev = (struct s2g_tcp_close_t*) malloc (sizeof(*ev));
	ev->sid = sid;
	gsq_push (service->s2g_queue, S2G_TCP_CLOSE, ev);
	lua_settop(lparser, st);
	return 0;
}

static int lua_error_cb(lua_State *L) {
    lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    lua_getfield(L, -1, "traceback");
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    fprintf(stderr, "\n%s\n\n", lua_tostring(L, -1));
    return 1;
}