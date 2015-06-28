--这里放客户端过来的协议处理

local handles = gtMsgHandle

handles["login"] = function (package, responser)
	responser:write("hello login")
end

handles["logout"] = function (package, responser)
	responser:write("hello logout")
	responser:closeconnect()
end