--全局定时器
local luasql = require("luasql.mysql") --3rd/luaso/luasql/mysql.so
local class = require("luautil.class")
local mysql = class.template("mysql")
function mysql:__init()
	self._env = assert (luasql.mysql())
end

function mysql:connect(db, usr, passwd, ip, port)
	local err
	if not self._conn then
		self._conn, err = self._env:connect(db, usr, passwd, ip, port)
	end
	return self._conn, err
end

function mysql:select(tbl, field, conditon)
	if not self._conn then return end
	assert(type(tbl) == 'string')
	assert(type(field) == 'string')
	assert(type(conditon) == 'string' or not conditon)
	local sql = "SELECT "..field.." FROM "..tbl
	if conditon then
		sql = sql.." WHERE "..conditon
	end
	local cur, err = self._conn:execute(sql)
	if cur then
		local rows = {}
		while cur do
	        local row = cur:fetch({}, "a")
	        if not row then break end
	        table.insert(rows, row)
	    end
	    cur:close()
	    return nil, rows
	end
	return err, nil
end

function mysql:insert(tbl, row)			--TODO
	if not self._conn then return end
	assert(type(tbl) == 'string')
	assert(type(row) == 'table')
	local sql = "INSERT "..tbl
	local key = "("
	local val = "("
	local first = true
	for k, v in pairs(row) do
		assert(type(k) ~= 'table')
		assert(type(v) ~= 'table')
		if not first then 
			key = key..","
			val = val..","
		end
		first = false
		key = key..k
		val = val.."'"..v.."'"
	end
	key = key..')'
	val = val..')'
	sql = sql..key..' VALUES'..val
	local effectRows, err = self._conn:execute(sql)
    return err, effectRows
end

function mysql:update(tbl, data, conditon)
	if not self._conn then return end
	assert(type(tbl) == 'string')
	assert(type(data) == 'table')
	assert(type(conditon) == 'string' or not conditon)
	local sql = "UPDATE "..tbl..' set '
	local upd = ""
	local first = true
	for k, v in pairs(data) do
		if not first then upd = upd.."," end
		first = false
		assert(type(k) ~= 'table')
		assert(type(v) ~= 'table')
		upd = upd..k.."='"..v.."'"
	end
	sql = sql..upd
	if conditon then
		sql = sql..upd.." WHERE "..conditon
	end
	local effectRows, err = self._conn:execute(sql)
	return err, effectRows
end

function mysql:close()
	if self._conn then
		self._conn:close()
		self._conn = nil
	end
	if self._env then
		self._env:close()
		self._env = nil
	end
end