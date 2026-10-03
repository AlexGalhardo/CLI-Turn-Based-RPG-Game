#include "domain/definitions.hpp"

#include <algorithm>
#include <format>

namespace rpg::domain {

namespace {

template <typename Container> auto& find_by_id(Container& items, std::string_view id) {
	const auto found = std::ranges::find(items, id, &Container::value_type::id);
	if (found == items.end()) {
		throw UnknownIdError(id);
	}
	return *found;
}

template <typename Index> auto position(const Index& index, std::string_view id) {
	const auto found = index.find(id);
	if (found == index.end()) {
		throw UnknownIdError(id);
	}
	return found->second;
}

template <typename Definitions> std::map<std::string, std::size_t, std::less<>> build_index(const Definitions& items) {
	std::map<std::string, std::size_t, std::less<>> index;
	for (std::size_t i = 0; i < items.size(); ++i) {
		index.emplace(items[i].id, i);
	}
	return index;
}

} // namespace

std::int64_t MonsterDef::resistance(std::string_view element) const {
	const auto found = resistances.find(element);
	return found == resistances.end() ? 100 : found->second;
}

const MonsterAttack& MonsterDef::attack(std::string_view attack_id) const { return find_by_id(attacks, attack_id); }

const DifficultyDef& Balance::difficulty(std::string_view id) const { return find_by_id(difficulties, id); }

const EnemyClassDef& Balance::enemy_class(std::string_view id) const { return find_by_id(enemy_classes, id); }

const AutoBattleModeDef& AutoBattleDef::mode(std::string_view mode_id) const { return find_by_id(modes, mode_id); }

const RarityDef& Balance::rarity(std::string_view id) const { return find_by_id(rarities, id); }

bool Balance::has_difficulty(std::string_view id) const {
	return std::ranges::contains(difficulties, id, &DifficultyDef::id);
}

GameData& GameData::index() {
	vocation_index_ = build_index(vocations);
	spell_index_ = build_index(spells);
	potion_index_ = build_index(potions);
	status_index_ = build_index(statuses);
	item_index_ = build_index(items);
	creature_index_.clear();
	for (std::size_t i = 0; i < monsters.size(); ++i) {
		creature_index_.emplace(monsters[i].id, std::pair{false, i});
	}
	for (std::size_t i = 0; i < bosses.size(); ++i) {
		creature_index_.emplace(bosses[i].id, std::pair{true, i});
	}
	return *this;
}

const VocationDef& GameData::vocation(std::string_view id) const { return vocations[position(vocation_index_, id)]; }

const SpellDef& GameData::spell(std::string_view id) const { return spells[position(spell_index_, id)]; }

const MonsterDef& GameData::creature(std::string_view id) const {
	const auto [is_boss, index] = position(creature_index_, id);
	return is_boss ? bosses[index] : monsters[index];
}

const PotionDef& GameData::potion(std::string_view id) const { return potions[position(potion_index_, id)]; }

const StatusDef& GameData::status(std::string_view id) const { return statuses[position(status_index_, id)]; }

const ItemDef& GameData::item(std::string_view id) const { return items[position(item_index_, id)]; }

std::vector<const MonsterDef*> GameData::monsters_in_tier(std::int64_t tier) const {
	std::vector<const MonsterDef*> result;
	for (const MonsterDef& monster : monsters) {
		if (monster.tier == tier) {
			result.push_back(&monster);
		}
	}
	// std::string's operator< compares bytes, which is code-point order for UTF-8 (like Python's default sort).
	std::ranges::sort(result, {}, &MonsterDef::id);
	return result;
}

const MonsterDef& GameData::boss_of_tier(std::int64_t tier) const {
	const auto found = std::ranges::find(bosses, tier, &MonsterDef::tier);
	if (found == bosses.end()) {
		throw UnknownIdError(std::format("boss of tier {}", tier));
	}
	return *found;
}

} // namespace rpg::domain
