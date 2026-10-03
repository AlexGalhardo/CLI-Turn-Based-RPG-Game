#include "domain/rng.hpp"

#include <format>

namespace rpg::domain {

// unsigned int is never promoted to int, so these products wrap modulo 2^32 exactly like Math.imul in TypeScript.
static_assert(sizeof(unsigned int) == sizeof(std::uint32_t), "the PRNG relies on 32-bit unsigned arithmetic");

std::uint32_t Rng::next_u32() noexcept {
	state_ += 0x6D2B79F5U;
	std::uint32_t t = state_;
	t = (t ^ (t >> 15U)) * (t | 1U);
	t ^= t + (t ^ (t >> 7U)) * (t | 61U);
	return t ^ (t >> 14U);
}

std::int64_t Rng::roll(std::int64_t minimum, std::int64_t maximum) {
	if (minimum > maximum) {
		throw std::invalid_argument(std::format("invalid range [{}, {}]", minimum, maximum));
	}
	const auto span = static_cast<std::uint64_t>(maximum - minimum) + 1U;
	return minimum + static_cast<std::int64_t>(static_cast<std::uint64_t>(next_u32()) % span);
}

bool Rng::chance(std::int64_t percent) {
	if (percent <= 0) {
		return false;
	}
	if (percent >= 100) {
		return true;
	}
	return roll(1, 100) <= percent;
}

std::size_t Rng::weighted(std::span<const std::int64_t> weights) {
	std::int64_t total = 0;
	for (const std::int64_t weight : weights) {
		total += weight;
	}
	if (total <= 0) {
		throw std::invalid_argument("weights must have a positive sum");
	}
	const std::int64_t target = roll(1, total);
	std::int64_t cumulative = 0;
	for (std::size_t index = 0; index < weights.size(); ++index) {
		cumulative += weights[index];
		if (cumulative >= target) {
			return index;
		}
	}
	throw std::logic_error("unreachable: the target never exceeds the total weight");
}

} // namespace rpg::domain
