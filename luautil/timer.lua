--全局定时器
local class = require("luautil.class")
local timer = class.singleton("timer")	--单例

function timer:__init()
	self._cbs = {}
end

function timer:timeout(ticks, repeatn, func, ...)
	assert(type(func) == 'function')
	local tid = c_interface.c_timeout(ticks, repeatn, repeatn or 1)
	self._cbs[tid] = {func, ...}
	return tid
end

function timer:onTimer(tid, erased)
	local cb = self._cbs[tid]
	if cb then
		if erased ~= 0 then
			self._cbs[tid] = nil
		end
		cb[1](unpack(cb, 2))
	else
		assert(nil, string.format("error timer id %d, erased : %d", tid, erased))
	end
end

--function timer:erase(tid)
	--todo
--end