--- Cross-run profile: bestiary, achievements and Hall of Fame (docs/game-design.md §11).

local base = require("rpg.domain.base")
local json = require("rpg.domain.json_types")

local json_obj, json_list, json_int = json.json_obj, json.json_list, json.json_int
local json_str, json_bool = json.json_str, json.json_bool

local profile = {}

profile.PROFILE_SCHEMA_VERSION = 2
profile.HALL_OF_FAME_SIZE = 10
profile.BESTIARY_REVEAL_KILLS = 5

-- ── HallOfFameEntry: run_id, name, vocation, difficulty, round, level, ended_at, won ──

local HallOfFameEntry = base.class("HallOfFameEntry")
profile.HallOfFameEntry = HallOfFameEntry

function HallOfFameEntry.new(fields)
	fields.won = fields.won or false
	return setmetatable(fields, HallOfFameEntry)
end

function HallOfFameEntry:to_dict()
	return json.object({
		{ "runId", self.run_id },
		{ "name", self.name },
		{ "vocation", self.vocation },
		{ "difficulty", self.difficulty },
		{ "round", self.round },
		{ "level", self.level },
		{ "endedAt", self.ended_at },
		{ "won", self.won },
	})
end

function HallOfFameEntry.from_dict(raw)
	local data = json_obj(raw)
	return HallOfFameEntry.new({
		run_id = json_str(data.runId),
		name = json_str(data.name),
		vocation = json_str(data.vocation),
		difficulty = json_str(data.difficulty),
		round = json_int(data.round),
		level = json_int(data.level),
		ended_at = json_str(data.endedAt),
		won = json_bool(data.won),
	})
end

-- ── Profile ─────────────────────────────────────────────────────────────────
-- bestiary: creature id → { kills, first_killed_at }; achievements: id → { unlocked_at, run_id };
-- hall_of_fame: list of HallOfFameEntry, best first.

local Profile = base.class("Profile")
profile.Profile = Profile

function Profile.new(fields)
	fields = fields or {}
	return setmetatable({
		bestiary = fields.bestiary or {},
		achievements = fields.achievements or {},
		hall_of_fame = fields.hall_of_fame or {},
	}, Profile)
end

function Profile:to_dict()
	local bestiary, achievements = {}, {}
	for index, key in ipairs(base.sorted_keys(self.bestiary)) do
		local entry = self.bestiary[key]
		bestiary[index] = { key, json.object({ { "kills", entry.kills }, { "firstKilledAt", entry.first_killed_at } }) }
	end
	for index, key in ipairs(base.sorted_keys(self.achievements)) do
		local unlock = self.achievements[key]
		achievements[index] = { key, json.object({ { "unlockedAt", unlock.unlocked_at }, { "runId", unlock.run_id } }) }
	end
	local hall = json.array({})
	for index, entry in ipairs(self.hall_of_fame) do
		hall[index] = entry:to_dict()
	end
	return json.object({
		{ "schemaVersion", profile.PROFILE_SCHEMA_VERSION },
		{ "bestiary", json.object(bestiary) },
		{ "achievements", json.object(achievements) },
		{ "hallOfFame", hall },
	})
end

function Profile.from_dict(raw)
	local data = json_obj(raw)
	local bestiary, achievements, hall = {}, {}, {}
	for key, value in pairs(json_obj(data.bestiary)) do
		local entry = json_obj(value)
		bestiary[key] = { kills = json_int(entry.kills), first_killed_at = json_str(entry.firstKilledAt) }
	end
	for key, value in pairs(json_obj(data.achievements)) do
		local unlock = json_obj(value)
		achievements[key] = { unlocked_at = json_str(unlock.unlockedAt), run_id = json_str(unlock.runId) }
	end
	for index, entry in ipairs(json_list(data.hallOfFame)) do
		hall[index] = HallOfFameEntry.from_dict(entry)
	end
	return Profile.new({ bestiary = bestiary, achievements = achievements, hall_of_fame = hall })
end

--- Won runs first, then the deepest round, the highest level and the earliest end.
local function ranks_before(a, b)
	if a.won ~= b.won then
		return a.won
	end
	if a.round ~= b.round then
		return a.round > b.round
	end
	if a.level ~= b.level then
		return a.level > b.level
	end
	return a.ended_at < b.ended_at
end

-- ── ProfileService ──────────────────────────────────────────────────────────

--- Feeds the profile from engine events. Returns the achievements unlocked by each step.
local ProfileService = base.class("ProfileService")
profile.ProfileService = ProfileService

function ProfileService.new(data, player_profile)
	return setmetatable({ _data = data, profile = player_profile }, ProfileService)
end

function ProfileService:observe(events, state, now, run_id)
	for _, event in ipairs(events) do
		if event.type == "monster_killed" then
			local monster_id = tostring(event.monsterId)
			local entry = self.profile.bestiary[monster_id]
			if entry == nil then
				self.profile.bestiary[monster_id] = { kills = 1, first_killed_at = now }
			else
				entry.kills = entry.kills + 1
			end
		end
	end
	local unlocked = {}
	for _, achievement in ipairs(self._data.achievements) do
		if
			self.profile.achievements[achievement.id] == nil
			and self:_progress(achievement, state) >= achievement.value
		then
			self.profile.achievements[achievement.id] = { unlocked_at = now, run_id = run_id }
			unlocked[#unlocked + 1] = achievement
		end
	end
	return unlocked
end

function ProfileService:_progress(achievement, state)
	local player = state.player
	local kind = achievement.type
	if kind == "kills_total" then
		local total = 0
		for _, entry in pairs(self.profile.bestiary) do
			total = total + entry.kills
		end
		return total
	elseif kind == "bosses_total" then
		local total = 0
		for _, boss in ipairs(self._data.bosses) do
			local entry = self.profile.bestiary[boss.id]
			total = total + (entry ~= nil and entry.kills or 0)
		end
		return total
	elseif kind == "round_reached" then
		return state.round
	elseif kind == "level_reached" then
		return player.level
	elseif kind == "legendary_found" then
		return state.stats.items_dropped.legendary or 0
	elseif kind == "spell_level_3" then
		local levels = self._data.balance.spell_levels
		local threshold = levels[#levels].uses
		local total = 0
		for _, uses in pairs(player.spell_uses) do
			if uses >= threshold then
				total = total + 1
			end
		end
		return total
	elseif kind == "gold_held" then
		return player.gold
	elseif kind == "distinct_monsters" then
		return base.count(self.profile.bestiary)
	elseif kind == "hard_round_reached" then
		return state.config.difficulty_id == "hard" and state.round or 0
	elseif kind == "run_won" then
		return state.won and 1 or 0
	end
	return 0
end

function ProfileService:record_finished_run(entry)
	local candidates = base.copy_list(self.profile.hall_of_fame)
	candidates[#candidates + 1] = entry
	local ranking = base.sorted(candidates, ranks_before)
	self.profile.hall_of_fame = table.move(ranking, 1, math.min(profile.HALL_OF_FAME_SIZE, #ranking), 1, {})
end

function ProfileService:revealed(monster_id)
	local entry = self.profile.bestiary[monster_id]
	return entry ~= nil and entry.kills >= profile.BESTIARY_REVEAL_KILLS
end

return profile
