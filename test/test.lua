require("dec")
function Player:new(pid)
	local p = {}
	p.pid = pid
	p.name = ""
	p.geted = 0
	setmetatable(p, {__index=Player})
	return p
end

function Player:getId()
	return self.pid
end

function Player:setName(name)
	self.name = name
end

function Player:getName()
	return self.name
end

function Player:getGift()
	--todo give gift to plr
	self.geted = 1
end

function Player:saveToDB()
	local sql = string.format("update player set geted=%d", self.geted)
	--todo update real with sql
end
--------------------------------------------------

function PlayerMgr:addPlr(plr)
	self.plrs[plr:getId()] = plr
end

function PlayerMgr:printPlrs()
	for pid, plr in pairs(self.plrs) do
		print("plr", pid, plr:getName())
	end
end

function PlayerMgr:getPlr(pid)
	return self.plrs[pid]
end
--------------------------------------------------
local plr11 = Player:new(11)
plr11:setName("n11")

local plr12 = Player:new(12)
plr12:setName("n12")


PlayerMgr:addPlr(plr11)
PlayerMgr:addPlr(plr12)

PlayerMgr:printPlrs()
--------------------------------------------------

plr11:getGift()
plr11:saveToDB()

