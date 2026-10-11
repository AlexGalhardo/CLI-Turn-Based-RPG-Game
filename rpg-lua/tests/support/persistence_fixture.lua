--- Shared by the persistence tests: the reference clock, sessions on a temp directory, raw JSON files and the
--- continuation of a save written by the Python reference.

local GreedyBot = require("rpg.application.bot")
local commands = require("rpg.application.commands")
local GameSession = require("rpg.application.game_session")
local run_state = require("rpg.application.run_state")
local save_game = require("rpg.application.save_game")
local json = require("rpg.domain.json_types")
local filesystem = require("rpg.infrastructure.filesystem")
local repositories = require("rpg.infrastructure.repositories")
local helpers = require("support.helpers")
local T = require("support.test")

local fixture = {}

fixture.MAX_STEPS = 200000
fixture.MAX_SWINGS = 200

--- The reference FakeClock: it starts at 2026-09-27T12:00:00Z and advances 10 s per reading.
function fixture.clock()
	return helpers.fake_clock(save_game.parse_timestamp("2026-09-27T12:00:00Z"))
end

--- `options`: clock, game_version ("1"), auto_equip. Returns the session, the creation events and the repositories.
function fixture.start(directory, name, vocation, difficulty, seed, options)
	options = options or {}
	local repos = repositories.file_repositories(directory)
	local session, events = GameSession.start(
		helpers.data(),
		run_state.RunConfig.new(name, vocation, difficulty, options.auto_equip),
		seed,
		{ repositories = repos, clock = options.clock or fixture.clock(), game_version = options.game_version or "1" }
	)
	return session, events, repos
end

function fixture.read_text(path)
	local text = filesystem.read_file(path)
	T.ok(text ~= nil, "cannot read " .. path)
	return text
end

--- File content with Windows line endings normalised (git may check the fixtures out with CRLF).
function fixture.read_lf(path)
	return (fixture.read_text(path):gsub("\r\n", "\n"))
end

function fixture.read_json(directory, name)
	return json.decode(fixture.read_text(filesystem.join(directory, name)))
end

--- Writes `document` on one line, like the `json.dumps` of the reference tests.
function fixture.write_json(directory, name, document)
	filesystem.write_file(filesystem.join(directory, name), json.encode(document))
end

function fixture.write_text(directory, name, text)
	filesystem.write_file(filesystem.join(directory, name), text)
end

function fixture.exists(directory, name)
	return filesystem.exists(filesystem.join(directory, name))
end

--- An independent copy of a JSON document (Python's `copy.deepcopy`).
function fixture.deep_copy(document)
	return json.decode(json.encode(document))
end

--- Names of the `.json` files of `directory/history`.
function fixture.history_files(directory)
	local names = {}
	for _, name in ipairs(filesystem.list_dir(filesystem.join(directory, "history"))) do
		if name:match("%.json$") then
			names[#names + 1] = name
		end
	end
	return names
end

--- Copies a save and a profile written by the Python reference into `directory`.
function fixture.install(directory, save_name, profile_name)
	fixture.write_text(directory, "save.json", fixture.read_text(helpers.fixture(save_name)))
	fixture.write_text(directory, "profile.json", fixture.read_text(helpers.fixture(profile_name)))
end

--- Plays with the bot until the run ends. Returns the number of steps and the ids of the unlocked achievements.
function fixture.play_to_end(session)
	local bot = GreedyBot.new(helpers.data())
	local steps, unlocked = 0, {}
	while session:state().phase ~= "game_over" do
		T.ok(steps < fixture.MAX_STEPS, "run did not finish")
		for _, achievement in ipairs(session:step(bot:choose(session:state())).achievements) do
			unlocked[#unlocked + 1] = achievement.id
		end
		steps = steps + 1
	end
	return steps, unlocked
end

--- Starts a run and beats the final boss; the session is left in the victory phase.
function fixture.win_final_fight(directory)
	local data = helpers.data()
	local clock = fixture.clock()
	local session, _, repos =
		fixture.start(directory, "Vic", "warrior", "easy", 8, { clock = clock, auto_equip = true })
	session:state().round = data.balance.final_round - 1
	session:step(commands.NextFight())
	local monster = session:state().monster
	T.ok(monster ~= nil)
	T.eq(monster.creature_id, "ferumbras")
	T.eq(monster.enemy_class, "boss")
	for _ = 1, fixture.MAX_SWINGS do
		if session:state().phase ~= "battle" then
			break
		end
		session:state().player.hp = 1000000
		monster.hp = 1
		session:step(commands.Attack())
	end
	T.eq(session:state().phase, "victory")
	return session, repos, clock
end

--- Asserts that two JSON documents are structurally equal (key order ignored), showing both when they are not.
function fixture.check_json(actual, expected, what)
	T.ok(
		json.equal(actual, expected),
		string.format("%s: expected %s\n       got %s", what, json.encode(expected), json.encode(actual))
	)
end

--- Loads the installed save and profile through the repositories and compares what they serialise back to.
function fixture.check_loaded_documents(directory, expected_run, expected_profile)
	local repos = repositories.file_repositories(directory)
	local save = repos.saves:load()
	T.ok(save ~= nil, "the Python save did not load")
	T.eq(save.implementation, "python")
	T.eq(save.run.config.auto_equip, expected_run.config.autoEquip)
	fixture.check_json(save.run:to_dict(), expected_run, "run")
	local profile = repos.profile:load()
	T.ok(next(profile.bestiary) ~= nil)
	fixture.check_json(profile:to_dict(), expected_profile, "profile")
end

--- Resumes the installed save, plays it with the bot until the run ends and compares it with the Python
--- continuation recorded in `expected_name`.
function fixture.check_python_continuation(directory, expected_name)
	local expected = helpers.read_json(helpers.fixture(expected_name))
	local repos = repositories.file_repositories(directory)
	local session = GameSession.resume(helpers.data(), repos, fixture.clock(), "1")
	T.ok(session ~= nil, "the save did not resume")
	T.eq(session.info.sessions, 2)
	local steps = fixture.play_to_end(session)
	local state = session:state()
	T.eq(steps, expected.steps, "steps")
	T.eq(state.round, expected.round, "round")
	T.eq(state.player.level, expected.level, "level")
	T.eq(state.player.gold, expected.gold, "gold")
	T.eq(session.engine:rng_state(), expected.rngState, "rngState")
	T.eq(state.death_cause or "", expected.deathCause, "deathCause")
	T.eq(state.won, expected.won, "won")
	fixture.check_json(state.stats:to_dict(), expected.stats, "stats")
	-- The run ended: the save is gone and the run is in the history.
	T.no(fixture.exists(directory, "save.json"))
	T.eq(#fixture.history_files(directory), 1)
	T.eq(#repos.history:list(), 1)
end

return fixture
