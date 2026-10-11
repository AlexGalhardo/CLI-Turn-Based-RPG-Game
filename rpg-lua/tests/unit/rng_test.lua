local json = require("rpg.domain.json_types")
local Rng = require("rpg.domain.rng")
local filesystem = require("rpg.infrastructure.filesystem")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "unit/rng"

local function outputs(rng, count)
	local result = {}
	for index = 1, count do
		result[index] = rng:next_u32()
	end
	return result
end

T.test(GROUP, "matches reference vectors", function()
	local document = helpers.read_json(filesystem.join(helpers.shared_dir(), "golden", "prng.json"))
	local vectors = json.json_list(document.vectors)
	T.ok(#vectors >= 5)
	for _, vector in ipairs(vectors) do
		local expected = json.json_list(vector.outputs)
		T.ok(json.equal(outputs(Rng.new(vector.seed), #expected), expected), "seed " .. vector.seed)
	end
end)

T.test(GROUP, "seed is reduced modulo 2^32", function()
	T.eq(Rng.new((1 << 32) + 42):next_u32(), Rng.new(42):next_u32())
end)

T.test(GROUP, "state round trip continues sequence", function()
	local rng = Rng.new(7)
	rng:next_u32()
	local clone = Rng.new(rng.state)
	T.eq(outputs(clone, 5), outputs(rng, 5))
end)

T.test(GROUP, "roll is inclusive and bounded", function()
	local rng = Rng.new(1)
	local seen = {}
	for _ = 1, 500 do
		seen[rng:roll(3, 5)] = true
	end
	T.eq(seen, { [3] = true, [4] = true, [5] = true })
end)

T.test(GROUP, "roll rejects inverted range", function()
	T.raises("ValueError", function()
		Rng.new(1):roll(5, 4)
	end, "invalid range")
end)

T.test(GROUP, "certain chances do not consume", function()
	for _, case in ipairs({ { 0, false }, { -5, false }, { 100, true }, { 150, true } }) do
		local rng = Rng.new(9)
		local state = rng.state
		T.eq(rng:chance(case[1]), case[2])
		T.eq(rng.state, state)
	end
end)

T.test(GROUP, "chance consumes one number", function()
	local rng = Rng.new(9)
	local reference = Rng.new(9)
	local result = rng:chance(50)
	T.eq(result, reference:roll(1, 100) <= 50)
	T.eq(rng.state, reference.state)
end)

T.test(GROUP, "weighted respects zero weights", function()
	local rng = Rng.new(3)
	for _ = 1, 50 do
		T.eq(rng:weighted({ 0, 5, 0 }), 2)
	end
end)

T.test(GROUP, "weighted rejects empty total", function()
	T.raises("ValueError", function()
		Rng.new(3):weighted({ 0, 0 })
	end, "positive")
end)

T.test(GROUP, "pick", function()
	local rng = Rng.new(5)
	T.eq(rng:pick({ "a" }), "a")
	T.raises("ValueError", function()
		rng:pick({})
	end, "empty")
end)
