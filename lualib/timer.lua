--全局定时器
local class = require("lualib.class")
local timer = class.singleton("timer")	--单例
local external = class.singleton("external")

function timer:__init()
	self._cbs = {}
end

local tid
function timer:timeout(ticks, repeatn, func, ...)
	assert(type(func) == 'function')
	tid = external.timeout(ticks, repeatn, repeatn or 1)
	self._cbs[tid] = {func, ...}
	return tid
end

local cb
function timer:onTimer(tid, erased)
	cb = self._cbs[tid]
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