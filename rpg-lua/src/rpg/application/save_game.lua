--- Save file and finished-run record formats, shared by every implementation (docs/persistence.md).
--- A moment in time is an integer: seconds since the Unix epoch, UTC (what `os.time()` returns).

local run_state = require("rpg.application.run_state")
local RunStatistics = require("rpg.application.statistics")
local base = require("rpg.domain.base")
local json = require("rpg.domain.json_types")

local json_obj, json_int, json_str, json_bool = json.json_obj, json.json_int, json.json_str, json.json_bool

local save_game = {}

save_game.SCHEMA_VERSION = 2
save_game.IMPLEMENTATION = "lua"

function save_game.format_timestamp(moment)
	return os.date("!%Y-%m-%dT%H:%M:%SZ", moment)
end

--- Days from 1970-01-01 to a civil date (Howard Hinnant's algorithm): Lua has no `timegm`.
local function days_from_civil(year, month, day)
	if month <= 2 then
		year = year - 1
	end
	local era = year // 400
	local year_of_era = year - era * 400
	local day_of_year = (153 * ((month + 9) % 12) + 2) // 5 + day - 1
	local day_of_era = year_of_era * 365 + year_of_era // 4 - year_of_era // 100 + day_of_year
	return era * 146097 + day_of_era - 719468
end

function save_game.parse_timestamp(text)
	local year, month, day, hour, minute, second = text:match("^(%d%d%d%d)-(%d%d)-(%d%d)T(%d%d):(%d%d):(%d%d)Z$")
	if year == nil then
		base.raise("ValueError", "invalid timestamp: " .. tostring(text))
	end
	local days = days_from_civil(tonumber(year), tonumber(month), tonumber(day))
	return days * 86400 + tonumber(hour) * 3600 + tonumber(minute) * 60 + tonumber(second)
end

function save_game.make_run_id(started_at, seed)
	return string.format("%s-%d", os.date("!%Y%m%dT%H%M%SZ", started_at), seed)
end

--- Raises `NewerSchemaError`: the file was written by a newer game version; it is never overwritten.
function save_game.check_schema(data, what)
	local version = json_int(data.schemaVersion)
	if version > save_game.SCHEMA_VERSION then
		base.raise(
			"NewerSchemaError",
			string.format("%s uses schema %d; update the game (supports %d)", what, version, save_game.SCHEMA_VERSION)
		)
	end
end

-- ── SessionInfo: run_id, started_at, play_time_seconds, sessions ────────────

local SessionInfo = base.class("SessionInfo")
save_game.SessionInfo = SessionInfo

function SessionInfo.new(fields)
	fields.play_time_seconds = fields.play_time_seconds or 0
	fields.sessions = fields.sessions or 1
	return setmetatable(fields, SessionInfo)
end

function SessionInfo:to_dict()
	return json.object({
		{ "runId", self.run_id },
		{ "startedAt", self.started_at },
		{ "playTimeSeconds", self.play_time_seconds },
		{ "sessions", self.sessions },
	})
end

function SessionInfo.from_dict(raw)
	local data = json_obj(raw)
	return SessionInfo.new({
		run_id = json_str(data.runId),
		started_at = json_str(data.startedAt),
		play_time_seconds = json_int(data.playTimeSeconds),
		sessions = json_int(data.sessions),
	})
end

-- ── SaveGame: game_version, implementation, saved_at, rng_state, session, run ──

local SaveGame = base.class("SaveGame")
save_game.SaveGame = SaveGame

function SaveGame.new(fields)
	return setmetatable(fields, SaveGame)
end

function SaveGame:to_dict()
	return json.object({
		{ "schemaVersion", save_game.SCHEMA_VERSION },
		{ "gameVersion", self.game_version },
		{ "implementation", self.implementation },
		{ "savedAt", self.saved_at },
		{ "rngState", self.rng_state },
		{ "session", self.session:to_dict() },
		{ "run", self.run:to_dict() },
	})
end

function SaveGame.from_dict(raw)
	local data = json_obj(raw)
	save_game.check_schema(data, "save.json")
	return SaveGame.new({
		game_version = json_str(data.gameVersion),
		implementation = json_str(data.implementation),
		saved_at = json_str(data.savedAt),
		rng_state = json_int(data.rngState),
		session = SessionInfo.from_dict(data.session),
		run = run_state.RunState.from_dict(data.run),
	})
end

-- ── RunRecord ───────────────────────────────────────────────────────────────

--- A finished run, written to history/<runId>.json.
local RunRecord = base.class("RunRecord")
save_game.RunRecord = RunRecord

-- Save key → field → reader, in the order the reference writes them (after `schemaVersion`).
local RECORD_FIELDS = {
	{ "runId", "run_id", json_str },
	{ "name", "name", json_str },
	{ "vocation", "vocation", json_str },
	{ "difficulty", "difficulty", json_str },
	{ "seed", "seed", json_int },
	{ "implementation", "implementation", json_str },
	{ "gameVersion", "game_version", json_str },
	{ "startedAt", "started_at", json_str },
	{ "endedAt", "ended_at", json_str },
	{ "playTimeSeconds", "play_time_seconds", json_int },
	{ "sessions", "sessions", json_int },
	{ "round", "round", json_int },
	{ "level", "level", json_int },
	{ "magicLevel", "magic_level", json_int },
	{ "deathCause", "death_cause", json_str },
	{ "won", "won", json_bool },
}

function RunRecord.new(fields)
	fields.won = fields.won or false
	fields.stats = fields.stats or RunStatistics.new()
	return setmetatable(fields, RunRecord)
end

function RunRecord:to_dict()
	local pairs_list = { { "schemaVersion", save_game.SCHEMA_VERSION } }
	for _, field in ipairs(RECORD_FIELDS) do
		pairs_list[#pairs_list + 1] = { field[1], self[field[2]] }
	end
	pairs_list[#pairs_list + 1] = { "stats", self.stats:to_dict() }
	return json.object(pairs_list)
end

function RunRecord.from_dict(raw)
	local data = json_obj(raw)
	save_game.check_schema(data, "history record")
	local fields = { stats = RunStatistics.from_dict(data.stats) }
	for _, field in ipairs(RECORD_FIELDS) do
		fields[field[2]] = field[3](data[field[1]])
	end
	return RunRecord.new(fields)
end

return save_game
