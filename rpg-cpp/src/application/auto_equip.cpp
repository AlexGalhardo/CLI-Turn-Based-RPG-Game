#include "application/auto_equip.hpp"

#include <algorithm>

#include "application/loot.hpp"
#include "domain/character.hpp"

namespace rpg::application {

std::optional<domain::ItemInstance> best_bag_item(
    const RunState& state, const domain::GameData& data, std::string_view slot) {
	const domain::Player& player = state.player;
	const domain::VocationDef& vocation = data.vocation(player.vocation_id);
	const domain::ItemInstance* best = nullptr;
	std::int64_t best_score = 0;
	for (const domain::ItemInstance& item : player.bag) {
		const domain::ItemDef& definition = data.item(item.item_id);
		if (definition.slot != slot || !can_use(definition, vocation) ||
		    domain::required_level(item, data) > player.level) {
			continue;
		}
		const std::int64_t score = domain::item_score(item, data);
		if (best == nullptr || score > best_score || (score == best_score && item.uid < best->uid)) {
			best = &item;
			best_score = score;
		}
	}
	if (best == nullptr) {
		return std::nullopt;
	}
	return *best;
}

std::vector<Event> auto_equip(RunState& state, const domain::GameData& data) {
	domain::Player& player = state.player;
	std::vector<Event> events;
	for (const std::string_view slot : domain::kEquipmentSlotOrder) {
		const auto best = best_bag_item(state, data, slot);
		if (!best.has_value()) {
			continue;
		}
		const std::int64_t score = domain::item_score(*best, data);
		std::optional<domain::ItemInstance> current;
		if (const auto found = player.equipment.find(slot); found != player.equipment.end()) {
			current = found->second;
		}
		if (current.has_value() && score <= domain::item_score(*current, data)) {
			continue;
		}
		std::erase(player.bag, *best);
		player.equipment.insert_or_assign(std::string(slot), *best);
		events.push_back(Event{"item_auto_equipped",
		    {{"uid", best->uid}, {"itemId", best->item_id}, {"slot", std::string(slot)}, {"score", score}}});
		if (current.has_value()) {
			const std::int64_t gold = domain::item_value(*current, data);
			player.gold += gold;
			events.push_back(
			    Event{"item_auto_sold", {{"uid", current->uid}, {"itemId", current->item_id}, {"gold", gold}}});
		}
	}
	if (!events.empty()) {
		const domain::CharacterSheet sheet = domain::build_sheet(player, data);
		player.hp = std::min(player.hp, sheet.max_hp);
		player.mp = std::min(player.mp, sheet.max_mp);
	}
	return events;
}

} // namespace rpg::application
