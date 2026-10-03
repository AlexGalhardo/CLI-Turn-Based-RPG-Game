#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "application/commands.hpp"
#include "application/events.hpp"
#include "application/run_state.hpp"
#include "domain/definitions.hpp"
#include "domain/rng.hpp"

namespace rpg::application {

// A pure state machine: step(command) → events (docs/architecture.md). The engine owns the run state and the PRNG;
// the game data is borrowed and must outlive it.
class GameEngine {
public:
	RunState state;

	// Starts a run in the merchant phase (round 0) with the vocation's starter weapon. An unknown vocation or
	// difficulty is an expected failure (std::unexpected with "invalid run config: ..."), not an exception.
	static std::expected<std::pair<GameEngine, std::vector<Event>>, std::string> new_run(
	    const domain::GameData& data, const RunConfig& config, std::uint64_t seed);

	// Continues a run from a saved state and PRNG state.
	static GameEngine restore(const domain::GameData& data, RunState state, std::uint32_t rng_state);

	[[nodiscard]] std::uint32_t rng_state() const { return rng_.state(); }
	[[nodiscard]] const domain::GameData& data() const { return *data_; }

	// Applies a command and returns its events.
	std::vector<Event> step(const Command& command);

private:
	GameEngine(const domain::GameData& data, RunState run_state, std::uint32_t rng_state) :
	    state(std::move(run_state)), data_(&data), rng_(rng_state) {}

	const domain::GameData* data_;
	domain::Rng rng_;

	std::vector<Event> dispatch(const Command& command);
	std::vector<Event> next_fight();
	std::vector<Event> battle_turn(const Command& command);
	std::vector<Event> victory();
	std::vector<Event> drops(const domain::MonsterInstance& monster);
	std::vector<Event> drop_item(const domain::EnemyClassDef& row);
	std::vector<Event> drop_potion();
	std::vector<Event> end_run();
	std::vector<Event> continue_run();
	std::vector<Event> defeat();
};

} // namespace rpg::application
