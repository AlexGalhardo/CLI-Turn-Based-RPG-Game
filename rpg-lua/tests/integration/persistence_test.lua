local commands = require("rpg.application.commands")
local GameSession = require("rpg.application.game_session")
local profile_module = require("rpg.application.profile")
local run_state = require("rpg.application.run_state")
local save_game = require("rpg.application.save_game")
local json = require("rpg.domain.json_types")
local filesystem = require("rpg.infrastructure.filesystem")
local repositories = require("rpg.infrastructure.repositories")
local helpers = require("support.helpers")
local fixture = require("support.persistence_fixture")
local T = require("support.test")

local GROUP = "integration/persistence"

local Settings = repositories.Settings
local NEWER = '{"schemaVersion": 99}'

T.test(GROUP, "new session autosaves at merchant", function()
	local directory = helpers.temp_dir()
	local session, events = fixture.start(directory, "Alex", "archer", "normal", 5, { game_version = "9.9.9" })
	T.eq(events[1].type, "run_started")
	local save = fixture.read_json(directory, "save.json")
	T.eq(save.schemaVersion, 2)
	T.eq(save.run.config.autoEquip, false)
	T.eq(save.run.won, false)
	T.eq(save.implementation, "lua")
	T.eq(save.gameVersion, "9.9.9")
	T.eq(save.session.runId, session.info.run_id)
	T.eq(save.run.phase, "merchant")
	-- The first clock reading names the run.
	T.eq(session.info.run_id, "20260927T120010Z-5")
	T.eq(session.info.started_at, "2026-09-27T12:00:10Z")

	session:step(commands.BuyPotion("health_potion", 1))
	save = fixture.read_json(directory, "save.json")
	T.eq(save.run.player.potions.health_potion, 6)

	-- Written like the reference: tab indentation and a final newline.
	local text = fixture.read_text(filesystem.join(directory, "save.json"))
	T.eq(text:sub(1, 22), '{\n\t"schemaVersion": 2,')
	T.eq(text:sub(-2), "}\n")
	T.no(fixture.exists(directory, "save.json.tmp"))
end)

T.test(GROUP, "invalid config does not start a session", function()
	local directory = helpers.temp_dir()
	T.raises("ValueError", function()
		fixture.start(directory, "Alex", "knight", "normal", 5)
	end, "invalid run config")
	T.no(fixture.exists(directory, "save.json"))
end)

T.test(GROUP, "quit mid battle resumes from last merchant", function()
	local directory = helpers.temp_dir()
	local clock = fixture.clock()
	local session, _, repos = fixture.start(directory, "Alex", "warrior", "normal", 5, { clock = clock })
	session:step(commands.NextFight())
	local monster = session:state().monster
	T.ok(monster ~= nil)
	monster.hp, monster.max_hp = 1000000, 1000000
	session:step(commands.Attack())
	T.eq(session:state().phase, "battle")
	session:save_and_quit()

	local resumed = GameSession.resume(helpers.data(), repos, clock, "1")
	T.ok(resumed ~= nil)
	T.eq(resumed:state().phase, "merchant")
	T.eq(resumed:state().round, 0)
	T.eq(resumed:state().monster, nil)
	T.eq(resumed.info.sessions, 2)
	T.ok(resumed.info.play_time_seconds > 0)
	T.eq(resumed.info.run_id, session.info.run_id)
end)

T.test(GROUP, "resume without save", function()
	local repos = repositories.file_repositories(helpers.temp_dir())
	T.eq(GameSession.resume(helpers.data(), repos, fixture.clock(), "1"), nil)
end)

