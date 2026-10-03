#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "application/commands.hpp"
#include "application/run_state.hpp"
#include "domain/definitions.hpp"

namespace rpg::application {

// The sum of an item's stats (the bot's naive "better item" heuristic).
std::int64_t item_score(const domain::ItemInstance& item, const domain::GameData& data);

// A deterministic heuristic player used by the simulator and the end-to-end parity tests. Its decisions are part of
// the golden "bot full run" files: it mirrors the reference GreedyBot decision by decision.
class GreedyBot {
public:
	explicit GreedyBot(const domain::GameData& data) : data_(&data) {}

	// The next command for the current state.
	[[nodiscard]] Command choose(const RunState& state) const;

private:
	const domain::GameData* data_;

	[[nodiscard]] Command battle(const RunState& state) const;
	[[nodiscard]] bool charge_incoming(const domain::MonsterInstance& monster) const;
	[[nodiscard]] std::int64_t cost(const RunState& state, const domain::SpellDef& spell) const;
	[[nodiscard]] std::vector<const domain::SpellDef*> spells(const RunState& state, std::string_view kind) const;
	[[nodiscard]] std::optional<Command> heal(const RunState& state) const;
	[[nodiscard]] const domain::PotionDef* best_owned_potion(const RunState& state, std::string_view resource) const;
	[[nodiscard]] const domain::SpellDef* best_attack_spell(
	    const RunState& state, const domain::MonsterInstance& monster) const;
	[[nodiscard]] Command merchant(const RunState& state) const;
	[[nodiscard]] std::optional<Command> potion_purchase(const RunState& state, std::string_view resource) const;
};

} // namespace rpg::application
