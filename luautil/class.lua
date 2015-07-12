module("luautil.class", package.seeall)

local _classes = {}

function template(classname)
	assert(type(classname) == 'string')
	local cls = _classes[classname]
	if not cls then
		cls = {}
		_classes[classname] = cls
	end
	return cls
end

function singleton(classname)
	
end

function instance(classname, ...)
	local cls = _classes[classname]
	if cls then
		local inst = {}
		setmetatable(inst, {__index=cls})
		inst.__init(inst, ...)
		return inst
	end
end
