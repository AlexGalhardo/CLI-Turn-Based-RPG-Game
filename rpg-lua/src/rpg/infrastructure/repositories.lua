--- JSON file repositories under the player's data directory (docs/persistence.md).

local profile_module = require("rpg.application.profile")
local save_game = require("rpg.application.save_game")
local base = require("rpg.domain.base")
local json = require("rpg.domain.json_types")
local filesystem = require("rpg.infrastructure.filesystem")
local i18n = require("rpg.infrastructure.i18n")
local migrations = require("rpg.infrastructure.migrations")

local json_obj, json_int, json_str, json_bool = json.json_obj, json.json_int, json.json_str, json.json_bool
local check_schema = save_game.check_schema

local repositories = {}

repositories.BATTLE_SPEEDS = { 1, 2 }

--- Write to a temp file then rename, so a crash never leaves a half-written save.
function repositories.write_json_atomic(path, document)
	local parent = filesystem.parent(path)
	if parent ~= nil then
		filesystem.make_dirs(parent)
	end
	local temporary = path .. ".tmp"
	filesystem.write_file(temporary, json.encode(document, "\t") .. "\n")
	filesystem.replace(temporary, path)
end

function repositories.read_json(path)
	local text = filesystem.read_file(path)
	if text == nil then
		base.raise("OSError", "cannot read " .. path)
	end
	return json.decode(text)
end

-- ── clock ───────────────────────────────────────────────────────────────────

local SystemClock = base.class("SystemClock")
repositories.SystemClock = SystemClock

function SystemClock.new()
	return setmetatable({}, SystemClock)
end

function SystemClock:now()
	return os.time()
end

-- ── settings: locale (or nil), auto_equip, battle_speed ─────────────────────

function repositories.Settings(fields)
	fields = fields or {}
	return {
		locale = fields.locale,
		auto_equip = fields.auto_equip or false,
		battle_speed = fields.battle_speed or 1,
	}
end

local SettingsRepository = base.class("SettingsRepository")
repositories.SettingsRepository = SettingsRepository

function SettingsRepository.new(data_dir)
	return setmetatable({ _path = filesystem.join(data_dir, "settings.json") }, SettingsRepository)
end

function SettingsRepository:load()
	if not filesystem.exists(self._path) then
		return repositories.Settings()
	end
	local data = json_obj(repositories.read_json(self._path))
	check_schema(data, "settings.json")
	data = migrations.migrate_settings(data)
	local locale = data.locale ~= nil and json_str(data.locale) or nil
	local speed = json_int(data.battleSpeed)
	return repositories.Settings({
		locale = base.contains(i18n.SUPPORTED_LOCALES, locale) and locale or nil,
		auto_equip = json_bool(data.autoEquip),
		battle_speed = base.contains(repositories.BATTLE_SPEEDS, speed) and speed or repositories.BATTLE_SPEEDS[1],
	})
end

function SettingsRepository:save(settings)
	repositories.write_json_atomic(
		self._path,
		json.object({
			{ "schemaVersion", save_game.SCHEMA_VERSION },
			{ "locale", settings.locale },
			{ "autoEquip", settings.auto_equip },
			{ "battleSpeed", settings.battle_speed },
		})
	)
end

-- ── save ────────────────────────────────────────────────────────────────────

local FileSaveRepository = base.class("FileSaveRepository")
repositories.FileSaveRepository = FileSaveRepository

function FileSaveRepository.new(data_dir)
	return setmetatable({ _path = filesystem.join(data_dir, "save.json") }, FileSaveRepository)
end

function FileSaveRepository:load()
	if not filesystem.exists(self._path) then
		return nil
	end
	local document = json_obj(repositories.read_json(self._path))
	check_schema(document, "save.json")
	return save_game.SaveGame.from_dict(migrations.migrate_save(document))
end

function FileSaveRepository:save(save)
	repositories.write_json_atomic(self._path, save:to_dict())
end

function FileSaveRepository:delete()
	filesystem.remove(self._path)
end

-- ── history ─────────────────────────────────────────────────────────────────

local FileHistoryRepository = base.class("FileHistoryRepository")
repositories.FileHistoryRepository = FileHistoryRepository

function FileHistoryRepository.new(data_dir)
	return setmetatable({ _dir = filesystem.join(data_dir, "history") }, FileHistoryRepository)
end

function FileHistoryRepository:add(record)
	repositories.write_json_atomic(filesystem.join(self._dir, record.run_id .. ".json"), record:to_dict())
end

function FileHistoryRepository:list()
	local records = {}
	for _, name in ipairs(filesystem.list_dir(self._dir)) do
		if name:match("%.json$") then
			local document = json_obj(repositories.read_json(filesystem.join(self._dir, name)))
			check_schema(document, "history record")
			records[#records + 1] = save_game.RunRecord.from_dict(migrations.migrate_history(document))
		end
	end
	return records
end

-- ── profile ─────────────────────────────────────────────────────────────────

local FileProfileRepository = base.class("FileProfileRepository")
repositories.FileProfileRepository = FileProfileRepository

function FileProfileRepository.new(data_dir)
	return setmetatable({ _path = filesystem.join(data_dir, "profile.json") }, FileProfileRepository)
end

function FileProfileRepository:load()
	if not filesystem.exists(self._path) then
		return profile_module.Profile.new()
	end
	local data = json_obj(repositories.read_json(self._path))
	check_schema(data, "profile.json")
	return profile_module.Profile.from_dict(migrations.migrate_profile(data))
end

function FileProfileRepository:save(player_profile)
	repositories.write_json_atomic(self._path, player_profile:to_dict())
end

--- The three file repositories of a data directory, as `GameSession` expects them.
function repositories.file_repositories(data_dir)
	return {
		saves = FileSaveRepository.new(data_dir),
		history = FileHistoryRepository.new(data_dir),
		profile = FileProfileRepository.new(data_dir),
	}
end

return repositories
