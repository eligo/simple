--这里放客户端过来的协议处理

local handler = g_msgHandlers

handler["login"] = function (package, responser)
	responser:write("hello login")
end

handler["logout"] = function (package, responser)
	responser:write("hello logout")
	responser:closeconnect()
end