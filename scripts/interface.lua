--这个文件定义接口以及全局信息

gtMsgHandle = {}	--协议处理方法

local gtResponser = {_sid=nil}		--

local json = require("scripts.util.json")	--json 工具, 也许你还需要别的工具, 如数据库访问工具等...

require ("scripts.handle.test_handle")		--协议处理
require ("scripts.util.timer")
gTimer:init()

function c_onTcpAccepted(sid)	--框架事件通知
end

function c_onTcpClosed(sid)		--框架事件通知
end

function c_onTcpData(sid, str)	--框架事件通知
	gtResponser._sid = sid
	local ok, package = pcall(json.decode, str)
	if not ok then
		gtResponser:write("server only accept json and end with \\r\\n !!!			")
		return
	end
	
	if not package.op then
		gtResponser:write(string.format("json missing field 'op' !!!			"))
		return
	end

	local hdl = gtMsgHandle[package.op]
	if hdl then
		hdl(package, gtResponser)
	else
		gtResponser:write(string.format("no handle for op:%s !!!			", package.op))
	end
end

function c_onTimer(tid, erased)
	gTimer:onTimer(tid, erased)
end

function gtResponser:write (content)
	local ty = type (content)
	if ty == 'table' then
		c_interface.c_send(self._sid, json.encode(content))
	elseif ty == 'string' then
		c_interface.c_send(self._sid, content)
	end
end

function gtResponser:closeconnect ()
	c_interface.c_close(self._sid)
end

gTimer:timeout(10, 2, 
function(...)
	print("hello world", ...)
end,
1
)