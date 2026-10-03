#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace rpg::domain {

// The mulberry32 generator shared by every implementation (docs/cross-language-parity.md).
// The engine must never use any other source of randomness.
class Rng {
public:
	// The seed is taken modulo 2^32.
	explicit Rng(std::uint64_t seed) noexcept : state_(static_cast<std::uint32_t>(seed)) {}

	[[nodiscard]] std::uint32_t state() const noexcept { return state_; }

	std::uint32_t next_u32() noexcept;

	// An integer in [minimum, maximum].
	std::int64_t roll(std::int64_t minimum, std::int64_t maximum);

	// Certain outcomes don't consume a number, so a 0% effect never shifts the sequence.
	bool chance(std::int64_t percent);

	// The index chosen by weight.
	std::size_t weighted(std::span<const std::int64_t> weights);

	template <typename T> const T& pick(const std::vector<T>& items) {
		if (items.empty()) {
			throw std::invalid_argument("cannot pick from an empty sequence");
		}
		return items[static_cast<std::size_t>(roll(0, static_cast<std::int64_t>(items.size()) - 1))];
	}

private:
	std::uint32_t state_;
};

} // namespace rpg::domain
