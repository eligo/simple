--这个文件主要实现供底层驱动上层的函数(c_onXxxx这种)
package.cpath = string.format("%s;%s?.so", package.cpath, './3rd/luaso/')	--设置外部c库的搜索路径
local class = require("luautil.class")	--类管理器
require("luautil.timer")	
--require("luautil.mysql")
local timer = class.singleton("timer")	--定时器
local json = require ('cjson')			--json 工具(encode decode)
local responser = {_sid=nil}			--简单封装一些操作
---------------------------------------------------------framework event---------------------------------------------------------
local cons = {}
local convec = {}
local rand = 1
function c_onTcpAccepted(sid)				--框架事件(连接接受)
	cons[sid] = {}
	table.insert(convec, sid)
end

function c_onTcpConnected(sid, ud)			--框架时间(连接成功)
end

function c_onTcpClosed(sid, ud)				--框架事件(连接断开,
	cons[sid] = nil
	for k, v in pairs(convec) do
		if sid == v then
			table.remove(convec, k)
		end
	end
end

c_interface.c_listen(11, "0.0.0.0", "10000")
function c_onTcpListened(sid, ud)
end

local total = 0
local lasttotal = 0
local lasttime = 0
local bytes = 0
local lastbytes = 0
function c_onTcpData(sid, str)				--框架事件(连接业务数据到达)
	--print(str)
	if str == 'quit' then
		c_interface.c_send(sid, "goodbye!!!\r\n")
		c_interface.c_close(sid)	
	end
	local i = 100
	local sn = #convec
	while i > 0 do
		i = i - 1
		rand = rand + 1
		if rand > sn then
			rand = 1
		end
		if not convec[rand] then break end
	--for _, sid in pairs(convec) do
		c_interface.c_send(convec[rand], str.."\r\n")--str.."\r\n")--string.format("welcome! current time: %s, enter 'quit' will close connection! recved:%s\r\n", c_interface.c_unixtime_ms(), str))
	--end
	end
	total = total + 1
	bytes = bytes + string.len(str)
	--[[
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
	end]]
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

timer:timeout(5,-1,function()
						local ctm = c_interface.c_unixtime_ms()
						local sec = ((ctm-lasttime)/1000)
						--if ctm - lasttime >= 1000 then
							print(string.format("recv package total:%d  speed:%d 个/s, %d mb/s",total, (total - lasttotal)/sec, (bytes-lastbytes)/sec/1024/1024))
							lasttime = ctm
							lasttotal = total
							lastbytes = bytes
						--end
					end
)