--这个文件主要实现供底层驱动上层的函数(c_onXxxx这种)
package.cpath = string.format("%s;%s?.so", package.cpath, './3rd/luaso/')	--设置外部c库的搜索路径
local class = require("luautil.class")	--类管理器
require("luautil.timer")	
local timer = class.singleton("timer")	--定时器
local json = require ('cjson')			--json 工具(encode decode)
local responser = {_sid=nil}			--简单封装一些操作
local handlers = class.singleton("protocol_handlers")	--协议处理集合
require("scripts.handle.test_handle")					--协议处理实现
require("luautil.mysql")
---------------------------------------------------------framework event---------------------------------------------------------
function c_onTcpAccepted(sid)				--框架事件(连接接受)
	print("c_onTcpAccepted", sid)
end

function c_onTcpConnected(sid, ud)			--框架时间(连接成功)
	print("c_onTcpConnected", sid, ud)
end

function c_onTcpClosed(sid, ud)				--框架事件(连接断开)
	print("c_onTcpClosed", sid, ud)
end

function c_onTcpData(sid, str)				--框架事件(连接业务数据到达)
	responser._sid = sid
	local ok, package = pcall(json.decode, str)
	if not ok then
		responser:write("server only accept json and end with \\r\\n !!!			")
		return
	end
	
	if not package.op then
		responser:write(string.format("json missing field 'op' !!!			"))
		return
	end

	local handler = handlers[package.op]
	if handler then
		handler(package, responser)
	else
		responser:write(string.format("no handle for op:%s !!!			", package.op))
	end
end

function c_onTimer(tid, erased)				--框架事件(某定时器到期触发) 定时器使用如 timer:timeout(1, 100, function() print("hello") end) 每1个滴答调用一下func, 重复100下
	timer:onTimer(tid, erased)
end

---------------------------------------------------------other---------------------------------------------------------
function responser:write (content)
	local ty = type (content)
	if ty == 'table' then
		c_interface.c_send(self._sid, json.encode(content))
	elseif ty == 'string' then
		c_interface.c_send(self._sid, content)
	end
end

function responser:closeconnect ()
	c_interface.c_close(self._sid)
end

function responser:sockid()
	return self._sid
end
--c_interface.c_connect(111, "0.0.0.0", 9999)

