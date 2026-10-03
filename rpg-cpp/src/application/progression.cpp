#include "application/progression.hpp"

#include <algorithm>

#include "domain/character.hpp"
#include "domain/formulas.hpp"

namespace rpg::application {

std::vector<Event> Progression::gain_experience(domain::Player& player, std::int64_t amount) const {
	player.xp += amount;
	std::vector<Event> events{Event{"xp_gained", {{"amount", amount}, {"total", player.xp}}}};
	const domain::VocationDef& vocation = data_->vocation(player.vocation_id);
	while (player.xp >= domain::xp_for_level(player.level + 1)) {
		player.level += 1;
		const domain::CharacterSheet sheet = domain::build_sheet(player, *data_);
		player.hp = std::min(sheet.max_hp, player.hp + vocation.hp_per_level);
		player.mp = std::min(sheet.max_mp, player.mp + vocation.mp_per_level);
		events.push_back(
		    Event{"level_up", {{"level", player.level}, {"maxHp", sheet.max_hp}, {"maxMp", sheet.max_mp}}});
	}
	return events;
}

std::vector<Event> Progression::after_cast(
    domain::Player& player, const domain::SpellDef& spell, std::int64_t mana_cost) const {
	const auto& levels = data_->balance.spell_levels;
	std::vector<Event> events;
	const std::int64_t uses_before = domain::count_of(player.spell_uses, spell.id);
	player.spell_uses[spell.id] = uses_before + 1;
	const auto& before = domain::spell_level_for_uses(uses_before, levels);
	const auto& after = domain::spell_level_for_uses(uses_before + 1, levels);
	if (after.level != before.level) {
		events.push_back(Event{"spell_level_up", {{"spellId", spell.id}, {"level", after.level}}});
	}

	player.mana_spent += mana_cost;
	while (player.mana_spent >= domain::mana_for_magic_level(player.magic_level, data_->balance)) {
		player.magic_level += 1;
		events.push_back(Event{"magic_level_up", {{"magicLevel", player.magic_level}}});
	}
	return events;
}

} // namespace rpg::application
