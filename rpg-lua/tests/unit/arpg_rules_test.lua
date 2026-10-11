--- M8 rules (docs/game-design.md §3, §6, §8, §9): enemy classes, monster dodge/parry/heal/crit, parry reflects,
--- class drop tables, potion drops, spell level effects and the victory phase.

local commands = require("rpg.application.commands")
local spawner = require("rpg.application.spawner")
local base = require("rpg.domain.base")
local character = require("rpg.domain.character")
local definitions = require("rpg.domain.definitions")
local formulas = require("rpg.domain.formulas")
local Rng = require("rpg.domain.rng")
local fixture = require("support.engine_fixture")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "unit/arpg_rules"

local MAX_TURNS = 300

local pct = formulas.pct
local types, first, has_type = fixture.types, fixture.first, fixture.has_type
local calm, with_enemy_class, with_balance = helpers.calm, helpers.with_enemy_class, helpers.with_balance

local function error_events(code)
	return { { type = "error", code = code } }
end

local function fight(engine, damage, element)
	local monster = fixture.fight(engine)
	fixture.fixed_attack(monster, damage or 1, element)
	return monster
end

--- Finishes the current fight with melee hits (the monster is left with 1 HP before each hit).
local function kill(engine)
	for _ = 1, MAX_TURNS do
		local monster = engine.state.monster
		if monster == nil then
			break
		end
		monster.hp = 1
		engine.state.player.hp = 1000000
		local events = engine:step(commands.Attack())
		if engine.state.phase ~= "battle" then
			return events
		end
	end
	return error("the fight did not end")
end

local function physical_resistant_100(data)
	for _, monster in ipairs(data.monsters) do
		if monster:resistance("physical") == 100 then
			return monster.id
		end
	end
	return error("no monster with 100% physical resistance")
end

local function rarities(drops)
	return base.map(drops, function(drop)
		return drop.rarity
	end)
end

local function all_in(values, allowed)
	for _, value in ipairs(values) do
		if not base.contains(allowed, value) then
			return false
		end
	end
	return true
end

-- ── spawn ───────────────────────────────────────────────────────────────────

T.test(GROUP, "elite spawn scales stats and rewards", function()
	local data = helpers.data()
	local normal_data = with_enemy_class(calm(data), "normal", {})
	local elite_data = with_balance(normal_data, { elite_chance_pct = 100 })
	local difficulty = data.balance:difficulty("normal")
	local normal = spawner.spawn_monster(normal_data, Rng.new(5), 3, difficulty)
	local elite = spawner.spawn_monster(elite_data, Rng.new(5), 3, difficulty)
	local row = data.balance:enemy_class("elite")
	T.eq({ normal.enemy_class, elite.enemy_class }, { "normal", "elite" })
	T.eq(elite.creature_id, normal.creature_id)
	T.eq(elite.max_hp, pct(normal.max_hp, row.stat_pct))
	T.eq(elite.attacks[1].max, pct(normal.attacks[1].max, row.stat_pct))
	T.eq(elite.xp, pct(normal.xp, row.reward_pct))
	T.eq(elite.gold_max, pct(normal.gold_max, row.reward_pct))
end)

T.test(GROUP, "elite roll follows the monster pick", function()
	local data = helpers.data()
	local difficulty = data.balance:difficulty("normal")
	local rng = Rng.new(77)
	local counts = {}
	for index = 0, 299 do
		local monster = spawner.spawn_monster(data, rng, 1 + index % 9, difficulty)
		counts[monster.enemy_class] = (counts[monster.enemy_class] or 0) + 1
	end
	T.eq(base.sorted_keys(counts), { "elite", "normal" })
	T.ok(30 <= counts.elite and counts.elite <= 90, "elites: " .. counts.elite)
	local boss = spawner.spawn_monster(data, Rng.new(1), 10, difficulty)
	T.eq(boss.enemy_class, "boss")
end)

T.test(GROUP, "round started reports the enemy class", function()
	local elite_data = with_balance(helpers.data(), { elite_chance_pct = 100 })
	local engine = helpers.new_engine({ data = elite_data })
	local started = engine:step(commands.NextFight())[1]
	T.eq(started.enemyClass, "elite")
	T.eq(started.isBoss, false)
end)

-- ── monster dodge, parry, heal and crit ─────────────────────────────────────

