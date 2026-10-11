local commands = require("rpg.application.commands")
local run_state = require("rpg.application.run_state")
local entities = require("rpg.domain.entities")
local json = require("rpg.domain.json_types")
local fixture = require("support.engine_fixture")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "unit/serialization"

local function through_json(value)
	return json.decode(json.encode(value))
end

T.test(GROUP, "command round trip", function()
	local all_commands = {
		commands.Attack(),
		commands.Cast("brutal_strike"),
		commands.UsePotion("health_potion"),
		commands.Defend(),
		commands.NextFight(),
		commands.BuyPotion("mana_potion", 3),
		commands.SellItem(4),
		commands.Equip(5),
		commands.Unequip("ring"),
		commands.BuyStockItem(1),
		commands.EndRun(),
		commands.ContinueRun(),
	}
	for _, command in ipairs(all_commands) do
		T.eq(commands.command_from_dict(through_json(commands.command_to_dict(command))), command, command.type)
	end
end)

T.test(GROUP, "unknown command type", function()
	T.raises("ValueError", function()
		commands.command_from_dict(json.object({ { "type", "dance" } }))
	end, "unknown command")
end)

T.test(GROUP, "run state round trip through json", function()
	local engine = helpers.new_engine()
	engine:step(commands.NextFight())
	for _ = 1, 3 do
		engine:step(commands.Attack())
	end
	local state = engine.state
	local bag = state.player.bag
	bag[#bag + 1] = fixture.item(90, "sword", "mythic", 2, { entities.AffixRoll.new("dodge", 3) })
	local statuses = state.player.statuses
	statuses[#statuses + 1] = entities.ActiveStatus.new("burn", 2, 4)
	if state.monster ~= nil then
		local monster_statuses = state.monster.statuses
		monster_statuses[#monster_statuses + 1] = entities.ActiveStatus.new("stun", 1, 0)
	end
	local restored = run_state.RunState.from_dict(through_json(state:to_dict()))
	T.eq(restored, state)
	T.ok(json.equal(restored:to_dict(), state:to_dict()))
	T.eq(json.encode(restored:to_dict(), "\t"), json.encode(state:to_dict(), "\t"))
end)

T.test(GROUP, "json readers reject wrong types", function()
	local cases = {
		{ json.json_obj, json.array({}) },
		{ json.json_list, json.object({}) },
		{ json.json_int, true },
		{ json.json_int, "1" },
		{ json.json_str, 1 },
		{ json.json_bool, 0 },
	}
	for _, case in ipairs(cases) do
		T.eq(type(case[1]), "function")
		T.raises("TypeError", function()
			case[1](case[2])
		end)
	end
end)
