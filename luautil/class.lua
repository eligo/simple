module("luautil.class", package.seeall)

local setmetatable = setmetatable
local _classes = {}

function template(classname)				--类模板
	assert(type(classname) == 'string')
	local t = _classes[classname]
	if not t then
		local cls = {}
		local metatable = {
			__call = function(...)
				local instance = {}
				setmetatable(instance, {__index=cls})
				instance.__init(instance, ...)
				return instance
			end
		}
		setmetatable(cls, metatable)
		t = {cls}
		_classes[classname] = t
	else
		assert(not t[2], classname)
	end
	return t[1]
end

function singleton(classname)				--单例
	local t = _classes[classname]
	if not t then
		t = {{}, true, 0}
		_classes[classname] = t
	else
		assert(t[2])
		if t[3] == 0 then
			if t[1].__init then
				t[1].__init(t[1])
			end
			t[3] = 1
		end
	end
	return t[1]
end