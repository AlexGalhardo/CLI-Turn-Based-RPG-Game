--- Player commands. Each maps 1:1 to a JSON object used by golden files (`{"type": "cast", "spellId": "…"}`).
--- A command is a table with its `type` and the fields of the reference dataclass in snake_case.

local base = require("rpg.domain.base")
local enums = require("rpg.domain.enums")
local json = require("rpg.domain.json_types")

local commands = {}

function commands.Attack()
	return { type = "attack" }
end

function commands.Cast(spell_id)
	return { type = "cast", spell_id = spell_id }
end

function commands.UsePotion(potion_id)
	return { type = "potion", potion_id = potion_id }
end

function commands.Defend()
	return { type = "defend" }
end

function commands.NextFight()
	return { type = "next_fight" }
end

function commands.BuyPotion(potion_id, quantity)
	return { type = "buy_potion", potion_id = potion_id, quantity = quantity }
end

function commands.SellItem(uid)
	return { type = "sell_item", uid = uid }
end

function commands.Equip(uid)
	return { type = "equip", uid = uid }
end

function commands.Unequip(slot)
	return { type = "unequip", slot = slot }
end

--- `index` counts from 0, like the golden files.
function commands.BuyStockItem(index)
	return { type = "buy_stock_item", index = index }
end

--- Victory phase: close the won run (history + Hall of Fame).
function commands.EndRun()
	return { type = "end_run" }
end

--- Victory phase: keep playing endlessly after beating the final boss.
function commands.ContinueRun()
	return { type = "continue_run" }
end

-- JSON key → command field, for the commands that carry data.
local FIELDS = {
	attack = {},
	cast = { { "spellId", "spell_id" } },
	potion = { { "potionId", "potion_id" } },
	defend = {},
	next_fight = {},
	buy_potion = { { "potionId", "potion_id" }, { "quantity", "quantity" } },
	sell_item = { { "uid", "uid" } },
	equip = { { "uid", "uid" } },
	unequip = { { "slot", "slot" } },
	buy_stock_item = { { "index", "index" } },
	end_run = {},
	continue_run = {},
}

local INTEGER_FIELDS = { quantity = true, uid = true, index = true }

function commands.command_to_dict(command)
	local pairs_list = { { "type", command.type } }
	for _, field in ipairs(FIELDS[command.type]) do
		pairs_list[#pairs_list + 1] = { field[1], command[field[2]] }
	end
	return json.object(pairs_list)
end

function commands.command_from_dict(raw)
	local data = json.json_obj(raw)
	local command_type = json.json_str(data.type)
	local fields = FIELDS[command_type]
	if fields == nil then
		base.raise("ValueError", "unknown command type: " .. command_type)
	end
	local command = { type = command_type }
	for _, field in ipairs(fields) do
		local value = data[field[1]]
		command[field[2]] = INTEGER_FIELDS[field[2]] and json.json_int(value) or json.json_str(value)
	end
	if command.slot ~= nil and not enums.is_member(enums.Slot, command.slot) then
		base.raise("ValueError", string.format("'%s' is not a valid Slot", command.slot))
	end
	return command
end

return commands
