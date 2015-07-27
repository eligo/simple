--这个文件主要实现供底层驱动上层的函数(c_onXxxx这种)
package.cpath = string.format("%s;%s?.so", package.cpath, './3rd/luaso/')	--设置外部c库的搜索路径
require("lualib.timer")
local class = require("lualib.class")	--类管理器(模板及实例)
local timer = class.singleton("timer")	--定时器
local external = class.singleton("external")
---------------------------------------------------------framework event---------------------------------------------------------
function c_onTcpAccepted(sid)				--框架事件(接受外来连接)
end

function c_onTcpConnected(sid, ud)			--框架时间(对外连接成功)
end

function c_onTcpClosed(sid, ud)				--框架事件(连接断开, 或者listen失败, 或者connect失败)
	--printf("socket error", sid, ud)
end

function c_onTcpListened(sid, ud)
	print("server Listen suc", ud)
end

function c_onTcpData(sid, str)				--框架事件(连接业务数据到达)
	--print("recv", str)
	if str == 'quit' then
		external.close(sid)
	else
		external.send(sid, string.format("+PONG %d\r\n", external.unixms()))
	end
end

function c_onTimer(tid, erased)				--框架事件(某定时器到期触发) 定时器使用如 timer:timeout(1, 100, function() print("hello") end) 每1个滴答调用一下func, 重复100下
	timer:onTimer(tid, erased)
end

timer:timeout(10,-1,function()
						print(string.format("hello simple, current unix ms : %d", external.unixms()))
					end
)

--c_onTcpAccepted = nil
--c_onTcpListened = nil
external.listen(11, "0.0.0.0", "10000")
