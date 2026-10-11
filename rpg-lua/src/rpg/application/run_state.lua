local RunStatistics = require("rpg.application.statistics")
local base = require("rpg.domain.base")
local entities = require("rpg.domain.entities")
local enums = require("rpg.domain.enums")
local json = require("rpg.domain.json_types")

local json_obj, json_list, json_int = json.json_obj, json.json_list, json.json_int
local json_str, json_bool = json.json_str, json.json_bool

local run_state = {}

-- ── RunConfig: name, vocation_id, difficulty_id, auto_equip ─────────────────

local RunConfig = base.class("RunConfig")
run_state.RunConfig = RunConfig

function RunConfig.new(name, vocation_id, difficulty_id, auto_equip)
	return setmetatable({
		name = name,
		vocation_id = vocation_id,
		difficulty_id = difficulty_id,
		auto_equip = auto_equip or false,
	}, RunConfig)
end

function RunConfig:to_dict()
	return json.object({
		{ "name", self.name },
		{ "vocation", self.vocation_id },
		{ "difficulty", self.difficulty_id },
		{ "autoEquip", self.auto_equip },
	})
end

function RunConfig.from_dict(raw)
	local data = json_obj(raw)
	return RunConfig.new(
		json_str(data.name),
		json_str(data.vocation),
		json_str(data.difficulty),
		json_bool(data.autoEquip)
	)
end

-- ── RunState ────────────────────────────────────────────────────────────────

--- Everything needed to continue a run, except the PRNG state (kept by the engine).
local RunState = base.class("RunState")
run_state.RunState = RunState

--- `fields`: seed, config, player and optionally the rest (a new run starts at the merchant of round 0).
function RunState.new(fields)
	local self = setmetatable(fields, RunState)
	self.phase = self.phase or enums.Phase.MERCHANT
	self.round = self.round or 0
	self.turn = self.turn or 0
	self.merchant_stock = self.merchant_stock or {}
	self.next_item_uid = self.next_item_uid or 1
	self.won = self.won or false
	self.stats = self.stats or RunStatistics.new()
	return self
end

function RunState:take_item_uid()
	local uid = self.next_item_uid
	self.next_item_uid = self.next_item_uid + 1
	return uid
end

function RunState:to_dict()
	local stock = json.array({})
	for index, item in ipairs(self.merchant_stock) do
		stock[index] = item:to_dict()
	end
	return json.object({
		{ "seed", self.seed },
		{ "config", self.config:to_dict() },
		{ "player", self.player:to_dict() },
		{ "phase", self.phase },
		{ "round", self.round },
		{ "turn", self.turn },
		{ "monster", self.monster == nil and json.null or self.monster:to_dict() },
		{ "merchantStock", stock },
		{ "nextItemUid", self.next_item_uid },
		{ "deathCause", self.death_cause == nil and json.null or self.death_cause },
		{ "won", self.won },
		{ "stats", self.stats:to_dict() },
	})
end

function RunState.from_dict(raw)
	local data = json_obj(raw)
	local monster_raw = json.opt(data, "monster")
	local death_cause = json.opt(data, "deathCause")
	local phase = json_str(data.phase)
	if not enums.is_member(enums.Phase, phase) then
		base.raise("ValueError", string.format("'%s' is not a valid Phase", phase))
	end
	local stock = {}
	for index, item in ipairs(json_list(data.merchantStock)) do
		stock[index] = entities.ItemInstance.from_dict(item)
	end
	return RunState.new({
		seed = json_int(data.seed),
		config = RunConfig.from_dict(data.config),
		player = entities.Player.from_dict(data.player),
		phase = phase,
		round = json_int(data.round),
		turn = json_int(data.turn),
		monster = monster_raw ~= nil and entities.MonsterInstance.from_dict(monster_raw) or nil,
		merchant_stock = stock,
		next_item_uid = json_int(data.nextItemUid),
		death_cause = death_cause ~= nil and json_str(death_cause) or nil,
		won = json_bool(data.won),
		stats = RunStatistics.from_dict(data.stats),
	})
end

--- An independent copy (the reference uses `copy.deepcopy`).
function RunState:clone()
	return RunState.from_dict(self:to_dict())
end

return run_state
