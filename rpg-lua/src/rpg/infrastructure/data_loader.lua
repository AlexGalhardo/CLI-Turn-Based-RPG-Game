--- Loads shared/data/*.json into immutable domain definitions.

local base = require("rpg.domain.base")
local definitions = require("rpg.domain.definitions")
local enums = require("rpg.domain.enums")
local json = require("rpg.domain.json_types")
local filesystem = require("rpg.infrastructure.filesystem")

local json_obj, json_list, json_int = json.json_obj, json.json_list, json.json_int
local json_str, json_bool = json.json_str, json.json_bool

local data_loader = {}

-- Raised (kind "DataError") when a data file is missing a field or has an invalid value.

-- Python's `raw["key"]`: reading a field the file does not have raises a KeyError that names it.
local STRICT = {
	__index = function(_, key)
		base.raise("KeyError", "'" .. tostring(key) .. "'")
	end,
}

--- Makes every object of a parsed data file strict; optional fields are read with `json.opt` or `rawget`.
local function strict(value)
	if type(value) == "table" and value ~= json.null then
		for _, child in pairs(value) do
			strict(child)
		end
		if json.is_object(value) then
			setmetatable(value, STRICT)
		end
	end
	return value
end

local function read(directory, name)
	local path = filesystem.join(directory, name .. ".json")
	local text = filesystem.read_file(path)
	if text == nil then
		base.raise("DataError", path .. ": cannot read the file")
	end
	local parsed, document = base.try({ "JsonError", "TypeError" }, function()
		return json_obj(json.decode(text))
	end)
	if not parsed then
		base.raise("DataError", path .. ": " .. document.message)
	end
	return strict(document)
end

local function member(set, name, value)
	local text = json_str(value)
	if not enums.is_member(set, text) then
		base.raise("ValueError", string.format("'%s' is not a valid %s", text, name))
	end
	return text
end

local function objects(document, key)
	local result = {}
	for index, entry in ipairs(json_list(document[key])) do
		result[index] = json_obj(entry)
	end
	return result
end

local function strs(value)
	local result = {}
	for index, text in ipairs(json_list(value)) do
		result[index] = json_str(text)
	end
	return result
end

--- An object of integers as a map plus its keys in file order.
local function int_map(raw, set, name)
	local map, order = {}, {}
	local object = json_obj(raw)
	for _, key in ipairs(json.keys(object)) do
		if set ~= nil then
			member(set, name, key)
		end
		map[key] = json_int(object[key])
		order[#order + 1] = key
	end
	return map, order
end

local function attack(raw)
	local status_raw = json.opt(raw, "status")
	local status
	if status_raw ~= nil then
		local status_obj = json_obj(status_raw)
		status = {
			status = json_str(status_obj.id),
			chance = json_int(status_obj.chance),
			damage_pct = json_int(status_obj.damagePct),
		}
	end
	return {
		id = json_str(raw.id),
		element = member(enums.Element, "Element", raw.element),
		min = json_int(raw.min),
		max = json_int(raw.max),
		weight = json_int(raw.weight),
		status = status,
	}
end

local function creature(raw, is_boss)
	local gold = json_obj(raw.gold)
	local charge = json.opt(raw, "chargeAttack")
	local attacks = {}
	for index, entry in ipairs(json_list(raw.attacks)) do
		attacks[index] = attack(json_obj(entry))
	end
	return definitions.MonsterDef.new({
		id = json_str(raw.id),
		name = json_str(raw.name),
		tier = json_int(raw.tier),
		family = json_str(raw.family),
		hp = json_int(raw.hp),
		xp = json_int(raw.xp),
		gold_min = json_int(gold.min),
		gold_max = json_int(gold.max),
		attacks = attacks,
		resistances = (int_map(raw.resistances, enums.Element, "Element")),
		is_boss = is_boss,
		charge_attack = charge ~= nil and json_str(charge) or nil,
	})
end

local function spell(raw)
	local bonus = json_obj(raw.level3Bonus)
	local status = json.opt(bonus, "status")
	return {
		id = json_str(raw.id),
		name = json_str(raw.name),
		words = json_str(raw.words),
		kind = member(enums.SpellKind, "SpellKind", raw.kind),
		element = member(enums.Element, "Element", raw.element),
		mana = json_int(raw.mana),
		min = json_int(raw.min),
		max = json_int(raw.max),
		per_level = json_int(raw.perLevel),
		per_magic_level = json_int(raw.perMagicLevel),
		level3_bonus = {
			status = status ~= nil and json_str(status) or nil,
			chance = json_int(rawget(bonus, "chance") or 0),
			cleanse = json_bool(rawget(bonus, "cleanse") or false),
		},
	}
end

local function vocation(raw)
	return {
		id = json_str(raw.id),
		name = json_str(raw.name),
		start_hp = json_int(raw.startHp),
		start_mp = json_int(raw.startMp),
		hp_per_level = json_int(raw.hpPerLevel),
		mp_per_level = json_int(raw.mpPerLevel),
		hp_regen = json_int(raw.hpRegen),
		mp_regen = json_int(raw.mpRegen),
		melee_min = json_int(raw.meleeMin),
		melee_max = json_int(raw.meleeMax),
		melee_per_level = json_int(raw.meleePerLevel),
		weapon_types = strs(raw.weaponTypes),
		shield_types = strs(raw.shieldTypes),
		starter_weapon = json_str(raw.starterWeapon),
		spells = strs(raw.spells),
	}
end

local function enemy_class(class_id, raw)
	return {
		id = class_id,
		stat_pct = json_int(raw.statPct),
		reward_pct = json_int(raw.rewardPct),
		dodge = json_int(raw.dodge),
		parry = json_int(raw.parry),
		crit = json_int(raw.crit),
		heal = json_int(raw.heal),
		drop_chance_pct = json_int(raw.dropChancePct),
		drops = json_int(raw.drops),
		potion_drop_pct = json_int(raw.potionDropPct),
		rarity_weights = (int_map(raw.rarityWeights)),
	}
end

local function auto_battle(raw)
	local modes_raw = json_obj(raw.modes)
	local modes = {}
	-- The key order of `modes` is the menu order, so it is read in file order.
	for _, mode_id in ipairs(json.keys(modes_raw)) do
		local mode = json_obj(modes_raw[mode_id])
		modes[#modes + 1] =
			{ id = mode_id, offense = json_str(mode.offense), support_every = json_int(mode.supportEvery) }
	end
	return definitions.AutoBattleDef.new({
		heal_below_pct = json_int(raw.healBelowPct),
		mana_below_pct = json_int(raw.manaBelowPct),
		emergency_heal_below_pct = json_int(raw.emergencyHealBelowPct),
		modes = modes,
	})
end

local function balance(raw)
	local caps = json_obj(raw.caps)
	local magic = json_obj(raw.magicLevel)
	local classes = json_obj(raw.enemyClasses)
	local enemy_classes = {}
	for index, class_id in ipairs(enums.EnemyClass.values) do
		enemy_classes[index] = enemy_class(class_id, json_obj(classes[class_id]))
	end
	local rarity_weights = {}
	for table_id, weights in pairs(json_obj(raw.rarityWeights)) do
		rarity_weights[table_id] = (int_map(weights))
	end
	return definitions.Balance.new({
		rounds_per_tier = json_int(raw.roundsPerTier),
		cycle_stat_pct = json_int(raw.cycleStatPct),
		cycle_reward_pct = json_int(raw.cycleRewardPct),
		position_pct = json_int(raw.positionPct),
		final_round = json_int(raw.finalRound),
		elite_chance_pct = json_int(raw.eliteChancePct),
		difficulties = base.map(objects(raw, "difficulties"), function(d)
			return {
				id = json_str(d.id),
				hp_pct = json_int(d.hpPct),
				damage_pct = json_int(d.damagePct),
				gold_pct = json_int(d.goldPct),
				xp_pct = json_int(d.xpPct),
			}
		end),
		enemy_classes = enemy_classes,
		crit_multiplier_pct = json_int(raw.critMultiplierPct),
		defend_damage_pct = json_int(raw.defendDamagePct),
		parry_reflect_pct = json_int(raw.parryReflectPct),
		monster_heal_pct = json_int(raw.monsterHealPct),
		boss_telegraph_every = json_int(raw.bossTelegraphEvery),
		boss_charge_damage_pct = json_int(raw.bossChargeDamagePct),
		caps = {
			crit_chance = json_int(caps.critChance),
			dodge = json_int(caps.dodge),
			parry = json_int(caps.parry),
			leech = json_int(caps.leech),
			protection = json_int(caps.protection),
		},
		magic_level_base = json_int(magic.base),
		magic_level_growth_pct = json_int(magic.growthPct),
		spell_levels = base.map(objects(raw, "spellLevels"), function(s)
			return {
				level = json_int(s.level),
				uses = json_int(s.uses),
				effect_pct = json_int(s.effectPct),
				mana_pct = json_int(s.manaPct),
			}
		end),
		starting_gold = json_int(raw.startingGold),
		starting_potions = base.map(objects(raw, "startingPotions"), function(p)
			return { json_str(p.potionId), json_int(p.quantity) }
		end),
		bag_capacity = json_int(raw.bagCapacity),
		item_level_per_tier = json_int(raw.itemLevelPerTier),
		item_score_weights = (int_map(raw.itemScoreWeights, enums.Stat, "Stat")),
		rarities = base.map(objects(raw, "rarities"), function(r)
			return {
				id = json_str(r.id),
				stat_pct = json_int(r.statPct),
				value_pct = json_int(r.valuePct),
				affix_min = json_int(r.affixMin),
				affix_max = json_int(r.affixMax),
			}
		end),
		rarity_weights = rarity_weights,
		merchant_stock_size = json_int(raw.merchantStockSize),
		merchant_markup_pct = json_int(raw.merchantMarkupPct),
		spell_status_damage_pct = json_int(raw.spellStatusDamagePct),
		auto_battle = auto_battle(json_obj(raw.autoBattle)),
	})
end

local function item(raw)
	local element = json.opt(raw, "element")
	local stats, stat_order = int_map(raw.stats, enums.Stat, "Stat")
	return {
		id = json_str(raw.id),
		name = json_str(raw.name),
		slot = member(enums.Slot, "Slot", raw.slot),
		type = json_str(raw.type),
		tier = json_int(raw.tier),
		element = element ~= nil and member(enums.Element, "Element", element) or nil,
		stats = stats,
		stat_order = stat_order,
		value = json_int(raw.value),
	}
end

local function optional(directory, name)
	if filesystem.exists(filesystem.join(directory, name .. ".json")) then
		return read(directory, name)
	end
	return json.object({ { name, json.array({}) } })
end

local function load(directory)
	return definitions.GameData.new({
		balance = balance(read(directory, "balance")),
		vocations = base.map(objects(read(directory, "vocations"), "vocations"), vocation),
		spells = base.map(objects(read(directory, "spells"), "spells"), spell),
		monsters = base.map(objects(read(directory, "monsters"), "monsters"), function(raw)
			return creature(raw, false)
		end),
		bosses = base.map(objects(read(directory, "bosses"), "bosses"), function(raw)
			return creature(raw, true)
		end),
		potions = base.map(objects(read(directory, "potions"), "potions"), function(p)
			return {
				id = json_str(p.id),
				name = json_str(p.name),
				resource = member(enums.Resource, "Resource", p.resource),
				min = json_int(p.min),
				max = json_int(p.max),
				price = json_int(p.price),
				unlock_round = json_int(p.unlockRound),
			}
		end),
		statuses = base.map(objects(read(directory, "statuses"), "statuses"), function(s)
			return {
				id = json_str(s.id),
				kind = member(enums.StatusKind, "StatusKind", s.kind),
				element = member(enums.Element, "Element", s.element),
				turns = json_int(s.turns),
			}
		end),
		items = base.map(objects(read(directory, "items"), "items"), item),
		affixes = base.map(objects(optional(directory, "affixes"), "affixes"), function(a)
			return {
				id = json_str(a.id),
				stat = member(enums.Stat, "Stat", a.stat),
				min = json_int(a.min),
				max = json_int(a.max),
				per_tier = json_int(a.perTier),
				slots = base.map(strs(a.slots), function(slot)
					return member(enums.Slot, "Slot", slot)
				end),
			}
		end),
		achievements = base.map(objects(optional(directory, "achievements"), "achievements"), function(a)
			return { id = json_str(a.id), type = json_str(a.type), value = json_int(a.value) }
		end),
		families = strs(read(directory, "families").families),
	})
end

function data_loader.load_game_data(shared_dir)
	local directory = filesystem.join(shared_dir, "data")
	local loaded, result = base.try({ "KeyError", "TypeError", "ValueError", "UnknownIdError" }, load, directory)
	if not loaded then
		base.raise("DataError", string.format("invalid game data in %s: %s", directory, result.message))
	end
	return result
end

return data_loader
