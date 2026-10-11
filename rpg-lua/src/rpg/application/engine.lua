--- The game engine: a pure state machine `step(command) -> events` (docs/architecture.md).

local auto_equip = require("rpg.application.auto_equip")
local Battle = require("rpg.application.battle")
local events_module = require("rpg.application.events")
local loot = require("rpg.application.loot")
local Merchant = require("rpg.application.merchant")
local Progression = require("rpg.application.progression")
local run_state = require("rpg.application.run_state")
local spawner = require("rpg.application.spawner")
local base = require("rpg.domain.base")
local character = require("rpg.domain.character")
local entities = require("rpg.domain.entities")
local enums = require("rpg.domain.enums")
local formulas = require("rpg.domain.formulas")
local Rng = require("rpg.domain.rng")

local event, error_event, ErrorCode = events_module.event, events_module.error, events_module.ErrorCode
local extend = events_module.extend
local Phase = enums.Phase
local BattleOutcome = Battle.BattleOutcome

-- The phase each command is valid in.
local COMMAND_PHASE = {
	attack = Phase.BATTLE,
	cast = Phase.BATTLE,
	potion = Phase.BATTLE,
	defend = Phase.BATTLE,
	next_fight = Phase.MERCHANT,
	buy_potion = Phase.MERCHANT,
	sell_item = Phase.MERCHANT,
	equip = Phase.MERCHANT,
	unequip = Phase.MERCHANT,
	buy_stock_item = Phase.MERCHANT,
	end_run = Phase.VICTORY,
	continue_run = Phase.VICTORY,
}

local GameEngine = base.class("GameEngine")

function GameEngine.new(data, state, rng)
	return setmetatable({ data = data, state = state, _rng = rng }, GameEngine)
end

function GameEngine:rng_state()
	return self._rng.state
end

--- Returns the engine and the events of the run creation.
function GameEngine.new_run(data, config, seed)
	local found, vocation = base.try({ "UnknownIdError" }, function()
		local definition = data:vocation(config.vocation_id)
		data.balance:difficulty(config.difficulty_id)
		return definition
	end)
	if not found then
		base.raise("ValueError", "invalid run config: " .. vocation.message)
	end

	local potions = {}
	for _, potion in ipairs(data.balance.starting_potions) do
		potions[potion[1]] = potion[2]
	end
	local player = entities.Player.new({
		name = config.name,
		vocation_id = vocation.id,
		hp = vocation.start_hp,
		mp = vocation.start_mp,
		gold = data.balance.starting_gold,
		potions = potions,
	})
	local state = run_state.RunState.new({ seed = seed, config = config, player = player })
	local starter = data:item(vocation.starter_weapon)
	player.equipment[starter.slot] = entities.ItemInstance.new({
		uid = state:take_item_uid(),
		item_id = starter.id,
		rarity = "common",
		tier = starter.tier,
	})
	local engine = GameEngine.new(data, state, Rng.new(seed))
	local events = { event("run_started", { seed = seed, vocation = vocation.id, difficulty = config.difficulty_id }) }
	extend(events, Merchant.new(data, engine._rng, state):enter())
	return engine, events
end

function GameEngine.restore(data, state, rng_state)
	return GameEngine.new(data, state, Rng.new(rng_state))
end

function GameEngine:step(command)
	local events = self:_dispatch(command)
	self.state.stats:record(events, self.state.round)
	return events
end

function GameEngine:_dispatch(command)
	local expected = COMMAND_PHASE[command.type]
	if expected == nil then
		base.raise("ValueError", "unknown command type: " .. tostring(command.type))
	end
	if self.state.phase ~= expected then
		return { error_event(ErrorCode.INVALID_PHASE) }
	end
	if expected == Phase.BATTLE then
		return self:_battle_turn(command)
	elseif command.type == "next_fight" then
		return self:_next_fight()
	elseif expected == Phase.MERCHANT then
		return Merchant.new(self.data, self._rng, self.state):handle(command)
	elseif command.type == "end_run" then
		return self:_end_run()
	end
	return self:_continue_run()
end

function GameEngine:_next_fight()
	local state = self.state
	state.round = state.round + 1
	local difficulty = self.data.balance:difficulty(state.config.difficulty_id)
	local monster, info = spawner.spawn_monster(self.data, self._rng, state.round, difficulty)
	state.monster = monster
	state.phase = Phase.BATTLE
	state.turn = 1
	state.merchant_stock = {}
	return {
		event("round_started", {
			round = state.round,
			tier = info.tier,
			cycle = info.cycle,
			monsterId = monster.creature_id,
			isBoss = monster.is_boss,
			enemyClass = monster.enemy_class,
			hp = monster.hp,
		}),
	}
