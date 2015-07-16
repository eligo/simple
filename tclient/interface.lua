--这个文件主要实现供底层驱动上层的函数(c_onXxxx这种)
package.cpath = string.format("%s;%s?.so", package.cpath, './3rd/luaso/')	--设置外部c库的搜索路径
local class = require("luautil.class")	--类管理器
require("luautil.timer")	
--require("handle.test_handle")					--协议处理实现
--require("luautil.mysql")
local timer = class.singleton("timer")	--定时器
local json = require ('cjson')			--json 工具(encode decode)
local responser = {_sid=nil}			--简单封装一些操作
local handlers = class.singleton("protocol_handlers")	--协议处理集合

local delays = 0
local sn = 0
local Client = class.template("Client")

function Client:__init(id)
	self._id = id
	self._state = 0
	self._sid = 0
	self._watting = 0
	self._data = string.format("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA%5sBBBBB\r\n", self._id)
end

function Client:send()
	if self._watting ~= 0 then return end
	if self._state ~= 2 then return end
	c_interface.c_send(self._sid, self._data)
	--print("send", self._sid, self._data)
	self._watting = c_interface.c_unixtime_ms()
	self._state = 3
end

function Client:onTimer()
	if self._state == 0 then
		self._state = 1
		c_interface.c_connect(self._id, "0.0.0.0", 9999)
	elseif self._state == 2 then
		self:send()
	end
end

function Client:onConn(sid)
	self._sid = sid
	self._state = 2
	self._watting = 0
end

function Client:onClose()
	self._sid = 0
	self._state = 0
end

local ctm = 0
function Client:onData(da)
	ctm = c_interface.c_unixtime_ms()
	assert(self._watting ~= 0)
	assert(self._state == 3)
	delays = delays + ctm - self._watting
	sn = sn + 1
	--if  > 100 then
	--	print(self._id, "cost:", ctm - self._watting)
	--end
	self._watting = 0
	self._state = 2
	self:send()
end

local _clients = {}
local _sidc = {}
for i=1, 1000 do
	_clients[i] = Client(i)
end

---------------------------------------------------------framework event---------------------------------------------------------
function c_onTcpAccepted(sid)				--框架事件(连接接受)
	print("c_onTcpAccepted", sid)
end

function c_onTcpConnected(sid, ud)			--框架时间(连接成功)
	print("c_onTcpConnected", sid, ud)
	_sidc[sid] = ud
	_clients[ud]:onConn(sid)
end

function c_onTcpClosed(sid, ud)				--框架事件(连接断开, 或者listen失败, 或者connect失败)
	--print("c_onTcpClosed", sid, ud)
	if ud == 0 then
		_clients[_sidc[sid]]:onClose(sid)
	else
		assert(ud)
		_clients[ud]:onClose(sid)
	end
end

function c_onTcpListened(sid, ud)
	print("server Listen suc", ud)
end

local total = 0
local lasttotal = 0
local lasttime = 0
function c_onTcpData(sid, str)				--框架事件(连接业务数据到达)
	_clients[_sidc[sid]]:onData(str)
	total=total+1
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

timer:timeout(1,-1,function()
	for k, v in pairs(_clients) do
		v:onTimer()
	end
end)

timer:timeout(10,-1,function()
						local ctm = c_interface.c_unixtime_ms()
						--if ctm - lasttime >= 1000 then
							local state = {0,0,0,0}
							state[0]=0
							for k, v in pairs(_clients) do
								assert(v._state)
								state[v._state] = (state[v._state] or 0) + 1
							end
							if sn == 0 then sn = 1 end
							print(string.format("client(broken:%d, connectting:%d, relaxing:%d, waitting_response:%d) recv package total:%d  speed:%d/s delay:%d",state[0],state[1],state[2],state[3], total, (total - lasttotal)/((ctm-lasttime)/1000), delays/sn))
							lasttime = ctm
							lasttotal = total
							delays = 0
							sn = 0
						--end
					end
)