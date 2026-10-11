local commands = require("rpg.application.commands")
local entities = require("rpg.domain.entities")
local i18n = require("rpg.infrastructure.i18n")
local EventFormatter = require("rpg.presentation.event_text")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "unit/event_text"

local function new_formatter()
	return EventFormatter.new(helpers.data(), i18n.Translator.new(helpers.shared_dir()))
end

local function rat()
	return entities.MonsterInstance.new({
		creature_id = "rat",
		is_boss = false,
		enemy_class = "normal",
		hp = 10,
		max_hp = 10,
		xp = 1,
		gold_min = 1,
		gold_max = 1,
		attacks = {},
	})
end

-- { event, expected }; without an expected text the sentence only has to name the unknown id.
local CASES = {
	{ { type = "player_attacked", damage = 12, crit = false, element = "fire" }, "You hit for 12 fire damage." },
	{
		{ type = "player_attacked", damage = 30, crit = true, element = "physical" },
		"CRITICAL! You hit for 30 physical damage.",
	},
	{
		{ type = "spell_cast", spellId = "flame_strike", damage = 9, crit = false, element = "fire", mana = 20 },
		"Flame Strike deals 9 fire damage.",
	},
	{
		{ type = "potion_used", potionId = "mana_potion", amount = 80, resource = "mp" },
		"Mana Potion restores 80 MP.",
	},
	{
		{ type = "status_applied", target = "player", status = "burn", turns = 3, perTurn = 2 },
		"You are burning (3 turns).",
	},
	{ { type = "monster_killed", monsterId = "dragon", isBoss = false }, "You defeated Dragon!" },
	{
		{ type = "round_started", round = 10, tier = 0, cycle = 0, monsterId = "munster", isBoss = true, hp = 5 },
		"Round 10: the boss Munster challenges you! (5 HP)",
	},
	{ { type = "item_sold", uid = 3, itemId = "sword", gold = 25 }, "You sold Sword for 25 gold." },
	{ { type = "error", code = "not_enough_mana" }, "Not enough mana." },
	{ { type = "error", code = "level_too_low" }, "Your level is too low for that item." },
	{
		{
			type = "round_started",
			round = 3,
			tier = 0,
			cycle = 0,
			monsterId = "rat",
			isBoss = false,
			enemyClass = "elite",
			hp = 90,
		},
		"Round 3: an ELITE Rat appears! (90 HP)",
	},
	{
		{
			type = "monster_attacked",
			attackId = "bite",
			damage = 9,
			element = "physical",
			charged = false,
			crit = true,
		},
		"CRITICAL! Rat hits you for 9 physical damage.",
	},
	{ { type = "monster_dodged" }, "Rat dodges your attack!" },
	{ { type = "monster_parried", reflected = 4 }, "Rat parries your attack: you take 4 damage!" },
	{ { type = "monster_healed", amount = 12 }, "Rat heals 12 HP." },
	{ { type = "attack_parried", attackId = "bite", reflected = 3 }, "You parry the attack and reflect 3 damage!" },
	{
		{ type = "item_auto_equipped", uid = 5, itemId = "sword", slot = "weapon", score = 60 },
		"Auto-equipped Sword (score 60).",
	},
	{ { type = "item_auto_sold", uid = 6, itemId = "bow", gold = 30 }, "Sold Bow for 30 gold (auto-sell)." },
	{ { type = "potion_dropped", potionId = "mana_potion" }, "Loot: Mana Potion!" },
	{ { type = "run_won", round = 100 }, "VICTORY! You defeated the final boss on round 100!" },
	{ { type = "run_ended", won = true }, "Your victory is recorded in the Hall of Fame." },
	{ { type = "spell_cast", spellId = "ghost_spell", damage = 1, crit = false, element = "fire", mana = 1 } },
}

T.test(GROUP, "format events", function()
	local formatter = new_formatter()
	for index, case in ipairs(CASES) do
		local event, expected = case[1], case[2]
		local state = helpers.new_engine().state
		state.monster = rat()
		local text = formatter:format(event, state)
		local label = string.format("case %d (%s)", index, event.type)
		if expected == nil then
			T.contains(text, "ghost_spell", label)
		else
			T.eq(text, expected, label)
		end
	end
end)

T.test(GROUP, "monster name comes from state", function()
	local formatter = new_formatter()
	local engine = helpers.new_engine()
	engine:step(commands.NextFight())
	T.ok(engine.state.monster ~= nil)
	local name = helpers.data():creature(engine.state.monster.creature_id).name
	local text = formatter:format({ type = "monster_stunned" }, engine.state)
	T.eq(text:sub(1, #name), name)
end)

T.test(GROUP, "item name by uid", function()
	local formatter = new_formatter()
	local state = helpers.new_engine().state
	local bag = state.player.bag
	bag[#bag + 1] = entities.ItemInstance.new({ uid = 77, item_id = "bow", rarity = "rare", tier = 0 })
	T.eq(
		formatter:format({ type = "item_dropped", uid = 77, itemId = "bow", rarity = "rare" }, state),
		"Loot: Bow [Rare]!"
	)
	T.contains(formatter:format({ type = "item_equipped", uid = 999, slot = "ring" }, state), "#999")
end)
