--- Immutable game definitions loaded from shared/data (see docs/data-format.md).
--- A definition is a table with the fields of the reference dataclass, in snake_case; lists keep the file order.
---
--- Fields per type:
---   StatusOnHit     status, chance, damage_pct
---   MonsterAttack   id, element, min, max, weight, status (StatusOnHit or nil)
---   MonsterDef      id, name, tier, family, hp, xp, gold_min, gold_max, attacks, resistances, is_boss, charge_attack
---   Level3Bonus     status (or nil), chance, cleanse
---   SpellDef        id, name, words, kind, element, mana, min, max, per_level, per_magic_level, level3_bonus
---   VocationDef     id, name, start_hp, start_mp, hp_per_level, mp_per_level, hp_regen, mp_regen, melee_min,
---                   melee_max, melee_per_level, weapon_types, shield_types, starter_weapon, spells
---   PotionDef       id, name, resource, min, max, price, unlock_round
---   StatusDef       id, kind, element, turns
---   ItemDef         id, name, slot, type, tier, element (or nil), stats, stat_order, value
---   AffixDef        id, stat, min, max, per_tier, slots
---   AchievementDef  id, type, value
---   DifficultyDef   id, hp_pct, damage_pct, gold_pct, xp_pct
---   RarityDef       id, stat_pct, value_pct, affix_min, affix_max
---   EnemyClassDef   id, stat_pct, reward_pct, dodge, parry, crit, heal, drop_chance_pct, drops, potion_drop_pct,
---                   rarity_weights
---   AutoBattleModeDef id, offense, support_every
---   SpellLevelDef   level, uses, effect_pct, mana_pct
---   Caps            crit_chance, dodge, parry, leech, protection

local base = require("rpg.domain.base")

local definitions = {}

local function unknown_id(id)
	base.raise("UnknownIdError", "'" .. tostring(id) .. "'")
end

local function find(list, id)
	for _, item in ipairs(list) do
		if item.id == id then
			return item
		end
	end
	return unknown_id(id)
end

--- A copy of `definition` with some fields replaced (Python's `dataclasses.replace`).
function definitions.replace(definition, overrides)
	local copy = {}
	for key, value in pairs(definition) do
		copy[key] = value
	end
	for key, value in pairs(overrides) do
		copy[key] = value
	end
	return setmetatable(copy, getmetatable(definition))
end

-- ── MonsterDef ──────────────────────────────────────────────────────────────

local MonsterDef = base.class("MonsterDef")
definitions.MonsterDef = MonsterDef

function MonsterDef.new(fields)
	fields.is_boss = fields.is_boss or false
	return setmetatable(fields, MonsterDef)
end

function MonsterDef:resistance(element)
	return self.resistances[element] or 100
end

function MonsterDef:attack(attack_id)
	return find(self.attacks, attack_id)
end

-- ── AutoBattleDef: heal_below_pct, mana_below_pct, emergency_heal_below_pct, modes ──

local AutoBattleDef = base.class("AutoBattleDef")
definitions.AutoBattleDef = AutoBattleDef

function AutoBattleDef.new(fields)
	return setmetatable(fields, AutoBattleDef)
end

function AutoBattleDef:mode(mode_id)
	return find(self.modes, mode_id)
end

-- ── Balance ─────────────────────────────────────────────────────────────────
-- rounds_per_tier, cycle_stat_pct, cycle_reward_pct, position_pct, final_round, elite_chance_pct, difficulties,
-- enemy_classes, crit_multiplier_pct, defend_damage_pct, parry_reflect_pct, monster_heal_pct, boss_telegraph_every,
-- boss_charge_damage_pct, caps, magic_level_base, magic_level_growth_pct, spell_levels, starting_gold,
-- starting_potions ({ { potion_id, quantity }, … }), bag_capacity, item_level_per_tier, item_score_weights,
-- rarities, rarity_weights, merchant_stock_size, merchant_markup_pct, spell_status_damage_pct, auto_battle

local Balance = base.class("Balance")
definitions.Balance = Balance

function Balance.new(fields)
	return setmetatable(fields, Balance)
end

function Balance:difficulty(difficulty_id)
	return find(self.difficulties, difficulty_id)
end

function Balance:enemy_class(class_id)
	return find(self.enemy_classes, class_id)
end

function Balance:rarity(rarity_id)
	return find(self.rarities, rarity_id)
end

-- ── GameData ────────────────────────────────────────────────────────────────

local GameData = base.class("GameData")
definitions.GameData = GameData

local LISTS = { "vocations", "spells", "monsters", "bosses", "potions", "statuses", "items", "affixes", "achievements" }

local function index(...)
	local result = {}
	for _, list in ipairs({ ... }) do
		for _, item in ipairs(list) do
			result[item.id] = item
		end
	end
	return result
end

--- `fields`: balance plus the lists vocations, spells, monsters, bosses, potions, statuses, items, affixes,
--- achievements and families. The lookup indexes are built once here.
function GameData.new(fields)
	local self = setmetatable({ balance = fields.balance, families = fields.families or {} }, GameData)
	for _, name in ipairs(LISTS) do
		self[name] = fields[name] or {}
	end
	self.tier_count = #self.bosses
	self._vocations = index(self.vocations)
	self._spells = index(self.spells)
	self._creatures = index(self.monsters, self.bosses)
	self._potions = index(self.potions)
	self._statuses = index(self.statuses)
	self._items = index(self.items)
	-- Every "sorted by id" list of the rules is sorted once: filtering a sorted list keeps it sorted.
	self.items_by_id = base.sorted(self.items, base.by_id)
	self.affixes_by_id = base.sorted(self.affixes, base.by_id)
	self._monsters_by_tier = {}
	for _, monster in ipairs(base.sorted(self.monsters, base.by_id)) do
		local group = self._monsters_by_tier[monster.tier] or {}
		group[#group + 1] = monster
		self._monsters_by_tier[monster.tier] = group
	end
	self._bosses_by_tier = {}
	for _, boss in ipairs(self.bosses) do
		self._bosses_by_tier[boss.tier] = boss
	end
	return self
end

--- A copy of the data with some fields replaced (used by tests to change one balance number or one list).
function GameData:with(overrides)
	local fields = { balance = self.balance, families = self.families }
	for _, name in ipairs(LISTS) do
		fields[name] = self[name]
	end
	for key, value in pairs(overrides) do
		fields[key] = value
	end
	return GameData.new(fields)
end

local function lookup(indexed, key)
	return indexed[key] or unknown_id(key)
end

function GameData:vocation(vocation_id)
	return lookup(self._vocations, vocation_id)
end

function GameData:spell(spell_id)
	return lookup(self._spells, spell_id)
end

function GameData:creature(creature_id)
	return lookup(self._creatures, creature_id)
end

function GameData:potion(potion_id)
	return lookup(self._potions, potion_id)
end

function GameData:has_potion(potion_id)
	return self._potions[potion_id] ~= nil
end

function GameData:status(status_id)
	return lookup(self._statuses, status_id)
end

function GameData:item(item_id)
	return lookup(self._items, item_id)
end

function GameData:monsters_in_tier(tier)
	return self._monsters_by_tier[tier] or {}
end

function GameData:boss_of_tier(tier)
	return lookup(self._bosses_by_tier, tier)
end

return definitions
