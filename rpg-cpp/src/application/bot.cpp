#include "application/bot.hpp"

#include <algorithm>

#include "application/loot.hpp"
#include "application/merchant.hpp"
#include "domain/character.hpp"
#include "domain/formulas.hpp"

namespace rpg::application {

namespace {

constexpr std::int64_t heal_threshold_pct = 45;
constexpr std::int64_t mana_potion_threshold_pct = 25;
constexpr std::int64_t max_potion_stock = 20;
constexpr std::int64_t potion_stock_base = 5;
constexpr std::int64_t potion_stock_round_divisor = 5;

// Mirrors Python's max(items, key=lambda x: (number, id)): the highest number wins, ties go to the higher id.
template <typename T, typename Key> const T* max_by(const std::vector<const T*>& items, Key key) {
	const T* best = nullptr;
	for (const T* item : items) {
		if (best == nullptr || key(*best) < key(*item)) {
			best = item;
		}
	}
	return best;
}

} // namespace

Command GreedyBot::choose(const RunState& state) const {
	if (state.phase == domain::Phase::battle) {
		return battle(state);
	}
	if (state.phase == domain::Phase::victory) {
		return EndRun{};
	}
	return merchant(state);
}

Command GreedyBot::battle(const RunState& state) const {
	const domain::Player& player = state.player;
	const domain::MonsterInstance& monster = *state.monster;
	const domain::CharacterSheet sheet = domain::build_sheet(player, *data_);

	if (charge_incoming(monster)) {
		return Defend{};
	}
	if (player.hp * 100 < sheet.max_hp * heal_threshold_pct) {
		if (auto command = heal(state)) {
			return *command;
		}
	}
	if (player.mp * 100 < sheet.max_mp * mana_potion_threshold_pct) {
		if (const auto* potion = best_owned_potion(state, "mp")) {
			return UsePotion{potion->id};
		}
	}
	if (const auto* spell = best_attack_spell(state, monster)) {
		return Cast{spell->id};
	}
	return Attack{};
}

bool GreedyBot::charge_incoming(const domain::MonsterInstance& monster) const {
	if (!monster.is_boss) {
		return false;
	}
	const std::int64_t every = data_->balance.boss_telegraph_every;
	return monster.boss_actions % (every + 1) == every;
}

std::int64_t GreedyBot::cost(const RunState& state, const domain::SpellDef& spell) const {
	const auto& level =
	    domain::spell_level_for_uses(domain::count_of(state.player.spell_uses, spell.id), data_->balance.spell_levels);
	return domain::pct(spell.mana, level.mana_pct);
}

std::vector<const domain::SpellDef*> GreedyBot::spells(const RunState& state, std::string_view kind) const {
	std::vector<const domain::SpellDef*> result;
	for (const std::string& id : data_->vocation(state.player.vocation_id).spells) {
		const domain::SpellDef& spell = data_->spell(id);
		if (spell.kind == kind && cost(state, spell) <= state.player.mp) {
			result.push_back(&spell);
		}
	}
	return result;
}

std::optional<Command> GreedyBot::heal(const RunState& state) const {
	const auto by_power = [](const domain::SpellDef& spell) { return std::pair{spell.max, spell.id}; };
	if (const auto* spell = max_by(spells(state, "heal"), by_power)) {
		return Cast{spell->id};
	}
	if (const auto* potion = best_owned_potion(state, "hp")) {
		return UsePotion{potion->id};
	}
	return std::nullopt;
}

const domain::PotionDef* GreedyBot::best_owned_potion(const RunState& state, std::string_view resource) const {
	std::vector<const domain::PotionDef*> owned;
	for (const domain::PotionDef& potion : data_->potions) {
		if (potion.resource == resource && state.player.potion_count(potion.id) > 0) {
			owned.push_back(&potion);
		}
	}
	return max_by(owned, [](const domain::PotionDef& potion) { return std::pair{potion.max, potion.id}; });
}

const domain::SpellDef* GreedyBot::best_attack_spell(
    const RunState& state, const domain::MonsterInstance& monster) const {
	const domain::MonsterDef& creature = data_->creature(monster.creature_id);
	std::vector<const domain::SpellDef*> candidates;
	for (const domain::SpellDef* spell : spells(state, "attack")) {
		if (creature.resistance(spell->element) > 0) {
			candidates.push_back(spell);
		}
	}
	return max_by(candidates, [&](const domain::SpellDef& spell) {
		return std::pair{(spell.min + spell.max) * creature.resistance(spell.element), spell.id};
	});
}

Command GreedyBot::merchant(const RunState& state) const {
	const domain::Player& player = state.player;
	const domain::VocationDef& vocation = data_->vocation(player.vocation_id);
	std::vector<domain::ItemInstance> bag = player.bag;
	std::ranges::sort(bag, {}, &domain::ItemInstance::uid);

	for (const domain::ItemInstance& item : bag) {
		const domain::ItemDef& definition = data_->item(item.item_id);
		if (!can_use(definition, vocation) || domain::required_level(item, *data_) > player.level) {
			continue;
		}
		const auto current = player.equipment.find(definition.slot);
		if (current == player.equipment.end() ||
		    domain::item_score(item, *data_) > domain::item_score(current->second, *data_)) {
			return Equip{item.uid};
		}
	}
	if (!bag.empty()) {
		return SellItem{bag.front().uid};
	}
	if (auto purchase = potion_purchase(state, "hp")) {
		return *purchase;
	}
	if (auto purchase = potion_purchase(state, "mp")) {
		return *purchase;
	}
	return NextFight{};
}

std::optional<Command> GreedyBot::potion_purchase(const RunState& state, std::string_view resource) const {
	const std::vector<std::string> unlocked = available_potions(state, *data_);
	std::vector<const domain::PotionDef*> options;
	for (const domain::PotionDef& potion : data_->potions) {
		if (potion.resource == resource && std::ranges::contains(unlocked, potion.id)) {
			options.push_back(&potion);
		}
	}
	const auto* best =
	    max_by(options, [](const domain::PotionDef& potion) { return std::pair{potion.max, potion.id}; });
	if (best == nullptr) {
		return std::nullopt;
	}

	std::int64_t owned = 0;
	for (const domain::PotionDef* potion : options) {
		owned += state.player.potion_count(potion->id);
	}
	const std::int64_t target =
	    std::min(max_potion_stock, potion_stock_base + state.round / potion_stock_round_divisor);
	const std::int64_t budget = resource == "mp" ? state.player.gold / 2 : state.player.gold;
	const std::int64_t quantity = std::min(target - owned, budget / best->price);
	if (quantity <= 0) {
		return std::nullopt;
	}
	return BuyPotion{best->id, quantity};
}

} // namespace rpg::application
