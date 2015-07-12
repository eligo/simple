--这里放客户端过来的协议处理
local luasql = require("luasql.mysql") --3rd/luaso/luasql/mysql.so
local handler = g_msgHandlers

local keys = {}
handler["prelogin"] = function (pack, responser)	--请求随机串
	local account = pack.account
	keys[account] = tostring(math.random(1, 10000000))
	responser:responser({key=keys[account]})
end

handler["login"] = function (package, responser)
	--[[
	-- create environment object
	local env = assert (luasql.mysql())
	-- connect to data source
	local con = assert (env:connect("database", "usr", "password", "192.168.xx.xxx", 3306))
	local cur = assert (con:execute(string.format("SELECT name, passwd from user where account='%s'", package.account)))    --获取用户数据
	row = cur:fetch ({}, "a") -- the rows will be indexed by field names
	if not row then
		responser:write("you are not registered") 
		responser:closeconnect()
	else
		if package.md5 == md5(package.account..keys[account]..row.passwd) then	--登陆不要传送密码明文, 科学的做法是传送(username + 随机串(登录前向服务器获取) + 密码) 做md5运算之后的串
			responser:write("welcome")
		else
			responser:write("passwd error!")
			responser:closeconnect()
		end
	end
	-- close everything
	if cur then cur:close() end
	con:close()	--也可以做数据库长连接(例如把con弄成全局的)
	env:close()
	]]
end

handler["logout"] = function (package, responser)
	responser:write("hello logout")
	responser:closeconnect()
end