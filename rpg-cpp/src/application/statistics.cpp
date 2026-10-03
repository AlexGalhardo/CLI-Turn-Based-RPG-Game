#include "application/statistics.hpp"

#include <algorithm>

namespace rpg::application {

std::int64_t RunStatistics::total_kills() const {
	std::int64_t total = 0;
	for (const auto& [monster_id, count] : kills) {
		total += count;
	}
	return total;
}

void RunStatistics::record(std::span<const Event> events, std::int64_t current_round) {
	for (const Event& event : events) {
		record_one(event, current_round);
	}
}

void RunStatistics::record_one(const Event& event, std::int64_t current_round) {
	const std::string& type = event.type;
	if (type == "player_attacked") {
		normal_attacks += 1;
		dealt(event);
	} else if (type == "spell_cast") {
		spells_cast[event.text("spellId")] += 1;
		dealt(event);
	} else if (type == "spell_healed") {
		spells_cast[event.text("spellId")] += 1;
		healing_done += event.integer("amount");
	} else if (type == "potion_used") {
		potions_used[event.text("potionId")] += 1;
		if (event.text("resource") == "hp") {
			healing_done += event.integer("amount");
		}
	} else if (type == "player_defended") {
		defends += 1;
	} else if (type == "monster_attacked") {
		damage_taken += event.integer("damage");
	} else if (type == "attack_dodged") {
		dodges += 1;
	} else if (type == "attack_parried") {
		parries += 1;
	} else if (type == "status_ticked") {
		if (event.text("target") == "player") {
			damage_taken += event.integer("damage");
		} else {
			damage_dealt += event.integer("damage");
		}
	} else if (type == "status_applied") {
		if (event.text("target") == "monster") {
			statuses_applied[event.text("status")] += 1;
		}
	} else if (type == "monster_killed") {
		kills[event.text("monsterId")] += 1;
		if (event.flag("isBoss")) {
			bosses_killed += 1;
		}
	} else if (type == "gold_looted") {
		gold_looted += event.integer("amount");
	} else if (type == "item_dropped") {
		items_dropped[event.text("rarity")] += 1;
		dropped_items.push_back(DroppedItem{event.text("itemId"), event.text("rarity"), current_round});
	} else if (type == "potion_bought") {
		potions_bought[event.text("potionId")] += event.integer("quantity");
		gold_spent += event.integer("gold");
	} else if (type == "item_bought") {
		gold_spent += event.integer("gold");
	} else if (type == "item_sold" || type == "item_auto_sold") {
		items_sold += 1;
		gold_earned += event.integer("gold");
	}
}

void RunStatistics::dealt(const Event& event) {
	const std::int64_t damage = event.integer("damage");
	damage_dealt += damage;
	highest_hit = std::max(highest_hit, damage);
	if (event.flag("crit")) {
		crits += 1;
	}
}

} // namespace rpg::application
