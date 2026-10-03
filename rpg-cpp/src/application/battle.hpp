#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "application/commands.hpp"
#include "application/events.hpp"
#include "application/progression.hpp"
#include "application/run_state.hpp"
#include "domain/character.hpp"
#include "domain/definitions.hpp"
#include "domain/rng.hpp"

namespace rpg::application {

enum class BattleOutcome { ongoing, victory, defeat };

// Resolves turns following docs/game-design.md §6. Every RNG call here is part of the contract.
// A short-lived view over the engine's state: it borrows data, rng and state for one turn.
class Battle {
public:
	Battle(const domain::GameData& data, domain::Rng& rng, RunState& state) :
	    data_(&data), rng_(&rng), state_(&state), progression_(data) {}

	// The error event of an invalid command. Validation never consumes randomness.
	[[nodiscard]] std::optional<Event> validate(const Command& command) const;

	// Resolves one player command and the monster's response.
	std::pair<std::vector<Event>, BattleOutcome> play_turn(const Command& command);

private:
	const domain::GameData* data_;
	domain::Rng* rng_;
	RunState* state_;
	Progression progression_;

	domain::Player& player() { return state_->player; }
	[[nodiscard]] const domain::Player& player() const { return state_->player; }
	domain::MonsterInstance& monster();
	[[nodiscard]] domain::CharacterSheet sheet() const { return domain::build_sheet(state_->player, *data_); }
	[[nodiscard]] std::int64_t spell_cost(const domain::SpellDef& spell) const;
	[[nodiscard]] std::int64_t monster_resistance(std::string_view element) const;

	void player_action(const Command& command, std::vector<Event>& events);
	void melee(std::vector<Event>& events);
	void cast(const domain::SpellDef& spell, std::vector<Event>& events);
	void drink(const std::string& potion_id, std::vector<Event>& events);
	std::pair<std::int64_t, bool> roll_crit(std::int64_t damage, const domain::CharacterSheet& sheet);
	[[nodiscard]] std::int64_t resisted(std::int64_t damage, std::string_view element) const;
	void hit_monster(std::int64_t damage);
	void leech(std::int64_t damage, const domain::CharacterSheet& sheet, std::vector<Event>& events);

	bool monster_phase(std::vector<Event>& events);
	void resolve_monster_attack(const domain::MonsterAttack& attack, bool charged, std::vector<Event>& events);
	bool end_of_turn(std::vector<Event>& events);

	void apply_status(
	    std::string_view target, const std::string& status_id, std::int64_t per_turn, std::vector<Event>& events);
	void tick(std::string_view target, std::vector<domain::ActiveStatus>& statuses, std::vector<Event>& events);
	[[nodiscard]] std::int64_t status_damage(
	    std::string_view target, std::int64_t per_turn, std::string_view element) const;
};

} // namespace rpg::application
