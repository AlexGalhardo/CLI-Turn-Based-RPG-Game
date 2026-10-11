--- Deterministic run counters, derived only from engine events (docs/game-design.md §11).

local base = require("rpg.domain.base")
local entities = require("rpg.domain.entities")
local json = require("rpg.domain.json_types")

local json_obj, json_list, json_int, json_str = json.json_obj, json.json_list, json.json_int, json.json_str
local counter_to, counter_from = entities.counter_to_dict, entities.counter_from_dict

local function int_field(event, name)
	local value = event[name]
	if math.type(value) ~= "integer" then
		base.raise("TypeError", "event field " .. name .. " must be int")
	end
	return value
end

local function str_field(event, name)
	local value = event[name]
	if type(value) ~= "string" then
		base.raise("TypeError", "event field " .. name .. " must be str")
	end
	return value
end

local function add(counter, key, amount)
	counter[key] = (counter[key] or 0) + (amount or 1)
end

-- Save key → field, in the order the reference writes them.
local INTEGERS = {
	{ "damageDealt", "damage_dealt" },
	{ "damageTaken", "damage_taken" },
	{ "healingDone", "healing_done" },
	{ "highestHit", "highest_hit" },
	{ "normalAttacks", "normal_attacks" },
	{ "crits", "crits" },
	{ "dodges", "dodges" },
	{ "parries", "parries" },
	{ "defends", "defends" },
	{ "goldLooted", "gold_looted" },
	{ "goldSpent", "gold_spent" },
	{ "goldEarned", "gold_earned" },
	{ "itemsSold", "items_sold" },
	{ "itemsAutoEquipped", "items_auto_equipped" },
	{ "bossesKilled", "bosses_killed" },
	{ "elitesKilled", "elites_killed" },
}

local COUNTERS = {
	{ "spellsCast", "spells_cast" },
	{ "potionsUsed", "potions_used" },
	{ "potionsBought", "potions_bought" },
	{ "potionsDropped", "potions_dropped" },
	{ "itemsDropped", "items_dropped" },
	{ "kills", "kills" },
	{ "statusesApplied", "statuses_applied" },
}

local RunStatistics = base.class("RunStatistics")

function RunStatistics.new()
	local self = setmetatable({ dropped_items = {} }, RunStatistics)
	for _, field in ipairs(INTEGERS) do
		self[field[2]] = 0
	end
	for _, field in ipairs(COUNTERS) do
		self[field[2]] = {}
	end
	return self
end

function RunStatistics:record(events, current_round)
	for _, event in ipairs(events) do
		if not self:_record_combat(event) then
			self:_record_loot(event, current_round)
		end
	end
end

function RunStatistics:_dealt(event)
	local damage = int_field(event, "damage")
	self.damage_dealt = self.damage_dealt + damage
	self.highest_hit = math.max(self.highest_hit, damage)
	if event.crit == true then
		self.crits = self.crits + 1
	end
end

local COMBAT = {
	player_attacked = function(self, event)
		self.normal_attacks = self.normal_attacks + 1
		self:_dealt(event)
	end,
	spell_cast = function(self, event)
		add(self.spells_cast, str_field(event, "spellId"))
		self:_dealt(event)
	end,
	spell_healed = function(self, event)
		add(self.spells_cast, str_field(event, "spellId"))
		self.healing_done = self.healing_done + int_field(event, "amount")
	end,
	potion_used = function(self, event)
		add(self.potions_used, str_field(event, "potionId"))
		if event.resource == "hp" then
			self.healing_done = self.healing_done + int_field(event, "amount")
		end
	end,
	player_defended = function(self)
		self.defends = self.defends + 1
	end,
	monster_attacked = function(self, event)
		self.damage_taken = self.damage_taken + int_field(event, "damage")
	end,
	attack_dodged = function(self)
		self.dodges = self.dodges + 1
	end,
	attack_parried = function(self, event)
		self.parries = self.parries + 1
		self.damage_dealt = self.damage_dealt + int_field(event, "reflected")
	end,
	monster_parried = function(self, event)
		self.damage_taken = self.damage_taken + int_field(event, "reflected")
	end,
	status_ticked = function(self, event)
		if event.target == "player" then
			self.damage_taken = self.damage_taken + int_field(event, "damage")
		else
			self.damage_dealt = self.damage_dealt + int_field(event, "damage")
		end
	end,
	status_applied = function(self, event)
		if event.target == "monster" then
			add(self.statuses_applied, str_field(event, "status"))
		end
	end,
	monster_killed = function(self, event)
		add(self.kills, str_field(event, "monsterId"))
		if event.isBoss == true then
			self.bosses_killed = self.bosses_killed + 1
		end
		if event.enemyClass == "elite" then
			self.elites_killed = self.elites_killed + 1
		end
	end,
}

local function sold(self, event)
	self.items_sold = self.items_sold + 1
	self.gold_earned = self.gold_earned + int_field(event, "gold")
end

local LOOT = {
	gold_looted = function(self, event)
		self.gold_looted = self.gold_looted + int_field(event, "amount")
	end,
	item_dropped = function(self, event, current_round)
		local rarity = str_field(event, "rarity")
		add(self.items_dropped, rarity)
		self.dropped_items[#self.dropped_items + 1] =
			{ item_id = str_field(event, "itemId"), rarity = rarity, round = current_round }
	end,
	potion_bought = function(self, event)
		add(self.potions_bought, str_field(event, "potionId"), int_field(event, "quantity"))
		self.gold_spent = self.gold_spent + int_field(event, "gold")
	end,
	item_bought = function(self, event)
		self.gold_spent = self.gold_spent + int_field(event, "gold")
	end,
	potion_dropped = function(self, event)
		add(self.potions_dropped, str_field(event, "potionId"))
	end,
	item_auto_equipped = function(self)
		self.items_auto_equipped = self.items_auto_equipped + 1
	end,
	item_sold = sold,
	item_auto_sold = sold,
}

--- Battle counters; returns false when the event is not a battle event.
function RunStatistics:_record_combat(event)
	local handler = COMBAT[event.type]
	if handler == nil then
		return false
	end
	handler(self, event)
	return true
end

function RunStatistics:_record_loot(event, current_round)
	local handler = LOOT[event.type]
	if handler ~= nil then
		handler(self, event, current_round)
	end
end

function RunStatistics:to_dict()
	local pairs_list = {}
	for _, field in ipairs(INTEGERS) do
		pairs_list[#pairs_list + 1] = { field[1], self[field[2]] }
	end
	for _, field in ipairs(COUNTERS) do
		pairs_list[#pairs_list + 1] = { field[1], counter_to(self[field[2]]) }
	end
	local dropped = json.array({})
	for index, item in ipairs(self.dropped_items) do
		dropped[index] = json.object({ { "itemId", item.item_id }, { "rarity", item.rarity }, { "round", item.round } })
	end
	pairs_list[#pairs_list + 1] = { "droppedItems", dropped }
	return json.object(pairs_list)
end

function RunStatistics.from_dict(raw)
	local data = json_obj(raw)
	local self = RunStatistics.new()
	for _, field in ipairs(INTEGERS) do
		self[field[2]] = json_int(data[field[1]])
	end
	for _, field in ipairs(COUNTERS) do
		self[field[2]] = counter_from(data[field[1]])
	end
	for index, entry in ipairs(json_list(data.droppedItems)) do
		local item = json_obj(entry)
		self.dropped_items[index] =
			{ item_id = json_str(item.itemId), rarity = json_str(item.rarity), round = json_int(item.round) }
	end
	return self
end

return RunStatistics
