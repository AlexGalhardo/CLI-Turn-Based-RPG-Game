#include "application/auto_battle.hpp"

#include <stdexcept>

#include "domain/character.hpp"
#include "domain/formulas.hpp"

namespace rpg::application {

namespace {

constexpr std::string_view offense_attack = "attack";

// Highest `max`; ties go to the lowest id.
template <typename T> const T* strongest(const std::vector<const T*>& options) {
	const T* best = nullptr;
	for (const T* option : options) {
		if (best == nullptr || option->max > best->max || (option->max == best->max && option->id < best->id)) {
			best = option;
		}
	}
	return best;
}

} // namespace

std::string_view to_string(AutoBattleMode mode) {
	switch (mode) {
	case AutoBattleMode::melee:
		return "melee";
	case AutoBattleMode::spells:
		return "spells";
	case AutoBattleMode::balanced:
		return "balanced";
	}
	throw std::logic_error("unknown auto-battle mode");
}

AutoBattlePolicy::AutoBattlePolicy(const domain::GameData& data, AutoBattleMode mode) :
    data_(&data), mode_(&data.balance.auto_battle.mode(to_string(mode))) {}

Command AutoBattlePolicy::choose(const RunState& state) const {
	const domain::Player& player = state.player;
	const domain::CharacterSheet sheet = domain::build_sheet(player, *data_);
	const domain::AutoBattleDef& config = data_->balance.auto_battle;

	if (player.hp * 100 < sheet.max_hp * config.emergency_heal_below_pct) {
		if (auto command = heal(state)) {
			return *command;
		}
	}
	const std::int64_t every = mode_->support_every;
	if (state.turn % every == every - 1) {
		if (player.hp * 100 < sheet.max_hp * config.heal_below_pct) {
			if (auto command = heal(state)) {
				return *command;
			}
		}
		if (player.mp * 100 < sheet.max_mp * config.mana_below_pct) {
			if (const domain::PotionDef* potion = best_potion(state, "mp")) {
				return UsePotion{potion->id};
			}
		}
		if (telegraph_pending(state)) {
			return Defend{};
		}
	}
	return offense(state);
}

Command AutoBattlePolicy::offense(const RunState& state) const {
	if (mode_->offense == offense_attack) {
		return Attack{};
	}
	const domain::SpellDef* spell = strongest(affordable(state, "attack"));
	if (spell == nullptr) {
		return Attack{};
	}
	return Cast{spell->id};
}

std::optional<Command> AutoBattlePolicy::heal(const RunState& state) const {
	if (const domain::SpellDef* spell = strongest(affordable(state, "heal"))) {
		return Cast{spell->id};
	}
	if (const domain::PotionDef* potion = best_potion(state, "hp")) {
		return UsePotion{potion->id};
	}
	return std::nullopt;
}

std::vector<const domain::SpellDef*> AutoBattlePolicy::affordable(const RunState& state, std::string_view kind) const {
	const domain::Player& player = state.player;
	std::vector<const domain::SpellDef*> result;
	for (const std::string& spell_id : data_->vocation(player.vocation_id).spells) {
		const domain::SpellDef& spell = data_->spell(spell_id);
		const domain::SpellLevelDef& level =
		    domain::spell_level_for_uses(domain::count_of(player.spell_uses, spell.id), data_->balance.spell_levels);
		if (spell.kind == kind && domain::pct(spell.mana, level.mana_pct) <= player.mp) {
			result.push_back(&spell);
		}
	}
	return result;
}

const domain::PotionDef* AutoBattlePolicy::best_potion(const RunState& state, std::string_view resource) const {
	std::vector<const domain::PotionDef*> owned;
	for (const domain::PotionDef& potion : data_->potions) {
		if (potion.resource == resource && state.player.potion_count(potion.id) > 0) {
			owned.push_back(&potion);
		}
	}
	return strongest(owned);
}

// The boss announced its charged attack: its next action is the charge.
bool AutoBattlePolicy::telegraph_pending(const RunState& state) const {
	if (!state.monster.has_value() || !state.monster->is_boss) {
		return false;
	}
	const std::int64_t every = data_->balance.boss_telegraph_every;
	return state.monster->boss_actions % (every + 1) == every;
}

} // namespace rpg::application