T.test(GROUP, "monster dodge stops melee and spells", function()
	local engine = helpers.new_engine({ data = with_enemy_class(calm(helpers.data()), "normal", { dodge = 100 }) })
	local monster = fight(engine)
	local events = engine:step(commands.Attack())
	T.eq(events[1], { type = "monster_dodged" })
	T.no(has_type(events, "player_attacked"))
	T.eq(monster.hp, monster.max_hp)
	local player = engine.state.player
	player.mp = 1000
	events = engine:step(commands.Cast("brutal_strike"))
	T.eq(events[1], { type = "monster_dodged" })
	T.ok(player.mp < 1000)
	T.eq(player.spell_uses.brutal_strike, 1)
end)

T.test(GROUP, "monster parry reflects physical hits", function()
	local engine = helpers.new_engine({ data = with_enemy_class(calm(helpers.data()), "normal", { parry = 100 }) })
	local monster = fight(engine)
	local player = engine.state.player
	local events = engine:step(commands.Defend())
	T.no(has_type(events, "monster_parried"))
	local hp = player.hp
	events = engine:step(commands.Attack())
	local parried = events[1]
	T.eq(parried.type, "monster_parried")
	T.eq(math.type(parried.reflected), "integer")
	T.ok(parried.reflected >= 1)
	T.no(has_type(events, "leeched"))
	T.eq(monster.hp, monster.max_hp)
	local hits, regen = 0, 0
	for _, event in ipairs(events) do
		if event.type == "monster_attacked" then
			hits = hits + event.damage
		elseif event.type == "regenerated" then
			regen = regen + event.hp
		end
	end
	T.eq(player.hp, hp - parried.reflected - hits + regen)
end)

T.test(GROUP, "monster parry reflect can kill the player", function()
	local engine = helpers.new_engine({ data = with_enemy_class(calm(helpers.data()), "normal", { parry = 100 }) })
	local monster = fight(engine)
	engine.state.player.hp = 1
	local events = engine:step(commands.Attack())
	T.eq(types(events), { "monster_parried", "player_died" })
	T.eq(engine.state.phase, "game_over")
	T.eq(monster.hp, monster.max_hp)
end)

T.test(GROUP, "monster parry ignores non physical spells", function()
	local engine = helpers.new_engine({
		vocation = "mage",
		data = with_enemy_class(calm(helpers.data()), "normal", { parry = 100 }),
	})
	fight(engine)
	engine.state.player.mp = 1000
	local events = engine:step(commands.Cast("flame_strike"))
	T.no(has_type(events, "monster_parried"))
	T.ok(has_type(events, "spell_cast"))
end)

T.test(GROUP, "monster heals instead of attacking", function()
	local data = helpers.data()
	local engine = helpers.new_engine({ data = with_enemy_class(calm(data), "normal", { heal = 100 }) })
	local monster = fight(engine)
	local events = engine:step(commands.Defend())
	T.no(has_type(events, "monster_healed"))
	T.ok(has_type(events, "monster_attacked"))
	monster.hp = monster.max_hp - 5
	events = engine:step(commands.Defend())
	T.ok(fixture.has(events, { type = "monster_healed", amount = 5 }))
	T.no(has_type(events, "monster_attacked"))
	monster.hp = 100
	events = engine:step(commands.Defend())
	T.ok(fixture.has(events, { type = "monster_healed", amount = pct(monster.max_hp, data.balance.monster_heal_pct) }))
end)

T.test(GROUP, "healing boss does not advance its pattern", function()
	local engine = helpers.new_engine({ data = with_enemy_class(calm(helpers.data()), "boss", { heal = 100 }) })
	engine.state.round = 9
	local boss = fixture.fight(engine)
	boss.hp = boss.max_hp - 1
	engine.state.player.hp = 1000000
	engine:step(commands.Defend())
	T.eq(boss.boss_actions, 0)
end)

T.test(GROUP, "monster crit multiplies raw damage", function()
	for _, case in ipairs({ { 0, 100 }, { 100, 150 } }) do
		local crit, expected = case[1], case[2]
		local engine = helpers.new_engine({ data = with_enemy_class(calm(helpers.data()), "normal", { crit = crit }) })
		fight(engine, 100, "fire")
		engine.state.player.hp = 1000
		local hit = first(engine:step(commands.Attack()), "monster_attacked")
		T.eq(hit.crit, crit == 100)
		T.eq(hit.damage, expected)
	end
end)

