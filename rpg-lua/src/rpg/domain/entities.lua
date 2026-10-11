--- Mutable run state. Serialised with camelCase keys: the save format is shared by every implementation.

local base = require("rpg.domain.base")
local enums = require("rpg.domain.enums")
local json = require("rpg.domain.json_types")

local json_obj, json_list, json_int = json.json_obj, json.json_list, json.json_int
local json_str, json_bool = json.json_str, json.json_bool

local entities = {}

local function member(set, name, value)
	if not enums.is_member(set, value) then
		base.raise("ValueError", string.format("'%s' is not a valid %s", tostring(value), name))
	end
	return value
end

local function to_dicts(list)
	local result = json.array({})
	for index, item in ipairs(list) do
		result[index] = item:to_dict()
	end
	return result
end

local function from_dicts(raw, class)
	local result = {}
	for index, item in ipairs(json_list(raw)) do
		result[index] = class.from_dict(item)
	end
	return result
end

--- A string → integer map as an object with its keys in code-point order.
local function counter_to_dict(counter)
	local pairs_list = {}
	for index, key in ipairs(base.sorted_keys(counter)) do
		pairs_list[index] = { key, counter[key] }
	end
	return json.object(pairs_list)
end
entities.counter_to_dict = counter_to_dict

local function counter_from_dict(raw)
	local counter = {}
	for key, value in pairs(json_obj(raw)) do
		counter[key] = json_int(value)
	end
	return counter
end
entities.counter_from_dict = counter_from_dict

-- ── ActiveStatus ────────────────────────────────────────────────────────────

local ActiveStatus = base.class("ActiveStatus")
entities.ActiveStatus = ActiveStatus

function ActiveStatus.new(status_id, turns, per_turn)
	return setmetatable({ status_id = status_id, turns = turns, per_turn = per_turn }, ActiveStatus)
end

function ActiveStatus:to_dict()
	return json.object({ { "statusId", self.status_id }, { "turns", self.turns }, { "perTurn", self.per_turn } })
end

function ActiveStatus.from_dict(raw)
	local data = json_obj(raw)
	return ActiveStatus.new(json_str(data.statusId), json_int(data.turns), json_int(data.perTurn))
end

-- ── AffixRoll ───────────────────────────────────────────────────────────────

local AffixRoll = base.class("AffixRoll")
entities.AffixRoll = AffixRoll

function AffixRoll.new(stat, value)
	return setmetatable({ stat = stat, value = value }, AffixRoll)
end

function AffixRoll:to_dict()
	return json.object({ { "stat", self.stat }, { "value", self.value } })
end

function AffixRoll.from_dict(raw)
	local data = json_obj(raw)
	return AffixRoll.new(member(enums.Stat, "Stat", json_str(data.stat)), json_int(data.value))
end

-- ── ItemInstance: uid, item_id, rarity, tier, affixes ───────────────────────

local ItemInstance = base.class("ItemInstance")
entities.ItemInstance = ItemInstance

function ItemInstance.new(fields)
	fields.affixes = fields.affixes or {}
	return setmetatable(fields, ItemInstance)
end

function ItemInstance:to_dict()
	return json.object({
		{ "uid", self.uid },
		{ "itemId", self.item_id },
		{ "rarity", self.rarity },
		{ "tier", self.tier },
		{ "affixes", to_dicts(self.affixes) },
	})
end

function ItemInstance.from_dict(raw)
	local data = json_obj(raw)
	return ItemInstance.new({
		uid = json_int(data.uid),
		item_id = json_str(data.itemId),
		rarity = json_str(data.rarity),
		tier = json_int(data.tier),
		affixes = from_dicts(data.affixes, AffixRoll),
	})
end

-- ── Player ──────────────────────────────────────────────────────────────────

local Player = base.class("Player")
entities.Player = Player

--- `fields`: name, vocation_id, hp, mp, gold and optionally the rest (level 1, empty bag…).
function Player.new(fields)
	local self = setmetatable(fields, Player)
	self.level = self.level or 1
	self.xp = self.xp or 0
	self.magic_level = self.magic_level or 1
	self.mana_spent = self.mana_spent or 0
	self.potions = self.potions or {}
	self.equipment = self.equipment or {}
	self.bag = self.bag or {}
	self.spell_uses = self.spell_uses or {}
	self.statuses = self.statuses or {}
	self.stun_cooldown = self.stun_cooldown or 0
	self.defending = self.defending or false
	return self
end

function Player:potion_count(potion_id)
	return self.potions[potion_id] or 0
end

