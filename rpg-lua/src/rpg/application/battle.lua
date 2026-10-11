--- Battle resolution, following docs/game-design.md §6. Every RNG call here is part of the contract.

local events_module = require("rpg.application.events")
local Progression = require("rpg.application.progression")
local base = require("rpg.domain.base")
local character = require("rpg.domain.character")
local entities = require("rpg.domain.entities")
local enums = require("rpg.domain.enums")
local formulas = require("rpg.domain.formulas")

local event, error_event, ErrorCode = events_module.event, events_module.error, events_module.ErrorCode
local extend = events_module.extend
local pct = formulas.pct
local Element, Resource, SpellKind = enums.Element, enums.Resource, enums.SpellKind
local StatusKind, Target = enums.StatusKind, enums.Target

local STUN = "stun"
local STUN_COOLDOWN_TURNS = 2

local BattleOutcome = { ONGOING = "ongoing", VICTORY = "victory", DEFEAT = "defeat" }

local Battle = base.class("Battle")
Battle.BattleOutcome = BattleOutcome
Battle.STUN = STUN
Battle.STUN_COOLDOWN_TURNS = STUN_COOLDOWN_TURNS

function Battle.new(data, rng, state)
	return setmetatable({ _data = data, _rng = rng, _state = state, _progression = Progression.new(data) }, Battle)
end

function Battle:_monster()
	local monster = self._state.monster
	if monster == nil then
		base.raise("RuntimeError", "battle without a monster")
	end
	return monster
end

function Battle:_sheet()
	return character.build_sheet(self._state.player, self._data)
end

function Battle:_class()
	return self._data.balance:enemy_class(self:_monster().enemy_class)
end

-- ── validation ──────────────────────────────────────────────────────────────

--- Returns an error event for an invalid command. Validation never consumes randomness.
function Battle:validate(command)
	local player = self._state.player
	if command.type == "cast" then
		local vocation = self._data:vocation(player.vocation_id)
		if not base.contains(vocation.spells, command.spell_id) then
			return error_event(ErrorCode.UNKNOWN_SPELL)
		end
		if player.mp < self:_spell_cost(self._data:spell(command.spell_id)) then
			return error_event(ErrorCode.NOT_ENOUGH_MANA)
		end
	elseif command.type == "potion" then
		if not self._data:has_potion(command.potion_id) then
			return error_event(ErrorCode.UNKNOWN_POTION)
		end
		if player:potion_count(command.potion_id) <= 0 then
			return error_event(ErrorCode.NO_POTION)
		end
	end
	return nil
end

function Battle:_spell_level(spell)
	return formulas.spell_level_for_uses(self._state.player.spell_uses[spell.id] or 0, self._data.balance.spell_levels)
end

function Battle:_spell_cost(spell)
	return pct(spell.mana, self:_spell_level(spell).mana_pct)
end

-- ── turn ────────────────────────────────────────────────────────────────────

