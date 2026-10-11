--- Helpers shared by the engine unit tests: reading event lists and setting up a fight with a known monster.

local commands = require("rpg.application.commands")
local entities = require("rpg.domain.entities")
local T = require("support.test")

local fixture = {}

function fixture.types(events)
	local result = {}
	for index, event in ipairs(events) do
		result[index] = event.type
	end
	return result
end

--- The first event of `event_type`; fails the test when there is none (Python's `next(e for e in …)`).
function fixture.first(events, event_type)
	for _, event in ipairs(events) do
		if event.type == event_type then
			return event
		end
	end
	return error("no " .. event_type .. " event in " .. T.show(fixture.types(events)), 2)
end

function fixture.all(events, event_type)
	local result = {}
	for _, event in ipairs(events) do
		if event.type == event_type then
			result[#result + 1] = event
		end
	end
	return result
end

function fixture.count(events, event_type)
	return #fixture.all(events, event_type)
end

function fixture.has_type(events, event_type)
	return fixture.count(events, event_type) > 0
end

--- True when `list` holds a value deeply equal to `expected` (Python's `expected in list`).
function fixture.has(list, expected)
	for _, value in ipairs(list) do
		if T.same(value, expected) then
			return true
		end
	end
	return false
end

function fixture.clear(map)
	for key in pairs(map) do
		map[key] = nil
	end
end

function fixture.item(uid, item_id, rarity, tier, affixes)
	return entities.ItemInstance.new({ uid = uid, item_id = item_id, rarity = rarity, tier = tier, affixes = affixes })
end

--- Starts the next fight and returns its monster.
function fixture.fight(engine)
	engine:step(commands.NextFight())
	local monster = engine.state.monster
	T.ok(monster ~= nil, "the fight has no monster")
	return monster
end

--- Gives the monster one attack that always deals `damage` and enough HP to outlast the test.
function fixture.fixed_attack(monster, damage, element, status)
	monster.attacks = {
		{ id = "test_hit", element = element or "physical", min = damage, max = damage, weight = 1, status = status },
	}
	monster.hp = 10000
	monster.max_hp = 10000
end

return fixture
