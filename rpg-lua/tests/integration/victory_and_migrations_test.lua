--- Victory phase through the session (save, resume, history, Hall of Fame) and schema 1 → 2 migrations.

local commands = require("rpg.application.commands")
local GameSession = require("rpg.application.game_session")
local json = require("rpg.domain.json_types")
local filesystem = require("rpg.infrastructure.filesystem")
local repositories = require("rpg.infrastructure.repositories")
local helpers = require("support.helpers")
local fixture = require("support.persistence_fixture")
local T = require("support.test")

local GROUP = "integration/victory_and_migrations"

local M8_STATS = { "itemsAutoEquipped", "elitesKilled", "potionsDropped" }

T.test(GROUP, "victory is saved resumed and ended as won", function()
	local directory = helpers.temp_dir()
	local _, repos, clock = fixture.win_final_fight(directory)
	local save = fixture.read_json(directory, "save.json")
	T.eq(save.run.phase, "victory")
	T.eq(save.run.won, true)

	local resumed = GameSession.resume(helpers.data(), repos, clock, "1")
	T.ok(resumed ~= nil)
	T.eq(resumed:state().phase, "victory")
	local result = resumed:step(commands.EndRun())
	T.eq(result.events, { { type = "run_ended", won = true } })
	T.ok(repos.profile:load().achievements.conqueror ~= nil)
	T.no(fixture.exists(directory, "save.json"))
	local record = repos.history:list()[1]
	T.eq(record.won, true)
	T.eq(record.death_cause, "")
	T.eq(repos.profile:load().hall_of_fame[1].won, true)
end)

T.test(GROUP, "continue after victory keeps the run won", function()
	local final_round = helpers.data().balance.final_round
	local session = fixture.win_final_fight(helpers.temp_dir())
	local events = session:step(commands.ContinueRun()).events
	T.eq(events, { { type = "merchant_entered", round = final_round } })
	T.eq(session:state().phase, "merchant")
	T.eq(session:state().won, true)
	session:step(commands.NextFight())
	T.eq(session:state().round, final_round + 1)
end)

--- Turns a current save into what version 1 wrote: no M8 fields and the old `epic` rarity.
local function v1(document)
	local old = fixture.deep_copy(document)
	old.schemaVersion = 1
	local run = old.run
	-- The current format has the fields the migration must restore.
	T.ok(json.remove(run.config, "autoEquip") ~= nil)
	T.ok(json.remove(run, "won") ~= nil)
	run.player.equipment.weapon.rarity = "epic"
	for _, key in ipairs(M8_STATS) do
		T.ok(json.remove(run.stats, key) ~= nil, key)
	end
	json.set(run.stats, "itemsDropped", json.object({ { "epic", 2 }, { "legendary", 1 } }))
	json.set(
		run.stats,
		"droppedItems",
		json.array({ json.object({ { "itemId", "sword" }, { "rarity", "epic" }, { "round", 3 } }) })
	)
	return old
end