--- Returns the events of the turn and its `BattleOutcome`.
function Battle:play_turn(command)
	local events = {}
	local player = self._state.player
	self:_player_action(command, events)
	while true do
		local outcome = self:_death_check()
		if outcome ~= nil then
			return events, outcome
		end
		self:_monster_phase(events)
		outcome = self:_death_check()
		if outcome ~= nil then
			return events, outcome
		end
		if self:_end_of_turn(events) then
			return events, BattleOutcome.DEFEAT
		end
		if not Battle._consume_stun(player.statuses) then
			return events, BattleOutcome.ONGOING
		end
		player.stun_cooldown = STUN_COOLDOWN_TURNS
		events[#events + 1] = event("player_stunned")
	end
end

--- Steps 2 and 4: a parried hit can kill the attacker, so the player is checked first.
function Battle:_death_check()
	if self._state.player.hp <= 0 then
		return BattleOutcome.DEFEAT
	end
	if self:_monster().hp <= 0 then
		return BattleOutcome.VICTORY
	end
	return nil
end

-- ── step 1: player action ───────────────────────────────────────────────────

function Battle:_player_action(command, events)
	if command.type == "attack" then
		self:_melee(events)
	elseif command.type == "cast" then
		self:_cast(self._data:spell(command.spell_id), events)
	elseif command.type == "potion" then
		self:_drink(command.potion_id, events)
	elseif command.type == "defend" then
		self._state.player.defending = true
		events[#events + 1] = event("player_defended")
	end
end

function Battle:_melee(events)
	local sheet = self:_sheet()
	if self:_monster_dodges(events) then
		return
	end
	local damage = pct(self._rng:roll(sheet.melee_min, sheet.melee_max), 100 + sheet.physical_damage)
	local crit
	damage, crit = self:_roll_crit(damage, sheet)
	damage = self:_resisted(damage, sheet.weapon_element)
	if self:_monster_parries(damage, sheet.weapon_element, events) then
		return
	end
	self:_hit_monster(damage)
	events[#events + 1] = event("player_attacked", { damage = damage, crit = crit, element = sheet.weapon_element })
	self:_leech(damage, sheet, events)
end

function Battle:_monster_dodges(events)
	if not self._rng:chance(self:_class().dodge) then
		return false
	end
	events[#events + 1] = event("monster_dodged")
	return true
end

--- Physical hits only: the monster takes nothing and reflects part of the hit (no mitigation).
function Battle:_monster_parries(damage, element, events)
	if element ~= Element.PHYSICAL or not self._rng:chance(self:_class().parry) then
		return false
	end
	local player = self._state.player
	local reflected = math.max(1, pct(damage, self._data.balance.parry_reflect_pct))
	player.hp = math.max(0, player.hp - reflected)
	events[#events + 1] = event("monster_parried", { reflected = reflected })
	return true
end

function Battle:_cast(spell, events)
	local player = self._state.player
	local sheet = self:_sheet()
	local level = self:_spell_level(spell)
	local cost = pct(spell.mana, level.mana_pct)
	player.mp = player.mp - cost
	if spell.kind == SpellKind.ATTACK and self:_monster_dodges(events) then
		extend(events, self._progression:after_cast(player, spell, cost))
		return
	end
	local bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level
	local amount =
		pct(pct(self._rng:roll(spell.min + bonus, spell.max + bonus), level.effect_pct), 100 + sheet.spell_power)

	if spell.kind == SpellKind.ATTACK then
		local damage, crit = self:_roll_crit(amount, sheet)
		damage = self:_resisted(damage, spell.element)
		if self:_monster_parries(damage, spell.element, events) then
			extend(events, self._progression:after_cast(player, spell, cost))
			return
		end
		self:_hit_monster(damage)
		events[#events + 1] = event(
			"spell_cast",
			{ spellId = spell.id, damage = damage, crit = crit, element = spell.element, mana = cost }
		)
		self:_leech(damage, sheet, events)
		local bonus_effect = spell.level3_bonus
		if level.level == 3 and bonus_effect.status ~= nil and self._rng:chance(bonus_effect.chance) then
			local per_turn = math.max(1, pct(damage, self._data.balance.spell_status_damage_pct))
			self:_apply_status(Target.MONSTER, bonus_effect.status, per_turn, events)
		end
	else
		local healed = math.min(amount, sheet.max_hp - player.hp)
		player.hp = player.hp + healed
		events[#events + 1] = event("spell_healed", { spellId = spell.id, amount = healed, mana = cost })
		if level.level == 3 and spell.level3_bonus.cleanse then
			for _, status in ipairs(base.copy_list(player.statuses)) do
				base.remove_value(player.statuses, status)
				events[#events + 1] = event("status_expired", { target = Target.PLAYER, status = status.status_id })
			end
		end
	end

	extend(events, self._progression:after_cast(player, spell, cost))
end

function Battle:_drink(potion_id, events)
	local player = self._state.player
	local sheet = self:_sheet()
	local potion = self._data:potion(potion_id)
	-- The key stays in the table when the last potion is drunk: saves keep `"potion": 0` like the reference.
	player.potions[potion_id] = player.potions[potion_id] - 1
	local amount = self._rng:roll(potion.min, potion.max)
	local restored
	if potion.resource == Resource.HP then
		restored = math.min(amount, sheet.max_hp - player.hp)
		player.hp = player.hp + restored
	else
		restored = math.min(amount, sheet.max_mp - player.mp)
		player.mp = player.mp + restored
	end
	events[#events + 1] = event("potion_used", { potionId = potion_id, amount = restored, resource = potion.resource })
end

function Battle:_roll_crit(damage, sheet)
	if self._rng:chance(sheet.crit_chance) then
		return pct(damage, self._data.balance.crit_multiplier_pct + sheet.crit_damage), true
	end
	return damage, false
end

function Battle:_resistance(element)
	return self._data:creature(self:_monster().creature_id):resistance(element)
end

function Battle:_resisted(damage, element)
	local resistance = self:_resistance(element)
	if resistance == 0 then
		return 0
	end
	return math.max(1, pct(damage, resistance))
end

function Battle:_hit_monster(damage)
	local monster = self:_monster()
	monster.hp = math.max(0, monster.hp - damage)
end

function Battle:_leech(damage, sheet, events)
	local player = self._state.player
	local hp_gain = math.min(pct(damage, sheet.life_leech), sheet.max_hp - player.hp)
	local mp_gain = math.min(pct(damage, sheet.mana_leech), sheet.max_mp - player.mp)
	if hp_gain <= 0 and mp_gain <= 0 then
		return
	end
	hp_gain, mp_gain = math.max(0, hp_gain), math.max(0, mp_gain)
	player.hp = player.hp + hp_gain
	player.mp = player.mp + mp_gain
	events[#events + 1] = event("leeched", { hp = hp_gain, mp = mp_gain })
end

-- ── step 3: monster phase ───────────────────────────────────────────────────

function Battle:_monster_phase(events)
	local monster = self:_monster()
	local balance = self._data.balance
	self:_tick(Target.MONSTER, monster.statuses, events)
	if monster.hp <= 0 then
		return
	end
	if Battle._consume_stun(monster.statuses) then
		monster.stun_cooldown = STUN_COOLDOWN_TURNS
		events[#events + 1] = event("monster_stunned")
		return
	end
	-- A healing monster does nothing else this turn; a boss does not advance its pattern.
	if monster.hp < monster.max_hp and self._rng:chance(self:_class().heal) then
		local healed = math.min(pct(monster.max_hp, balance.monster_heal_pct), monster.max_hp - monster.hp)
		monster.hp = monster.hp + healed
		events[#events + 1] = event("monster_healed", { amount = healed })
		return
	end

	if monster.is_boss then
		local every = balance.boss_telegraph_every
		local position = monster.boss_actions % (every + 1)
		monster.boss_actions = monster.boss_actions + 1
		local charge_id = self._data:creature(monster.creature_id).charge_attack
		if charge_id ~= nil and position == every - 1 then
			local charge = monster:attack(charge_id)
			events[#events + 1] = event("boss_telegraph", { attackId = charge.id, element = charge.element })
			return
		end
		if charge_id ~= nil and position == every then
			self:_resolve_monster_attack(monster:attack(charge_id), true, events)
			return
		end
	end

	local weights = {}
	for index, attack in ipairs(monster.attacks) do
		weights[index] = attack.weight
	end
	self:_resolve_monster_attack(monster.attacks[self._rng:weighted(weights)], false, events)
end

function Battle:_resolve_monster_attack(attack, charged, events)
	local player = self._state.player
	local sheet = self:_sheet()
	local balance = self._data.balance
	if self._rng:chance(sheet.dodge) then
		events[#events + 1] = event("attack_dodged", { attackId = attack.id })
		return
	end
	local damage = self._rng:roll(attack.min, attack.max)
	if charged then
		damage = pct(damage, balance.boss_charge_damage_pct)
	end
	if attack.element == Element.PHYSICAL and self._rng:chance(sheet.parry) then
		local reflected = math.max(1, pct(damage, balance.parry_reflect_pct))
		self:_hit_monster(reflected)
		events[#events + 1] = event("attack_parried", { attackId = attack.id, reflected = reflected })
		return
	end
	local crit = self._rng:chance(self:_class().crit)
	if crit then
		damage = pct(damage, balance.crit_multiplier_pct)
	end
	if attack.element == Element.PHYSICAL then
		damage = formulas.armor_mitigation(damage, sheet.armor)
	end
	damage = pct(damage, 100 - sheet:protection(attack.element))
	if player.defending then
		damage = pct(damage, balance.defend_damage_pct)
	end
	damage = math.max(1, damage)
	player.hp = math.max(0, player.hp - damage)
	events[#events + 1] = event("monster_attacked", {
		attackId = attack.id,
		damage = damage,
		element = attack.element,
		charged = charged,
		crit = crit,
	})
	if attack.status ~= nil and self._rng:chance(attack.status.chance) then
		local per_turn = math.max(1, pct(damage, attack.status.damage_pct))
		self:_apply_status(Target.PLAYER, attack.status.status, per_turn, events)
	end
end

-- ── step 5: end of turn ─────────────────────────────────────────────────────

--- Returns true when the player died from status ticks.
function Battle:_end_of_turn(events)
	local player = self._state.player
	local monster = self:_monster()
	self:_tick(Target.PLAYER, player.statuses, events)
	if player.hp <= 0 then
		return true
	end
	local sheet = self:_sheet()
	local hp_gain = math.max(0, math.min(sheet.hp_regen, sheet.max_hp - player.hp))
	local mp_gain = math.max(0, math.min(sheet.mp_regen, sheet.max_mp - player.mp))
	player.hp = player.hp + hp_gain
	player.mp = player.mp + mp_gain
	if hp_gain > 0 or mp_gain > 0 then
		events[#events + 1] = event("regenerated", { hp = hp_gain, mp = mp_gain })
	end
	player.defending = false
	player.stun_cooldown = math.max(0, player.stun_cooldown - 1)
	monster.stun_cooldown = math.max(0, monster.stun_cooldown - 1)
	self._state.turn = self._state.turn + 1
	return false
end

-- ── statuses ────────────────────────────────────────────────────────────────

local function find_status(statuses, status_id)
	for _, status in ipairs(statuses) do
		if status.status_id == status_id then
			return status
		end
	end
	return nil
end

function Battle:_apply_status(target, status_id, per_turn, events)
	local definition = self._data:status(status_id)
	local statuses, cooldown
	if target == Target.PLAYER then
		statuses, cooldown = self._state.player.statuses, self._state.player.stun_cooldown
	else
		local monster = self:_monster()
		statuses, cooldown = monster.statuses, monster.stun_cooldown
		if self:_resistance(definition.element) == 0 then
			return
		end
	end

	if definition.kind == StatusKind.STUN then
		if cooldown > 0 or find_status(statuses, STUN) ~= nil then
			return
		end
		statuses[#statuses + 1] = entities.ActiveStatus.new(STUN, definition.turns, 0)
		events[#events + 1] =
			event("status_applied", { target = target, status = STUN, turns = definition.turns, perTurn = 0 })
		return
	end

	local existing = find_status(statuses, status_id)
	if existing == nil then
		existing = entities.ActiveStatus.new(status_id, definition.turns, per_turn)
		statuses[#statuses + 1] = existing
	else
		existing.turns = definition.turns
		existing.per_turn = math.max(existing.per_turn, per_turn)
	end
	events[#events + 1] = event(
		"status_applied",
		{ target = target, status = status_id, turns = existing.turns, perTurn = existing.per_turn }
	)
end

function Battle:_tick(target, statuses, events)
	for _, status in ipairs(base.copy_list(statuses)) do
		local definition = self._data:status(status.status_id)
		if definition.kind == StatusKind.DOT then
			local damage = self:_status_damage(target, status.per_turn, definition.element)
			if target == Target.PLAYER then
				local player = self._state.player
				player.hp = math.max(0, player.hp - damage)
			else
				self:_hit_monster(damage)
			end
			events[#events + 1] =
				event("status_ticked", { target = target, status = status.status_id, damage = damage })
			status.turns = status.turns - 1
			if status.turns <= 0 then
				base.remove_value(statuses, status)
				events[#events + 1] = event("status_expired", { target = target, status = status.status_id })
			end
		end
	end
end

function Battle:_status_damage(target, per_turn, element)
	if target == Target.PLAYER then
		return math.max(1, pct(per_turn, 100 - self:_sheet():protection(element)))
	end
	local resistance = self:_resistance(element)
	return resistance == 0 and 0 or math.max(1, pct(per_turn, resistance))
end

function Battle._consume_stun(statuses)
	local stun = find_status(statuses, STUN)
	if stun == nil then
		return false
	end
	base.remove_value(statuses, stun)
	return true
end

return Battle
