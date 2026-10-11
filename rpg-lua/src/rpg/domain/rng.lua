--- mulberry32 PRNG shared by every implementation (docs/cross-language-parity.md).
--- Lua integers are 64-bit, so every intermediate value is masked back to 32 bits.

local base = require("rpg.domain.base")

local MASK = 0xFFFFFFFF

local function imul(a, b)
	return (a * b) & MASK
end

--- Deterministic 32-bit generator. The engine must never use any other source of randomness.
local Rng = base.class("Rng")

function Rng.new(seed)
	return setmetatable({ state = seed & MASK }, Rng)
end

function Rng:next_u32()
	self.state = (self.state + 0x6D2B79F5) & MASK
	local t = self.state
	t = imul(t ~ (t >> 15), t | 1)
	t = (t ~ ((t + imul(t ~ (t >> 7), t | 61)) & MASK)) & MASK
	return (t ~ (t >> 14)) & MASK
end

function Rng:roll(minimum, maximum)
	if minimum > maximum then
		base.raise("ValueError", string.format("invalid range [%d, %d]", minimum, maximum))
	end
	return minimum + self:next_u32() % (maximum - minimum + 1)
end

function Rng:chance(percent)
	-- Certain outcomes don't consume a number, so adding a 0% effect never shifts the sequence.
	if percent <= 0 then
		return false
	end
	if percent >= 100 then
		return true
	end
	return self:roll(1, 100) <= percent
end

--- Returns the (1-based) position of the chosen weight.
function Rng:weighted(weights)
	local total = 0
	for _, weight in ipairs(weights) do
		total = total + weight
	end
	if total <= 0 then
		base.raise("ValueError", "weights must have a positive sum")
	end
	local roll = self:roll(1, total)
	local cumulative = 0
	for index, weight in ipairs(weights) do
		cumulative = cumulative + weight
		if cumulative >= roll then
			return index
		end
	end
	error("unreachable")
end

function Rng:pick(items)
	if #items == 0 then
		base.raise("ValueError", "cannot pick from an empty sequence")
	end
	return items[self:roll(0, #items - 1) + 1]
end

return Rng
