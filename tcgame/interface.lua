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
	self._idstr = tostring(id).."\r\n"
	self._idstr2 = tostring(id)
	self._state = 0
	self._sid = 0
	self._watting = 0
	self._inc = 0
	self._onc = 0
	self._tms = {}
end

function Client:send()
	self._stm = c_interface.c_unixtime_ms()
	c_interface.c_send(self._sid, self._idstr)--"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n")
	self._inc = self._inc + 1
	self._state = 3
end
function Client:onConn(sid)
	self._sid = sid
	self._state = 2
	self._watting = 0
	self:send()
end

function Client:onClose()
	self._sid = 0
	self._state = 0
end

local ctm = 0
function Client:onData(data)
	if data == self._idstr2 then
		ctm = c_interface.c_unixtime_ms()
		delays = ctm - self._stm
		sn = sn + 1
		--print(data, self._idstr2)
	end
end

local _clients = {}
local _sidc = {}

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

for i=1, 10000 do
	_clients[i] = Client(i)
	_clients[i]._state = 1
	c_interface.c_connect(i, "0.0.0.0", 10000)--6379)
end

timer:timeout(10,-1,function()
	for sid, ud in pairs(_sidc) do
		_clients[ud]:send()
	end
end
)

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