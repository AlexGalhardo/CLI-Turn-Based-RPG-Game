#include "domain/character.hpp"

#include <algorithm>

#include "domain/formulas.hpp"

namespace rpg::domain {

namespace {

std::int64_t total_of(const StatTotals& totals, std::string_view stat) {
	const auto found = totals.find(stat);
	return found == totals.end() ? 0 : found->second;
}

} // namespace

StatTotals item_stats(const ItemInstance& item, const GameData& data) {
	const ItemDef& definition = data.item(item.item_id);
	const RarityDef& rarity = data.balance.rarity(item.rarity);
	StatTotals stats;
	for (const auto& [stat, value] : definition.stats) {
		stats[stat] += pct(value, rarity.stat_pct);
	}
	for (const AffixRoll& affix : item.affixes) {
		stats[affix.stat] += affix.value;
	}
	return stats;
}

std::int64_t item_value(const ItemInstance& item, const GameData& data) {
	return pct(data.item(item.item_id).value, data.balance.rarity(item.rarity).value_pct);
}

std::int64_t CharacterSheet::protection(std::string_view element) const { return total_of(protections, element); }

CharacterSheet build_sheet(const Player& player, const GameData& data) {
	const VocationDef& vocation = data.vocation(player.vocation_id);
	const Caps& caps = data.balance.caps;
	StatTotals totals;
	for (const auto& [slot, item] : player.equipment) {
		for (const auto& [stat, value] : item_stats(item, data)) {
			totals[stat] += value;
		}
	}

	Element weapon_element{element::physical};
	if (const auto weapon = player.equipment.find(slot::weapon); weapon != player.equipment.end()) {
		if (const auto& item_element = data.item(weapon->second.item_id).element; item_element.has_value()) {
			weapon_element = *item_element;
		}
	}

	const auto total = [&](std::string_view stat) { return total_of(totals, stat); };
	const std::int64_t level_bonus = (player.level - 1) * vocation.melee_per_level;
	StatTotals protections;
	for (const std::string_view element : kElements) {
		protections.emplace(element, std::min(total(protection_stat(element)), caps.protection));
	}

	return CharacterSheet{
	    .max_hp = vocation.start_hp + (player.level - 1) * vocation.hp_per_level + total(stat::max_hp),
	    .max_mp = vocation.start_mp + (player.level - 1) * vocation.mp_per_level + total(stat::max_mp),
	    .hp_regen = vocation.hp_regen + total(stat::hp_regen),
	    .mp_regen = vocation.mp_regen + total(stat::mp_regen),
	    .melee_min = vocation.melee_min + level_bonus + total(stat::attack),
	    .melee_max = vocation.melee_max + level_bonus + total(stat::attack),
	    .weapon_element = weapon_element,
	    .armor = total(stat::armor),
	    .crit_chance = std::min(total(stat::crit_chance), caps.crit_chance),
	    .crit_damage = total(stat::crit_damage),
	    .spell_power = total(stat::spell_power),
	    .physical_damage = total(stat::physical_damage),
	    .dodge = std::min(total(stat::dodge), caps.dodge),
	    .parry = std::min(total(stat::parry), caps.parry),
	    .life_leech = std::min(total(stat::life_leech), caps.leech),
	    .mana_leech = std::min(total(stat::mana_leech), caps.leech),
	    .protections = std::move(protections),
	};
}

} // namespace rpg::domain
