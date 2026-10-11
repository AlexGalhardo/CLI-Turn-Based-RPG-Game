local commands = require("rpg.application.commands")
local character = require("rpg.domain.character")
local definitions = require("rpg.domain.definitions")
local entities = require("rpg.domain.entities")
local fixture = require("support.engine_fixture")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "unit/battle"

local ActiveStatus = entities.ActiveStatus
local first, fight, fixed_attack = fixture.first, fixture.fight, fixture.fixed_attack

local function status_on_hit(status, chance, damage_pct)
	return { status = status, chance = chance, damage_pct = damage_pct }
end

local function error_events(code)
	return { { type = "error", code = code } }
end

T.test(GROUP, "melee damage is within sheet range", function()
	local data = helpers.data()
	local engine = helpers.new_engine()
	fight(engine)
	local sheet = character.build_sheet(engine.state.player, data)
	local resistance = data:creature(engine.state.monster.creature_id):resistance("physical")
	local events = engine:step(commands.Attack())
	local hit = first(events, "player_attacked")
	local low = math.max(1, sheet.melee_min * resistance // 100)
	local high = sheet.melee_max * resistance // 100
	T.eq(hit.crit, false)
	T.eq(math.type(hit.damage), "integer")
	T.ok(low <= hit.damage and hit.damage <= math.max(high, low))
end)

T.test(GROUP, "invalid battle commands do not consume rng", function()
	local engine = helpers.new_engine()
	T.eq(engine:step(commands.Attack()), error_events("invalid_phase"))
	fight(engine)
	local rng_state = engine:rng_state()
	T.eq(engine:step(commands.Cast("flame_strike")), error_events("unknown_spell"))
	engine.state.player.mp = 0
	T.eq(engine:step(commands.Cast("brutal_strike")), error_events("not_enough_mana"))
	fixture.clear(engine.state.player.potions)
	T.eq(engine:step(commands.UsePotion("health_potion")), error_events("no_potion"))
	T.eq(engine:step(commands.UsePotion("elixir")), error_events("unknown_potion"))
	T.eq(engine:step(commands.NextFight()), error_events("invalid_phase"))
	T.eq(engine:step(commands.BuyPotion("health_potion", 1)), error_events("invalid_phase"))
	T.eq(engine:rng_state(), rng_state)
end)

T.test(GROUP, "defend halves incoming damage", function()
	local engine = helpers.new_engine()
	fixed_attack(fight(engine), 100)
	engine.state.player.hp = 150
	local events = engine:step(commands.Defend())
	T.eq(first(events, "monster_attacked").damage, 50)
	T.eq(engine.state.player.defending, false)
end)

T.test(GROUP, "undefended damage and death", function()
	local engine = helpers.new_engine()
	local monster = fight(engine)
	fixed_attack(monster, 1000)
	local events = engine:step(commands.Attack())
	T.eq(events[#events], { type = "player_died", monsterId = monster.creature_id, round = 1 })
	T.eq(engine.state.phase, "game_over")
	T.eq(engine.state.death_cause, monster.creature_id)
	T.eq(engine:step(commands.Attack()), error_events("invalid_phase"))
	T.eq(engine:step(commands.NextFight()), error_events("invalid_phase"))
end)

T.test(GROUP, "monster status applies and ticks", function()
	local engine = helpers.new_engine()
	fixed_attack(fight(engine), 10, "fire", status_on_hit("burn", 100, 50))
	local events = engine:step(commands.Defend())
	T.eq(
		first(events, "status_applied"),
		{ type = "status_applied", target = "player", status = "burn", turns = 3, perTurn = 2 }
	)
	T.eq(first(events, "status_ticked"), { type = "status_ticked", target = "player", status = "burn", damage = 2 })
	T.eq(engine.state.player.statuses, { ActiveStatus.new("burn", 2, 2) })
end)

T.test(GROUP, "reapplying status refreshes turns and keeps higher damage", function()
	local engine = helpers.new_engine()
	fixed_attack(fight(engine), 10, "fire", status_on_hit("burn", 100, 50))
	local statuses = engine.state.player.statuses
	statuses[#statuses + 1] = ActiveStatus.new("burn", 1, 9)
	engine:step(commands.Defend())
	T.eq(engine.state.player.statuses, { ActiveStatus.new("burn", 2, 9) })
end)

T.test(GROUP, "status expires", function()
	local engine = helpers.new_engine()
	fixed_attack(fight(engine), 1)
	local statuses = engine.state.player.statuses
	statuses[#statuses + 1] = ActiveStatus.new("bleed", 1, 1)
	local events = engine:step(commands.Defend())
	T.ok(fixture.has_type(events, "status_expired"))
	T.eq(engine.state.player.statuses, {})
end)

T.test(GROUP, "player stun skips turn and has cooldown", function()
	local engine = helpers.new_engine()
	fixed_attack(fight(engine), 1, "physical", status_on_hit("stun", 100, 0))
	local events = engine:step(commands.Defend())
	T.eq(fixture.count(events, "monster_attacked"), 2)
	T.ok(fixture.has_type(events, "player_stunned"))
	T.eq(fixture.count(events, "status_applied"), 1)
	T.eq(engine.state.player.stun_cooldown, 1)
	events = engine:step(commands.Defend())
	T.no(fixture.has_type(events, "status_applied"))
end)

T.test(GROUP, "monster stun skips its attack", function()
	local engine = helpers.new_engine()
	local monster = fight(engine)
	fixed_attack(monster, 5)
	monster.statuses[#monster.statuses + 1] = ActiveStatus.new("stun", 1, 0)
	local events = engine:step(commands.Defend())
	T.ok(fixture.has_type(events, "monster_stunned"))
	T.no(fixture.has_type(events, "monster_attacked"))
	T.eq(monster.stun_cooldown, 1)
end)

T.test(GROUP, "monster dies from status tick", function()
	local engine = helpers.new_engine()
	local monster = fight(engine)
	monster.hp = 1
	monster.statuses[#monster.statuses + 1] = ActiveStatus.new("bleed", 3, 5)
	local events = engine:step(commands.Defend())
	T.ok(fixture.has_type(events, "monster_killed"))
	T.eq(engine.state.phase, "merchant")
end)

T.test(GROUP, "boss telegraphs then charges", function()
	local engine = helpers.new_engine()
	engine.state.round = 9
	local boss = fight(engine)
	T.ok(boss.is_boss)
	boss.hp = 1000000
	boss.max_hp = 1000000
	local attacks = {}
	for index, attack in ipairs(boss.attacks) do
		-- `replace` cannot clear a field: a nil override is no override.
		local copy = definitions.replace(attack, {})
		copy.status = nil
		attacks[index] = copy
	end
	boss.attacks = attacks
	engine.state.player.hp = 1000000
	local relevant = { boss_telegraph = true, monster_attacked = true, attack_dodged = true, attack_parried = true }
	local sequence = {}
	for _ = 1, 4 do
		local events = engine:step(commands.Defend())
		local outcome = "none"
		for _, event in ipairs(events) do
			if relevant[event.type] then
				if event.type == "boss_telegraph" then
					outcome = "telegraph"
				elseif event.type == "monster_attacked" and event.charged then
					outcome = "charged"
				else
					outcome = "normal"
				end
				break
			end
		end
		sequence[#sequence + 1] = outcome
	end
	T.eq(sequence, { "normal", "normal", "telegraph", "charged" })
end)

T.test(GROUP, "heal is capped and level three cleanses", function()
	local data = helpers.data()
	local engine = helpers.new_engine()
	fixed_attack(fight(engine), 1)
	local player = engine.state.player
	player.spell_uses.wound_cleansing = 50
	player.statuses[#player.statuses + 1] = ActiveStatus.new("poison", 5, 1)
	player.hp = character.build_sheet(player, data).max_hp - 3
	local events = engine:step(commands.Cast("wound_cleansing"))
	local healed = first(events, "spell_healed")
	T.eq(healed.amount, 3)
	T.eq(healed.mana, 32)
	T.ok(fixture.has(events, { type = "status_expired", target = "player", status = "poison" }))
	T.eq(player.spell_uses.wound_cleansing, 51)
end)

T.test(GROUP, "spell levels up after twenty uses", function()
	local engine = helpers.new_engine()
	fixed_attack(fight(engine), 1)
	local player = engine.state.player
	player.spell_uses.brutal_strike = 19
	player.mp = 1000
	local events = engine:step(commands.Cast("brutal_strike"))
	T.ok(fixture.has(events, { type = "spell_level_up", spellId = "brutal_strike", level = 2 }))
	T.eq(first(events, "spell_cast").mana, 20)
end)

T.test(GROUP, "magic level grows with mana spent", function()
	local data = helpers.data()
	local engine = helpers.new_engine()
	fixed_attack(fight(engine), 1)
	local player = engine.state.player
	player.mana_spent = data.balance.magic_level_base - 1
	player.mp = 1000
	local events = engine:step(commands.Cast("brutal_strike"))
	T.ok(fixture.has(events, { type = "magic_level_up", magicLevel = 2 }))
end)

T.test(GROUP, "immune monster takes no damage", function()
	local data = helpers.data()
	local engine = helpers.new_engine({ vocation = "mage" })
	local monster = fight(engine)
	fixed_attack(monster, 1)
	engine.state.monster = definitions.replace(monster, { creature_id = "fire_elemental" })
	T.eq(data:creature("fire_elemental"):resistance("fire"), 0)
	engine.state.player.mp = 1000
	local events = engine:step(commands.Cast("flame_strike"))
	T.eq(first(events, "spell_cast").damage, 0)
end)

T.test(GROUP, "items add crit dodge and leech", function()
	local game_data = helpers.with_test_items(helpers.data())
	local engine = helpers.new_engine({ data = game_data })
	local player = engine.state.player
	player.equipment.ring = fixture.item(99, "test_ring", "common", 0)
	local sheet = character.build_sheet(player, game_data)
	T.eq(sheet.crit_chance, game_data.balance.caps.crit_chance)
	T.eq(sheet.dodge, game_data.balance.caps.dodge)
	fixed_attack(fight(engine), 5)
	local crits, dodges = 0, 0
	for _ = 1, 60 do
		player.hp = 100
		local events = engine:step(commands.Attack())
		for _, event in ipairs(events) do
			if event.type == "player_attacked" and event.crit then
				crits = crits + 1
			end
		end
		dodges = dodges + fixture.count(events, "attack_dodged")
	end
	T.ok(crits > 0)
	T.ok(dodges > 0)
end)

T.test(GROUP, "potion restores and is consumed", function()
	local data = helpers.data()
	local engine = helpers.new_engine()
	fixed_attack(fight(engine), 1)
	local player = engine.state.player
	player.hp = 10
	local before = player:potion_count("mana_potion")
	player.mp = 0
	local events = engine:step(commands.UsePotion("mana_potion"))
	T.eq(first(events, "potion_used").resource, "mp")
	T.eq(player:potion_count("mana_potion"), before - 1)
	T.ok(0 < player.mp and player.mp <= character.build_sheet(player, data).max_mp)
end)

T.test(GROUP, "every vocation spell can be cast", function()
	local data = helpers.data()
	for _, vocation in ipairs({ "warrior", "archer", "mage" }) do
		local engine = helpers.new_engine({ vocation = vocation })
		fixed_attack(fight(engine), 1)
		for _, spell_id in ipairs(data:vocation(vocation).spells) do
			engine.state.player.mp = 10000
			engine.state.player.hp = 5
			local events = engine:step(commands.Cast(spell_id))
			local cast = false
			for _, event in ipairs(events) do
				if (event.type == "spell_cast" or event.type == "spell_healed") and event.spellId == spell_id then
					cast = true
				end
			end
			T.ok(cast, vocation .. " could not cast " .. spell_id)
		end
	end
end)
