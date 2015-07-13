--这里放客户端过来的协议处理
local class = require("luautil.class")
--local mysql = class.template("mysql")
local handlers = class.singleton("protocol_handlers")	--协议处理

local keys = {}
handlers["prelogin"] = function (pack, responser)		--请求随机串
	local account = pack.account
	keys[account] = tostring(math.random(1, 10000000))
	responser:responser({key=keys[account]})
end

handlers["login"] = function (package, responser)
	local my = mysql()
	if my:connect("dbname", "usr", "password", "192.168.xx.xxx", 3306) then		--也可以用db长连接
		local err, rows = my:select("user", "id,password", {account=package.account})
		if not err then
			if not rows[1] then
				responser:write("you are not registered") 
				responser:closeconnect()
			else
				if package.md5 == md5(package.account..keys[account]..rows[1].passwd) then	--登陆不要传送密码明文, 科学的做法是传送(username + 随机串(登录前向服务器获取) + 密码) 做md5运算之后的串
					responser:write("login success!")
				else
					responser:write("login fail!")
					responser:closeconnect()
				end
			end
		end
	else
		responser:write("db error") 
		responser:closeconnect()
	end
	my:close()
end

handlers["logout"] = function (package, responser)
	responser:write("hello logout")
	responser:closeconnect()
end
