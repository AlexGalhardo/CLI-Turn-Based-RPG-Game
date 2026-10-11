--- Use case that wraps the pure engine with time, persistence and the profile.
---
--- The engine stays deterministic; everything that depends on the clock or the filesystem happens here.

local GameEngine = require("rpg.application.engine")
local ports = require("rpg.application.ports")
local profile_module = require("rpg.application.profile")
local save_game = require("rpg.application.save_game")
local base = require("rpg.domain.base")
local enums = require("rpg.domain.enums")

local Phase = enums.Phase
local format_timestamp = save_game.format_timestamp

local GameSession = base.class("GameSession")

--- `options`: info (SessionInfo), repositories, clock, game_version.
function GameSession.new(data, engine, options)
	ports.check("clock", options.clock)
	ports.check("saves", options.repositories.saves)
	ports.check("history", options.repositories.history)
	ports.check("profile", options.repositories.profile)
	local self = setmetatable({
		data = data,
		engine = engine,
		info = options.info,
		_repositories = options.repositories,
		_clock = options.clock,
		_game_version = options.game_version,
	}, GameSession)
	self.profile = profile_module.ProfileService.new(data, options.repositories.profile:load())
	self._segment_started = options.clock:now()
	-- { run = RunState, rng_state = integer }: what a save written now would contain.
	self._merchant_snapshot = nil
	self.finished_record = nil
	return self
end

function GameSession:state()
	return self.engine.state
end

-- ── creation ────────────────────────────────────────────────────────────────

--- `options`: repositories, clock, game_version. Returns the session and the events of the run creation.
function GameSession.start(data, config, seed, options)
	local engine, events = GameEngine.new_run(data, config, seed)
	local now = options.clock:now()
	local info = save_game.SessionInfo.new({
		run_id = save_game.make_run_id(now, seed),
		started_at = format_timestamp(now),
	})
	local session = GameSession.new(data, engine, {
		info = info,
		repositories = options.repositories,
		clock = options.clock,
		game_version = options.game_version,
	})
	session:_after_step(events)
	return session, events
end

--- Returns nil when there is no save.
function GameSession.resume(data, repositories, clock, game_version)
	local save = repositories.saves:load()
	if save == nil then
		return nil
	end
	local engine = GameEngine.restore(data, save.run, save.rng_state)
	save.session.sessions = save.session.sessions + 1
	local session = GameSession.new(data, engine, {
		info = save.session,
		repositories = repositories,
		clock = clock,
		game_version = game_version,
	})
	session._merchant_snapshot = { run = save.run:clone(), rng_state = save.rng_state }
	return session
end

-- ── play ────────────────────────────────────────────────────────────────────

--- StepResult: `events` and the `achievements` unlocked by the step.
function GameSession:step(command)
	local events = self.engine:step(command)
	return { events = events, achievements = self:_after_step(events) }
end

function GameSession:_after_step(events)
	local state = self.engine.state
	local now = format_timestamp(self._clock:now())
	local unlocked = self.profile:observe(events, state, now, self.info.run_id)
	local profile_changed = #unlocked > 0
	for _, event in ipairs(events) do
		if event.type == "monster_killed" then
			profile_changed = true
		end
	end
	if state.phase == Phase.MERCHANT or state.phase == Phase.VICTORY then
		self._merchant_snapshot = { run = state:clone(), rng_state = self.engine:rng_state() }
		self:_write_save()
	elseif state.phase == Phase.GAME_OVER and self.finished_record == nil then
		self:_finish()
		profile_changed = true
	end
	if profile_changed then
		self._repositories.profile:save(self.profile.profile)
	end
	return unlocked
end

--- Persists play time. Mid-battle quits resume from the last merchant (or victory) snapshot.
function GameSession:save_and_quit()
	if self.engine.state.phase ~= Phase.GAME_OVER then
		self:_write_save()
	end
end

-- ── persistence ─────────────────────────────────────────────────────────────

function GameSession:_accumulate_play_time()
	local now = self._clock:now()
	self.info.play_time_seconds = self.info.play_time_seconds + math.max(0, now - self._segment_started)
	self._segment_started = now
	return now
end

function GameSession:_write_save()
	if self._merchant_snapshot == nil then
		return
	end
	local now = self:_accumulate_play_time()
	self._repositories.saves:save(save_game.SaveGame.new({
		game_version = self._game_version,
		implementation = save_game.IMPLEMENTATION,
		saved_at = format_timestamp(now),
		rng_state = self._merchant_snapshot.rng_state,
		session = self.info,
		run = self._merchant_snapshot.run,
	}))
end

function GameSession:_finish()
	local now = format_timestamp(self:_accumulate_play_time())
	local state = self.engine.state
	local record = save_game.RunRecord.new({
		run_id = self.info.run_id,
		name = state.config.name,
		vocation = state.config.vocation_id,
		difficulty = state.config.difficulty_id,
		seed = state.seed,
		implementation = save_game.IMPLEMENTATION,
		game_version = self._game_version,
		started_at = self.info.started_at,
		ended_at = now,
		play_time_seconds = self.info.play_time_seconds,
		sessions = self.info.sessions,
		round = state.round,
		level = state.player.level,
		magic_level = state.player.magic_level,
		death_cause = state.death_cause or "",
		won = state.won,
		stats = state.stats,
	})
	self._repositories.history:add(record)
	self._repositories.saves:delete()
	self.profile:record_finished_run(profile_module.HallOfFameEntry.new({
		run_id = record.run_id,
		name = record.name,
		vocation = record.vocation,
		difficulty = record.difficulty,
		round = record.round,
		level = record.level,
		ended_at = record.ended_at,
		won = record.won,
	}))
	self.finished_record = record
end

return GameSession