T.test(GROUP, "v1 save is migrated", function()
	local directory = helpers.temp_dir()
	fixture.start(directory, "Old", "warrior", "normal", 4)
	local current = fixture.read_json(directory, "save.json")
	fixture.write_json(directory, "save.json", v1(current))
	T.eq(fixture.read_json(directory, "save.json").schemaVersion, 1)

	local loaded = repositories.FileSaveRepository.new(directory):load()
	T.ok(loaded ~= nil)
	local run = loaded.run
	T.eq(run.config.auto_equip, false)
	T.eq(run.won, false)
	local slot, item = next(run.player.equipment)
	T.eq(slot, "weapon")
	T.eq(item.rarity, "legendary")
	T.eq(run.stats.items_dropped, { legendary = 3 })
	T.eq(#run.stats.dropped_items, 1)
	T.eq(run.stats.dropped_items[1].rarity, "legendary")
	T.eq(run.stats.elites_killed, 0)
end)

T.test(GROUP, "v1 monster gets its class from is boss", function()
	local directory = helpers.temp_dir()
	local session = fixture.start(directory, "Old", "mage", "normal", 4)
	session:state().round = 9
	session:step(commands.NextFight())
	local document = fixture.read_json(directory, "save.json")
	json.set(document.run, "monster", session:state():to_dict().monster)
	local old = v1(document)
	T.eq(old.run.monster.isBoss, true)
	T.ok(json.remove(old.run.monster, "enemyClass") ~= nil)
	fixture.write_json(directory, "save.json", old)
	local loaded = repositories.FileSaveRepository.new(directory):load()
	T.ok(loaded ~= nil)
	T.ok(loaded.run.monster ~= nil)
	T.eq(loaded.run.monster.is_boss, true)
	T.eq(loaded.run.monster.enemy_class, "boss")
end)

T.test(GROUP, "v1 history and profile are migrated", function()
	local directory = helpers.temp_dir()
	local won, old = filesystem.join(directory, "won"), filesystem.join(directory, "old")
	local session = fixture.win_final_fight(won)
	session:step(commands.EndRun())
	local names = fixture.history_files(won)
	T.eq(#names, 1)
	local record = fixture.read_json(won, "history/" .. names[1])
	record.schemaVersion = 1
	T.eq(json.remove(record, "won"), true)
	for _, key in ipairs(M8_STATS) do
		T.ok(json.remove(record.stats, key) ~= nil, key)
	end
	filesystem.make_dirs(filesystem.join(old, "history"))
	fixture.write_json(old, "history/" .. names[1], record)
	local records = repositories.FileHistoryRepository.new(old):list()
	T.eq(#records, 1)
	T.eq(records[1].won, false)
	T.eq(records[1].stats.elites_killed, 0)
	T.eq(records[1].round, helpers.data().balance.final_round)

	local profile = fixture.read_json(won, "profile.json")
	profile.schemaVersion = 1
	T.ok(#profile.hallOfFame > 0)
	for _, entry in ipairs(profile.hallOfFame) do
		T.eq(json.remove(entry, "won"), true)
	end
	fixture.write_json(old, "profile.json", profile)
	T.eq(repositories.FileProfileRepository.new(old):load().hall_of_fame[1].won, false)
end)

-- ── version 1 documents written by the Python reference ─────────────────────

T.test(GROUP, "python v1 documents migrate to the expected v2 documents", function()
	local migrated = helpers.read_json(helpers.fixture("python_save_v1_migrated.json"))
	T.eq(helpers.read_json(helpers.fixture("python_save_v1.json")).schemaVersion, 1)
	T.eq(helpers.read_json(helpers.fixture("python_profile_v1.json")).schemaVersion, 1)
	local directory = helpers.temp_dir()
	fixture.install(directory, "python_save_v1.json", "python_profile_v1.json")
	local repos = repositories.file_repositories(directory)
	local save, profile = repos.saves:load(), repos.profile:load()
	T.ok(save ~= nil)
	-- Same text as the reference migration, key order included.
	T.eq(json.encode(save.run:to_dict(), "\t"), json.encode(migrated.run, "\t"))
	T.eq(json.encode(profile:to_dict(), "\t"), json.encode(migrated.profile, "\t"))
	T.eq(save:to_dict().schemaVersion, 2)
	-- Loading migrates in memory only: the version 1 files stay as they were.
	T.eq(
		fixture.read_text(filesystem.join(directory, "save.json")),
		fixture.read_text(helpers.fixture("python_save_v1.json"))
	)
end)

T.test(GROUP, "python v1 save is migrated and continues identically", function()
	local directory = helpers.temp_dir()
	fixture.install(directory, "python_save_v1.json", "python_profile_v1.json")
	local migrated = helpers.read_json(helpers.fixture("python_save_v1_migrated.json"))
	fixture.check_loaded_documents(directory, migrated.run, migrated.profile)
	fixture.check_python_continuation(directory, "python_save_v1_continued.json")
end)