end

function GameEngine:_battle_turn(command)
	local battle = Battle.new(self.data, self._rng, self.state)
	local invalid = battle:validate(command)
	if invalid ~= nil then
		return { invalid }
	end
	local events, outcome = battle:play_turn(command)
	if outcome == BattleOutcome.VICTORY then
		extend(events, self:_victory())
	elseif outcome == BattleOutcome.DEFEAT then
		extend(events, self:_defeat())
	end
	return events
end

function GameEngine:_victory()
	local state = self.state
	local player = state.player
	local monster = state.monster
	if monster == nil then
		base.raise("RuntimeError", "victory without a monster")
	end
	local events = {
		event("monster_killed", {
			monsterId = monster.creature_id,
			isBoss = monster.is_boss,
			enemyClass = monster.enemy_class,
		}),
	}
	extend(events, Progression.new(self.data):gain_experience(player, monster.xp))

	local gold = self._rng:roll(monster.gold_min, monster.gold_max)
	player.gold = player.gold + gold
	events[#events + 1] = event("gold_looted", { amount = gold })
	extend(events, self:_drops(monster))
	if state.config.auto_equip then
		extend(events, auto_equip.auto_equip(state, self.data))
	end

	player.statuses = {}
	player.stun_cooldown = 0
	player.defending = false
	local sheet = character.build_sheet(player, self.data)
	player.hp = math.min(player.hp, sheet.max_hp)
	player.mp = math.min(player.mp, sheet.max_mp)
	state.monster = nil
	state.turn = 0
	if state.round == self.data.balance.final_round then
		state.won = true
		state.phase = Phase.VICTORY
		events[#events + 1] = event("run_won", { round = state.round })
		return events
	end
	state.phase = Phase.MERCHANT
	extend(events, Merchant.new(self.data, self._rng, state):enter())
	return events
end

--- One rule for the three classes: chance(100) and chance(0) consume nothing (docs/game-design.md §8).
function GameEngine:_drops(monster)
	local row = self.data.balance:enemy_class(monster.enemy_class)
	local events = {}
	if self._rng:chance(row.drop_chance_pct) then
		for _ = 1, row.drops do
			extend(events, self:_drop_item(row))
		end
	end
	if self._rng:chance(row.potion_drop_pct) then
		extend(events, self:_drop_potion())
	end
	return events
end

function GameEngine:_drop_item(row)
	local state = self.state
	local player = state.player
	local balance = self.data.balance
	local item = loot.generate_item(self.data, self._rng, {
		vocation = self.data:vocation(player.vocation_id),
		tier = formulas.round_info(state.round, balance, self.data.tier_count).tier,
		weights = row.rarity_weights,
		uid = state.next_item_uid,
	})
	if item == nil then
		return {}
	end
	state:take_item_uid()
	local events = { event("item_dropped", { uid = item.uid, itemId = item.item_id, rarity = item.rarity }) }
	if #player.bag >= balance.bag_capacity then
		local value = character.item_value(item, self.data)
		player.gold = player.gold + value
		events[#events + 1] = event("item_auto_sold", { uid = item.uid, itemId = item.item_id, gold = value })
	else
		player.bag[#player.bag + 1] = item
	end
	return events
end

function GameEngine:_drop_potion()
	local state = self.state
	local unlocked = base.filter(self.data.potions, function(potion)
		return potion.unlock_round <= state.round
	end)
	if #unlocked == 0 then
		return {}
	end
	local potion = self._rng:pick(unlocked)
	state.player.potions[potion.id] = state.player:potion_count(potion.id) + 1
	return { event("potion_dropped", { potionId = potion.id }) }
end

function GameEngine:_end_run()
	local state = self.state
	state.phase = Phase.GAME_OVER
	state.death_cause = nil
	return { event("run_ended", { won = state.won }) }
end

function GameEngine:_continue_run()
	local state = self.state
	state.phase = Phase.MERCHANT
	return Merchant.new(self.data, self._rng, state):enter()
end

function GameEngine:_defeat()
	local state = self.state
	local monster = state.monster
	local monster_id = monster == nil and "" or monster.creature_id
	state.phase = Phase.GAME_OVER
	state.death_cause = monster_id
	return { event("player_died", { monsterId = monster_id, round = state.round }) }
end

return GameEngine