local function parry_data(data)
	local shield = {
		id = "test_parry_shield",
		name = "Test Shield",
		slot = "shield",
		type = "shield",
		tier = 0,
		stats = { parry = 100 },
		stat_order = { "parry" },
		value = 10,
	}
	local caps = data.balance.caps
	local items = base.copy_list(data.items)
	items[#items + 1] = shield
	local game_data = calm(data):with({ items = items })
	return with_balance(game_data, {
		caps = { crit_chance = caps.crit_chance, dodge = 0, parry = 100, leech = 25, protection = 75 },
	})
end

T.test(GROUP, "player parry reflects damage to the monster", function()
	local game_data = parry_data(helpers.data())
	local engine = helpers.new_engine({ data = game_data })
	engine.state.player.equipment.shield = fixture.item(90, "test_parry_shield", "common", 0)
	T.eq(character.build_sheet(engine.state.player, game_data).parry, 100)
	local monster = fight(engine, 50)
	local events = engine:step(commands.Defend())
	T.ok(fixture.has(events, { type = "attack_parried", attackId = "test_hit", reflected = 10 }))
	T.eq(monster.hp, monster.max_hp - 10)
	T.eq(engine.state.stats.parries, 1)
	monster.hp = 5
	events = engine:step(commands.Defend())
	T.ok(has_type(events, "monster_killed"))
	T.eq(engine.state.phase, "merchant")
end)

-- ── victory, drops and potions ──────────────────────────────────────────────

T.test(GROUP, "normal drop table", function()
	local data = helpers.data()
	local engine = helpers.new_engine({ data = with_enemy_class(calm(data), "normal", { drop_chance_pct = 100 }) })
	fight(engine)
	local events = kill(engine)
	local drops = fixture.all(events, "item_dropped")
	T.eq(#drops, 1)
	T.ok(all_in(rarities(drops), { "common", "rare" }))
	T.no(has_type(events, "potion_dropped"))
	engine = helpers.new_engine({ data = with_enemy_class(calm(data), "normal", { drop_chance_pct = 0 }) })
	fight(engine)
	T.no(has_type(kill(engine), "item_dropped"))
end)

T.test(GROUP, "elite drops a good item and maybe a potion", function()
	local data = helpers.data()
	local game_data = with_enemy_class(calm(data), "elite", { potion_drop_pct = 100 })
	game_data = with_balance(game_data, { elite_chance_pct = 100 })
	local engine = helpers.new_engine({ data = game_data })
	fight(engine)
	local before = {}
	for potion_id, quantity in pairs(engine.state.player.potions) do
		before[potion_id] = quantity
	end
	local events = kill(engine)
	local drops = fixture.all(events, "item_dropped")
	T.eq(#drops, 1)
	T.ok(all_in(rarities(drops), { "rare", "legendary" }))
	local potion_id = first(events, "potion_dropped").potionId
	T.ok(data:potion(potion_id).unlock_round <= 1)
	T.eq(engine.state.player.potions[potion_id], (before[potion_id] or 0) + 1)
	T.eq(engine.state.stats.potions_dropped[potion_id], 1)
	T.eq(engine.state.stats.elites_killed, 1)
	T.eq(first(events, "monster_killed").enemyClass, "elite")
end)

T.test(GROUP, "potion drop without unlocked potions consumes nothing", function()
	local data = helpers.data()
	local locked = base.map(data.potions, function(potion)
		return definitions.replace(potion, { unlock_round = 99 })
	end)
	local game_data = with_enemy_class(calm(data), "normal", { drop_chance_pct = 0, potion_drop_pct = 100 })
	local engine = helpers.new_engine({ data = game_data:with({ potions = locked }) })
	fight(engine)
	T.no(has_type(kill(engine), "potion_dropped"))
end)

T.test(GROUP, "boss drops several top items", function()
	local data = helpers.data()
	local engine = helpers.new_engine({ data = calm(data) })
	engine.state.round = 9
	fight(engine)
	local events = kill(engine)
	local drops = fixture.all(events, "item_dropped")
	T.eq(#drops, data.balance:enemy_class("boss").drops)
	T.ok(all_in(rarities(drops), { "legendary", "mythic" }))
end)

T.test(GROUP, "full bag auto sells drops", function()
	local data = helpers.data()
	local engine = helpers.new_engine({ data = with_enemy_class(calm(data), "normal", { drop_chance_pct = 100 }) })
	local player = engine.state.player
	for offset = 0, data.balance.bag_capacity - 1 do
		player.bag[#player.bag + 1] = fixture.item(500 + offset, "sword", "common", 0)
	end
	fight(engine)
	local events = kill(engine)
	T.ok(has_type(events, "item_auto_sold"))
	T.eq(#player.bag, data.balance.bag_capacity)
end)

local function final_victory(engine)
	engine.state.round = engine.data.balance.final_round - 1
	fight(engine)
	return kill(engine)
end

T.test(GROUP, "beating the final boss enters the victory phase", function()
	local data = helpers.data()
	local engine = helpers.new_engine({ data = calm(data) })
	local events = final_victory(engine)
	T.eq(events[#events], { type = "run_won", round = data.balance.final_round })
	T.no(has_type(events, "merchant_entered"))
	T.eq(engine.state.phase, "victory")
	T.eq(engine.state.won, true)
	local rng_state = engine:rng_state()
	local stock = base.copy_list(engine.state.merchant_stock)
	local rejected = {
		commands.Attack(),
		commands.Defend(),
		commands.NextFight(),
		commands.BuyPotion("health_potion", 1),
		commands.Equip(1),
		commands.SellItem(1),
	}
	for _, command in ipairs(rejected) do
		T.eq(engine:step(command), error_events("invalid_phase"), command.type)
	end
	T.eq(engine:rng_state(), rng_state)
	T.eq(engine.state.merchant_stock, stock)
	T.eq(engine:step(commands.EndRun()), { { type = "run_ended", won = true } })
	T.eq(engine.state.phase, "game_over")
	T.eq(engine.state.death_cause, nil)
	T.eq(engine:step(commands.ContinueRun()), error_events("invalid_phase"))
end)

T.test(GROUP, "continue run enters the merchant", function()
	local data = helpers.data()
	local engine = helpers.new_engine({ data = calm(data) })
	final_victory(engine)
	local events = engine:step(commands.ContinueRun())
	T.eq(events, { { type = "merchant_entered", round = data.balance.final_round } })
	T.eq(engine.state.phase, "merchant")
	T.eq(#engine.state.merchant_stock, data.balance.merchant_stock_size)
	engine:step(commands.NextFight())
	T.eq(engine.state.round, data.balance.final_round + 1)
	T.eq(engine.state.won, true)
end)

T.test(GROUP, "victory commands are rejected outside the victory phase", function()
	local engine = helpers.new_engine()
	T.eq(engine:step(commands.EndRun()), error_events("invalid_phase"))
	T.eq(engine:step(commands.ContinueRun()), error_events("invalid_phase"))
end)

-- ── spell levels ────────────────────────────────────────────────────────────

T.test(GROUP, "spell level effect scales damage", function()
	local data = helpers.data()
	for _, case in ipairs({ { 0, 100 }, { 20, 150 }, { 50, 200 } }) do
		local uses, effect = case[1], case[2]
		local engine = helpers.new_engine({ data = calm(data) })
		local monster = fight(engine)
		engine.state.monster = definitions.replace(monster, { creature_id = physical_resistant_100(data) })
		local player = engine.state.player
		player.spell_uses.brutal_strike = uses
		player.mp = 1000
		local rng = Rng.new(engine:rng_state())
		local spell = data:spell("brutal_strike")
		local bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level
		local rolled = rng:roll(spell.min + bonus, spell.max + bonus)
		local cast = first(engine:step(commands.Cast("brutal_strike")), "spell_cast")
		T.eq(cast.damage, math.max(1, pct(rolled, effect)), uses .. " uses")
	end
end)

T.test(GROUP, "spell level effect scales healing", function()
	local data = helpers.data()
	local engine = helpers.new_engine({ data = calm(data) })
	fight(engine)
	local player = engine.state.player
	player.spell_uses.wound_cleansing = 20
	player.mp = 1000
	player.hp = 1
	local rng = Rng.new(engine:rng_state())
	local spell = data:spell("wound_cleansing")
	local bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level
	local expected = pct(rng:roll(spell.min + bonus, spell.max + bonus), 150)
	local room = character.build_sheet(player, data).max_hp - player.hp
	local healed = first(engine:step(commands.Cast("wound_cleansing")), "spell_healed")
	T.eq(healed.amount, math.min(expected, room))
end)
