gTimer = gTimer or {}

function gTimer:init()
	self._cbs = {}
end

function gTimer:timeout(ticks, repeatn, func, ...)
	assert(type(func) == 'function')
	local tid = c_interface.c_timeout(ticks, repeatn, repeatn or 1)
	self._cbs[tid] = {func, ...}
	print("tid is", tid)
	return tid
end

function gTimer:onTimer(tid, erased)
	local cb = self._cbs[tid]
	if cb then
		if erased ~= 0 then
			self._cbs[tid] = nil
			self:timeout(10, 1, cb[1], 'haha'..tid)
		end
		cb[1](unpack(cb, 2))
	else
		assert(nil, string.format("error timer id %d, erased : %d", tid, erased))
	end
end

function gTimer:erase(tid)
	--todo
end