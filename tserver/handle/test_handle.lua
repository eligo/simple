--这里放客户端过来的协议处理
local class = require("luautil.class")
local handlers = class.singleton("protocol_handlers")	--协议处理
local mysql = require("lmysql")
local keys = {}

handlers["prelogin"] = function (pack, responser)		--请求随机串
	local account = pack.account
	keys[account] = tostring(math.random(1, 10000000))
	responser:responser({key=keys[account]})
end

handlers["login"] = function (package, responser)
	local dbcon, err = s:connect("db1", "root", "", "0.0.0.0", 3306)
	if dbcon then		--也可以用db长连接
		local rown, cursor = my:select("user", "id,password", {account=package.account})
		if rown then
			if rown < 1 then
				responser:write("you are not registered") 
				responser:closeconnect()
			else
				local data = cursor:fetchrow()
				if package.md5 == md5(package.account..keys[account]..data[1].passwd) then	--登陆不要传送密码明文, 科学的做法是传送(username + 随机串(登录前向服务器获取) + 密码) 做md5运算之后的串
					responser:write("login success!")
				else
					responser:write("login fail!")
					responser:closeconnect()
				end
			end
		end
		if cursor then cursor:close() end
		dbcon:close()
	else
		responser:write("db error") 
		responser:closeconnect()
	end
end

handlers["logout"] = function (package, responser)
	responser:write("hello logout")
	responser:closeconnect()
end