T.test(GROUP, "death writes history profile and deletes save", function()
	local directory = helpers.temp_dir()
	local session, _, repos = fixture.start(directory, "Bot", "mage", "hard", 3)
	local _, unlocked = fixture.play_to_end(session)
	local state = session:state()
	T.no(fixture.exists(directory, "save.json"))
	local records = repos.history:list()
	T.eq(#records, 1)
	local record = records[1]
	T.eq(record.round, state.round)
	T.eq(record.death_cause, state.death_cause)
	T.eq(record.implementation, "lua")
	T.ok(save_game.parse_timestamp(record.ended_at) > save_game.parse_timestamp(record.started_at))
	T.ok(session.finished_record ~= nil)
	fixture.check_json(record:to_dict(), session.finished_record:to_dict(), "history record")
	local profile = repos.profile:load()
	T.eq(profile.hall_of_fame[1].run_id, record.run_id)
	T.ok(#unlocked > 0)
	local first_blood = false
	for _, achievement_id in ipairs(unlocked) do
		first_blood = first_blood or achievement_id == "first_blood"
	end
	T.ok(first_blood, "first_blood was not unlocked")
	T.ok(profile.achievements.first_blood ~= nil)
	local kills = 0
	for _, entry in pairs(profile.bestiary) do
		kills = kills + entry.kills
	end
	T.eq(kills, state.round - 1)
	session:save_and_quit()
	T.no(fixture.exists(directory, "save.json"))
end)

T.test(GROUP, "newer schema is refused", function()
	local directory = helpers.temp_dir()
	local repos = repositories.file_repositories(directory)

	fixture.write_text(directory, "save.json", NEWER)
	T.raises("NewerSchemaError", function()
		repos.saves:load()
	end, "schema 99; update the game")
	-- A session cannot resume from it either, and the refused file is left alone.
	T.raises("NewerSchemaError", function()
		GameSession.resume(helpers.data(), repos, fixture.clock(), "1")
	end)
	T.eq(fixture.read_text(filesystem.join(directory, "save.json")), NEWER)

	fixture.write_text(directory, "profile.json", NEWER)
	T.raises("NewerSchemaError", function()
		repos.profile:load()
	end, "schema 99")
	T.eq(fixture.read_text(filesystem.join(directory, "profile.json")), NEWER)

	fixture.write_text(directory, "settings.json", NEWER)
	T.raises("NewerSchemaError", function()
		repositories.SettingsRepository.new(directory):load()
	end, "schema 99")
	T.eq(fixture.read_text(filesystem.join(directory, "settings.json")), NEWER)

	filesystem.make_dirs(filesystem.join(directory, "history"))
	fixture.write_text(directory, "history/x.json", NEWER)
	T.raises("NewerSchemaError", function()
		repos.history:list()
	end, "schema 99")
	T.eq(fixture.read_text(filesystem.join(directory, "history/x.json")), NEWER)
end)

T.test(GROUP, "newer python save is refused and left untouched", function()
	local directory = helpers.temp_dir()
	local repos = repositories.file_repositories(directory)
	for _, name in ipairs({ "save", "profile" }) do
		local document = helpers.read_json(helpers.fixture("python_" .. name .. ".json"))
		document.schemaVersion = 99
		filesystem.write_file(filesystem.join(directory, name .. ".json"), json.encode(document, "\t") .. "\n")
	end
	local save_text = fixture.read_text(filesystem.join(directory, "save.json"))
	local profile_text = fixture.read_text(filesystem.join(directory, "profile.json"))
	T.contains(save_text, '"schemaVersion": 99')

	T.raises("NewerSchemaError", function()
		repos.saves:load()
	end, "update the game")
	T.raises("NewerSchemaError", function()
		repos.profile:load()
	end, "update the game")
	T.raises("NewerSchemaError", function()
		GameSession.resume(helpers.data(), repos, fixture.clock(), "1")
	end, "update the game")
	T.eq(fixture.read_text(filesystem.join(directory, "save.json")), save_text)
	T.eq(fixture.read_text(filesystem.join(directory, "profile.json")), profile_text)
	T.eq(#fixture.history_files(directory), 0)
end)

T.test(GROUP, "broken save is not resumed", function()
	local directory = helpers.temp_dir()
	local repos = repositories.file_repositories(directory)
	fixture.write_text(directory, "save.json", "{ broken")
	T.raises("JsonError", function()
		repos.saves:load()
	end)
	T.raises("JsonError", function()
		GameSession.resume(helpers.data(), repos, fixture.clock(), "1")
	end)
	T.eq(fixture.read_text(filesystem.join(directory, "save.json")), "{ broken")
end)

T.test(GROUP, "settings round trip", function()
	local directory = helpers.temp_dir()
	local repo = repositories.SettingsRepository.new(directory)
	T.eq(repo:load(), Settings())
	T.eq(repo:load(), { auto_equip = false, battle_speed = 1 })
	repo:save(Settings({ locale = "pt-BR", auto_equip = true, battle_speed = 2 }))
	T.eq(repo:load(), Settings({ locale = "pt-BR", auto_equip = true, battle_speed = 2 }))
	local saved = fixture.read_json(directory, "settings.json")
	T.eq(saved, { schemaVersion = 2, locale = "pt-BR", autoEquip = true, battleSpeed = 2 })
	T.eq(json.keys(saved), { "schemaVersion", "locale", "autoEquip", "battleSpeed" })
	repo:save(Settings())
	T.eq(repo:load(), Settings())
	-- No locale chosen yet: the key is left out, not written as null.
	T.eq(fixture.read_json(directory, "settings.json"), { schemaVersion = 2, autoEquip = false, battleSpeed = 1 })
	fixture.write_text(directory, "settings.json", '{"schemaVersion": 1, "locale": "fr"}')
	T.eq(repo:load(), Settings())
	fixture.write_text(directory, "settings.json", '{"schemaVersion": 1, "locale": "en"}')
	T.eq(repo:load(), Settings({ locale = "en", auto_equip = false, battle_speed = 1 }))
	fixture.write_text(directory, "settings.json", '{"schemaVersion": 2, "autoEquip": true, "battleSpeed": 7}')
	T.eq(repo:load(), Settings({ auto_equip = true, battle_speed = 1 }))
end)

T.test(GROUP, "profile round trip and hall of fame order", function()
	local directory = helpers.temp_dir()
	local service = profile_module.ProfileService.new(helpers.data(), profile_module.Profile.new())
	for index = 0, 11 do
		service:record_finished_run(profile_module.HallOfFameEntry.new({
			run_id = "run" .. index,
			name = "A",
			vocation = "mage",
			difficulty = "normal",
			round = index % 5,
			level = index,
			ended_at = string.format("2026-01-%02dT00:00:00Z", index + 1),
		}))
	end
	local hall = service.profile.hall_of_fame
	T.eq(#hall, 10)
	T.eq({ hall[1].round, hall[2].round, hall[3].round }, { 4, 4, 3 })
	T.ok(hall[1].level > hall[2].level)
	service:record_finished_run(profile_module.HallOfFameEntry.new({
		run_id = "winner",
		name = "W",
		vocation = "mage",
		difficulty = "easy",
		round = 1,
		level = 1,
		ended_at = "2026-02-01T00:00:00Z",
		won = true,
	}))
	T.eq(#service.profile.hall_of_fame, 10)
	T.eq(service.profile.hall_of_fame[1].run_id, "winner")
	local repo = repositories.FileProfileRepository.new(directory)
	repo:save(service.profile)
	local loaded = repo:load()
	T.eq(loaded, service.profile)
	T.eq(json.encode(loaded:to_dict(), "\t"), json.encode(service.profile:to_dict(), "\t"))
	T.eq(loaded.hall_of_fame[1].won, true)
	T.eq(service:revealed("rat"), false)
end)

T.test(GROUP, "run id and clock", function()
	local moment = save_game.parse_timestamp("2026-01-02T03:04:05Z")
	T.eq(moment, 1767323045)
	T.eq(save_game.make_run_id(moment, 42), "20260102T030405Z-42")
	T.eq(save_game.format_timestamp(moment), "2026-01-02T03:04:05Z")
	T.eq(save_game.format_timestamp(0), "1970-01-01T00:00:00Z")
	-- The last second of a leap day in a century year.
	T.eq(save_game.format_timestamp(951868799), "2000-02-29T23:59:59Z")
	T.eq(save_game.parse_timestamp("2000-02-29T23:59:59Z"), 951868799)
	T.raises("ValueError", function()
		save_game.parse_timestamp("2026-01-02 03:04:05")
	end, "invalid timestamp")
	-- A moment is an integer number of seconds since the epoch, in UTC (the reference checks `tzinfo is UTC`).
	local now = repositories.SystemClock.new():now()
	T.eq(math.type(now), "integer")
	T.ok(now > moment)

	local clock = fixture.clock()
	T.eq(save_game.format_timestamp(clock:now()), "2026-09-27T12:00:10Z")
	T.eq(save_game.format_timestamp(clock:now()), "2026-09-27T12:00:20Z")
end)

-- ── saves written by the Python reference ───────────────────────────────────

T.test(GROUP, "python save round trips to the same text", function()
	local directory = helpers.temp_dir()
	fixture.install(directory, "python_save.json", "python_profile.json")
	local repos = repositories.file_repositories(directory)
	local save = repos.saves:load()
	T.ok(save ~= nil)
	T.eq(save.implementation, "python")
	T.eq(save.run.config, run_state.RunConfig.new(save.run.config.name, "archer", "hard", true))
	T.eq(save.run.seed, 2024)
	T.eq(save.run.round, 12)
	T.eq(save.run.phase, "merchant")
	T.eq(json.encode(save:to_dict(), "\t") .. "\n", fixture.read_lf(helpers.fixture("python_save.json")))
	local profile = repos.profile:load()
	T.eq(json.encode(profile:to_dict(), "\t") .. "\n", fixture.read_lf(helpers.fixture("python_profile.json")))

	-- Writing it back through the repositories gives the same files, byte for byte.
	repos.saves:save(save)
	repos.profile:save(profile)
	T.eq(
		fixture.read_text(filesystem.join(directory, "save.json")),
		fixture.read_lf(helpers.fixture("python_save.json"))
	)
	T.eq(
		fixture.read_text(filesystem.join(directory, "profile.json")),
		fixture.read_lf(helpers.fixture("python_profile.json"))
	)
end)

T.test(GROUP, "python save continues identically", function()
	local directory = helpers.temp_dir()
	fixture.install(directory, "python_save.json", "python_profile.json")
	local save = helpers.read_json(helpers.fixture("python_save.json"))
	local profile = helpers.read_json(helpers.fixture("python_profile.json"))
	T.eq(save.run.config.autoEquip, true)
	-- The parsed run serialises back to exactly the Python document.
	fixture.check_loaded_documents(directory, save.run, profile)
	fixture.check_python_continuation(directory, "python_save_continued.json")
end)