function Player:to_dict()
	local equipment = {}
	for index, slot in ipairs(base.sorted_keys(self.equipment)) do
		equipment[index] = { slot, self.equipment[slot]:to_dict() }
	end
	return json.object({
		{ "name", self.name },
		{ "vocationId", self.vocation_id },
		{ "hp", self.hp },
		{ "mp", self.mp },
		{ "gold", self.gold },
		{ "level", self.level },
		{ "xp", self.xp },
		{ "magicLevel", self.magic_level },
		{ "manaSpent", self.mana_spent },
		{ "potions", counter_to_dict(self.potions) },
		{ "equipment", json.object(equipment) },
		{ "bag", to_dicts(self.bag) },
		{ "spellUses", counter_to_dict(self.spell_uses) },
		{ "statuses", to_dicts(self.statuses) },
		{ "stunCooldown", self.stun_cooldown },
		{ "defending", self.defending },
	})
end

function Player.from_dict(raw)
	local data = json_obj(raw)
	local equipment = {}
	for slot, item in pairs(json_obj(data.equipment)) do
		equipment[member(enums.Slot, "Slot", slot)] = ItemInstance.from_dict(item)
	end
	return Player.new({
		name = json_str(data.name),
		vocation_id = json_str(data.vocationId),
		hp = json_int(data.hp),
		mp = json_int(data.mp),
		gold = json_int(data.gold),
		level = json_int(data.level),
		xp = json_int(data.xp),
		magic_level = json_int(data.magicLevel),
		mana_spent = json_int(data.manaSpent),
		potions = counter_from_dict(data.potions),
		equipment = equipment,
		bag = from_dicts(data.bag, ItemInstance),
		spell_uses = counter_from_dict(data.spellUses),
		statuses = from_dicts(data.statuses, ActiveStatus),
		stun_cooldown = json_int(data.stunCooldown),
		defending = json_bool(data.defending),
	})
end

-- ── monster attacks (definitions scaled for one spawned monster) ────────────

local function attack_to_dict(attack)
	local status
	if attack.status ~= nil then
		status = json.object({
			{ "id", attack.status.status },
			{ "chance", attack.status.chance },
			{ "damagePct", attack.status.damage_pct },
		})
	end
	return json.object({
		{ "id", attack.id },
		{ "element", attack.element },
		{ "min", attack.min },
		{ "max", attack.max },
		{ "weight", attack.weight },
		{ "status", status },
	})
end

function entities.attack_from_dict(raw)
	local data = json_obj(raw)
	local status_raw = json.opt(data, "status")
	local status
	if status_raw ~= nil then
		local status_data = json_obj(status_raw)
		status = {
			status = json_str(status_data.id),
			chance = json_int(status_data.chance),
			damage_pct = json_int(status_data.damagePct),
		}
	end
	return {
		id = json_str(data.id),
		element = member(enums.Element, "Element", json_str(data.element)),
		min = json_int(data.min),
		max = json_int(data.max),
		weight = json_int(data.weight),
		status = status,
	}
end

-- ── MonsterInstance ─────────────────────────────────────────────────────────

--- A spawned monster: definition id plus stats already scaled for the round and difficulty.
local MonsterInstance = base.class("MonsterInstance")
entities.MonsterInstance = MonsterInstance

--- `fields`: creature_id, is_boss, enemy_class, hp, max_hp, xp, gold_min, gold_max, attacks.
function MonsterInstance.new(fields)
	local self = setmetatable(fields, MonsterInstance)
	self.statuses = self.statuses or {}
	self.stun_cooldown = self.stun_cooldown or 0
	self.boss_actions = self.boss_actions or 0
	return self
end

function MonsterInstance:attack(attack_id)
	for _, attack in ipairs(self.attacks) do
		if attack.id == attack_id then
			return attack
		end
	end
	return base.raise("KeyError", "'" .. tostring(attack_id) .. "'")
end

function MonsterInstance:to_dict()
	local attacks = json.array({})
	for index, attack in ipairs(self.attacks) do
		attacks[index] = attack_to_dict(attack)
	end
	return json.object({
		{ "creatureId", self.creature_id },
		{ "isBoss", self.is_boss },
		{ "enemyClass", self.enemy_class },
		{ "hp", self.hp },
		{ "maxHp", self.max_hp },
		{ "xp", self.xp },
		{ "goldMin", self.gold_min },
		{ "goldMax", self.gold_max },
		{ "attacks", attacks },
		{ "statuses", to_dicts(self.statuses) },
		{ "stunCooldown", self.stun_cooldown },
		{ "bossActions", self.boss_actions },
	})
end

function MonsterInstance.from_dict(raw)
	local data = json_obj(raw)
	local attacks = {}
	for index, attack in ipairs(json_list(data.attacks)) do
		attacks[index] = entities.attack_from_dict(attack)
	end
	return MonsterInstance.new({
		creature_id = json_str(data.creatureId),
		is_boss = json_bool(data.isBoss),
		enemy_class = json_str(data.enemyClass),
		hp = json_int(data.hp),
		max_hp = json_int(data.maxHp),
		xp = json_int(data.xp),
		gold_min = json_int(data.goldMin),
		gold_max = json_int(data.goldMax),
		attacks = attacks,
		statuses = from_dicts(data.statuses, ActiveStatus),
		stun_cooldown = json_int(data.stunCooldown),
		boss_actions = json_int(data.bossActions),
	})
end

return entities
