#include "application/run_state.hpp"

namespace rpg::application {

using nlohmann::json;
using nlohmann::ordered_json;

namespace {

ordered_json counts_to_json(const domain::CountMap& counts) {
	ordered_json result = ordered_json::object();
	for (const auto& [key, value] : counts) {
		result[key] = value;
	}
	return result;
}

domain::CountMap counts_from_json(const json& document, const char* key) {
	domain::CountMap result;
	if (const auto found = document.find(key); found != document.end() && !found->is_null()) {
		for (const auto& [name, value] : found->items()) {
			result.emplace(name, value.get<std::int64_t>());
		}
	}
	return result;
}

ordered_json statuses_to_json(const std::vector<domain::ActiveStatus>& statuses) {
	ordered_json result = ordered_json::array();
	for (const auto& status : statuses) {
		result.push_back(
		    ordered_json{{"statusId", status.status_id}, {"turns", status.turns}, {"perTurn", status.per_turn}});
	}
	return result;
}

std::vector<domain::ActiveStatus> statuses_from_json(const json& document, const char* key) {
	std::vector<domain::ActiveStatus> result;
	if (const auto found = document.find(key); found != document.end() && !found->is_null()) {
		for (const auto& status : *found) {
			result.push_back(domain::ActiveStatus{
			    status.at("statusId").get<std::string>(),
			    status.at("turns").get<std::int64_t>(),
			    status.at("perTurn").get<std::int64_t>(),
			});
		}
	}
	return result;
}

ordered_json item_to_json(const domain::ItemInstance& item) {
	ordered_json affixes = ordered_json::array();
	for (const auto& affix : item.affixes) {
		affixes.push_back(ordered_json{{"stat", affix.stat}, {"value", affix.value}});
	}
	return ordered_json{
	    {"uid", item.uid},
	    {"itemId", item.item_id},
	    {"rarity", item.rarity},
	    {"tier", item.tier},
	    {"affixes", affixes},
	};
}

domain::ItemInstance item_from_json(const json& document) {
	domain::ItemInstance item{
	    .uid = document.at("uid").get<std::int64_t>(),
	    .item_id = document.at("itemId").get<std::string>(),
	    .rarity = document.at("rarity").get<std::string>(),
	    .tier = document.at("tier").get<std::int64_t>(),
	    .affixes = {},
	};
	if (const auto found = document.find("affixes"); found != document.end() && !found->is_null()) {
		for (const auto& affix : *found) {
			item.affixes.push_back(
			    domain::AffixRoll{affix.at("stat").get<std::string>(), affix.at("value").get<std::int64_t>()});
		}
	}
	return item;
}

std::vector<domain::ItemInstance> items_from_json(const json& document, const char* key) {
	std::vector<domain::ItemInstance> result;
	if (const auto found = document.find(key); found != document.end() && !found->is_null()) {
		for (const auto& item : *found) {
			result.push_back(item_from_json(item));
		}
	}
	return result;
}

ordered_json items_to_json(const std::vector<domain::ItemInstance>& items) {
	ordered_json result = ordered_json::array();
	for (const auto& item : items) {
		result.push_back(item_to_json(item));
	}
	return result;
}

ordered_json player_to_json(const domain::Player& player) {
	ordered_json equipment = ordered_json::object();
	for (const auto& [slot, item] : player.equipment) {
		equipment[slot] = item_to_json(item);
	}
	return ordered_json{
	    {"name", player.name},
	    {"vocationId", player.vocation_id},
	    {"hp", player.hp},
	    {"mp", player.mp},
	    {"gold", player.gold},
	    {"level", player.level},
	    {"xp", player.xp},
	    {"magicLevel", player.magic_level},
	    {"manaSpent", player.mana_spent},
	    {"potions", counts_to_json(player.potions)},
	    {"equipment", equipment},
	    {"bag", items_to_json(player.bag)},
	    {"spellUses", counts_to_json(player.spell_uses)},
	    {"statuses", statuses_to_json(player.statuses)},
	    {"stunCooldown", player.stun_cooldown},
	    {"defending", player.defending},
	};
}

domain::Player player_from_json(const json& document) {
	domain::Player player{
	    .name = document.at("name").get<std::string>(),
	    .vocation_id = document.at("vocationId").get<std::string>(),
	    .hp = document.at("hp").get<std::int64_t>(),
	    .mp = document.at("mp").get<std::int64_t>(),
	    .gold = document.at("gold").get<std::int64_t>(),
	    .level = document.at("level").get<std::int64_t>(),
	    .xp = document.at("xp").get<std::int64_t>(),
	    .magic_level = document.at("magicLevel").get<std::int64_t>(),
	    .mana_spent = document.at("manaSpent").get<std::int64_t>(),
	    .potions = counts_from_json(document, "potions"),
	    .equipment = {},
	    .bag = items_from_json(document, "bag"),
	    .spell_uses = counts_from_json(document, "spellUses"),
	    .statuses = statuses_from_json(document, "statuses"),
	    .stun_cooldown = document.value("stunCooldown", std::int64_t{0}),
	    .defending = document.value("defending", false),
	};
	if (const auto found = document.find("equipment"); found != document.end() && !found->is_null()) {
		for (const auto& [slot, item] : found->items()) {
			player.equipment.emplace(slot, item_from_json(item));
		}
	}
	return player;
}

ordered_json attack_to_json(const domain::MonsterAttack& attack) {
	ordered_json result{
	    {"id", attack.id},
	    {"element", attack.element},
	    {"min", attack.min},
	    {"max", attack.max},
	    {"weight", attack.weight},
	};
	// The reference omits `status` when the attack has none (it never writes null here).
	if (attack.status.has_value()) {
		result["status"] = ordered_json{
		    {"id", attack.status->status}, {"chance", attack.status->chance}, {"damagePct", attack.status->damage_pct}};
	}
	return result;
}

domain::MonsterAttack attack_from_json(const json& document) {
	domain::MonsterAttack attack{
	    .id = document.at("id").get<std::string>(),
	    .element = document.at("element").get<std::string>(),
	    .min = document.at("min").get<std::int64_t>(),
	    .max = document.at("max").get<std::int64_t>(),
	    .weight = document.at("weight").get<std::int64_t>(),
	    .status = std::nullopt,
	};
	if (const auto found = document.find("status"); found != document.end() && !found->is_null()) {
		attack.status = domain::StatusOnHit{
		    found->at("id").get<std::string>(),
		    found->at("chance").get<std::int64_t>(),
		    found->at("damagePct").get<std::int64_t>(),
		};
	}
	return attack;
}

ordered_json monster_to_json(const domain::MonsterInstance& monster) {
	ordered_json attacks = ordered_json::array();
	for (const auto& attack : monster.attacks) {
		attacks.push_back(attack_to_json(attack));
	}
	return ordered_json{
	    {"creatureId", monster.creature_id},
	    {"isBoss", monster.is_boss},
	    {"enemyClass", monster.enemy_class},
	    {"hp", monster.hp},
	    {"maxHp", monster.max_hp},
	    {"xp", monster.xp},
	    {"goldMin", monster.gold_min},
	    {"goldMax", monster.gold_max},
	    {"attacks", attacks},
	    {"statuses", statuses_to_json(monster.statuses)},
	    {"stunCooldown", monster.stun_cooldown},
	    {"bossActions", monster.boss_actions},
	};
}

domain::MonsterInstance monster_from_json(const json& document) {
	domain::MonsterInstance monster{
	    .creature_id = document.at("creatureId").get<std::string>(),
	    .is_boss = document.at("isBoss").get<bool>(),
	    .enemy_class = document.at("enemyClass").get<std::string>(),
	    .hp = document.at("hp").get<std::int64_t>(),
	    .max_hp = document.at("maxHp").get<std::int64_t>(),
	    .xp = document.at("xp").get<std::int64_t>(),
	    .gold_min = document.at("goldMin").get<std::int64_t>(),
	    .gold_max = document.at("goldMax").get<std::int64_t>(),
	    .attacks = {},
	    .statuses = statuses_from_json(document, "statuses"),
	    .stun_cooldown = document.value("stunCooldown", std::int64_t{0}),
	    .boss_actions = document.value("bossActions", std::int64_t{0}),
	};
	for (const auto& attack : document.at("attacks")) {
		monster.attacks.push_back(attack_from_json(attack));
	}
	return monster;
}

} // namespace

ordered_json to_json(const RunConfig& config) {
	return ordered_json{
	    {"name", config.name},
	    {"vocation", config.vocation_id},
	    {"difficulty", config.difficulty_id},
	    {"autoEquip", config.auto_equip},
	};
}

ordered_json to_json(const RunStatistics& stats) {
	ordered_json dropped = ordered_json::array();
	for (const auto& item : stats.dropped_items) {
		dropped.push_back(ordered_json{{"itemId", item.item_id}, {"rarity", item.rarity}, {"round", item.round}});
	}
	return ordered_json{
	    {"damageDealt", stats.damage_dealt},
	    {"damageTaken", stats.damage_taken},
	    {"healingDone", stats.healing_done},
	    {"highestHit", stats.highest_hit},
	    {"normalAttacks", stats.normal_attacks},
	    {"crits", stats.crits},
	    {"dodges", stats.dodges},
	    {"parries", stats.parries},
	    {"defends", stats.defends},
	    {"goldLooted", stats.gold_looted},
	    {"goldSpent", stats.gold_spent},
	    {"goldEarned", stats.gold_earned},
	    {"itemsSold", stats.items_sold},
	    {"itemsAutoEquipped", stats.items_auto_equipped},
	    {"bossesKilled", stats.bosses_killed},
	    {"elitesKilled", stats.elites_killed},
	    {"spellsCast", counts_to_json(stats.spells_cast)},
	    {"potionsUsed", counts_to_json(stats.potions_used)},
	    {"potionsBought", counts_to_json(stats.potions_bought)},
	    {"potionsDropped", counts_to_json(stats.potions_dropped)},
	    {"itemsDropped", counts_to_json(stats.items_dropped)},
	    {"kills", counts_to_json(stats.kills)},
	    {"statusesApplied", counts_to_json(stats.statuses_applied)},
	    {"droppedItems", dropped},
	};
}

ordered_json to_json(const RunState& state) {
	return ordered_json{
	    {"seed", state.seed},
	    {"config", to_json(state.config)},
	    {"player", player_to_json(state.player)},
	    {"phase", domain::to_string(state.phase)},
	    {"round", state.round},
	    {"turn", state.turn},
	    {"monster", state.monster.has_value() ? monster_to_json(*state.monster) : ordered_json(nullptr)},
	    {"merchantStock", items_to_json(state.merchant_stock)},
	    {"nextItemUid", state.next_item_uid},
	    {"deathCause", state.death_cause.has_value() ? ordered_json(*state.death_cause) : ordered_json(nullptr)},
	    {"won", state.won},
	    {"stats", to_json(state.stats)},
	};
}

RunStatistics run_statistics_from_json(const json& document) {
	RunStatistics stats{
	    .damage_dealt = document.value("damageDealt", std::int64_t{0}),
	    .damage_taken = document.value("damageTaken", std::int64_t{0}),
	    .healing_done = document.value("healingDone", std::int64_t{0}),
	    .highest_hit = document.value("highestHit", std::int64_t{0}),
	    .normal_attacks = document.value("normalAttacks", std::int64_t{0}),
	    .crits = document.value("crits", std::int64_t{0}),
	    .dodges = document.value("dodges", std::int64_t{0}),
	    .parries = document.value("parries", std::int64_t{0}),
	    .defends = document.value("defends", std::int64_t{0}),
	    .gold_looted = document.value("goldLooted", std::int64_t{0}),
	    .gold_spent = document.value("goldSpent", std::int64_t{0}),
	    .gold_earned = document.value("goldEarned", std::int64_t{0}),
	    .items_sold = document.value("itemsSold", std::int64_t{0}),
	    .items_auto_equipped = document.value("itemsAutoEquipped", std::int64_t{0}),
	    .bosses_killed = document.value("bossesKilled", std::int64_t{0}),
	    .elites_killed = document.value("elitesKilled", std::int64_t{0}),
	    .spells_cast = counts_from_json(document, "spellsCast"),
	    .potions_used = counts_from_json(document, "potionsUsed"),
	    .potions_bought = counts_from_json(document, "potionsBought"),
	    .potions_dropped = counts_from_json(document, "potionsDropped"),
	    .items_dropped = counts_from_json(document, "itemsDropped"),
	    .kills = counts_from_json(document, "kills"),
	    .statuses_applied = counts_from_json(document, "statusesApplied"),
	    .dropped_items = {},
	};
	if (const auto found = document.find("droppedItems"); found != document.end() && !found->is_null()) {
		for (const auto& item : *found) {
			stats.dropped_items.push_back(DroppedItem{
			    item.at("itemId").get<std::string>(),
			    item.at("rarity").get<std::string>(),
			    item.at("round").get<std::int64_t>(),
			});
		}
	}
	return stats;
}

RunState run_state_from_json(const json& document) {
	const json& config = document.at("config");
	RunState state{
	    .seed = document.at("seed").get<std::uint64_t>(),
	    .config =
	        RunConfig{
	            config.at("name").get<std::string>(),
	            config.at("vocation").get<std::string>(),
	            config.at("difficulty").get<std::string>(),
	            config.value("autoEquip", false),
	        },
	    .player = player_from_json(document.at("player")),
	    .phase = domain::phase_from_string(document.at("phase").get<std::string>()),
	    .round = document.at("round").get<std::int64_t>(),
	    .turn = document.value("turn", std::int64_t{0}),
	    .monster = std::nullopt,
	    .merchant_stock = items_from_json(document, "merchantStock"),
	    .next_item_uid = document.at("nextItemUid").get<std::int64_t>(),
	    .death_cause = std::nullopt,
	    .won = document.value("won", false),
	    .stats = run_statistics_from_json(document.at("stats")),
	};
	if (const auto found = document.find("monster"); found != document.end() && !found->is_null()) {
		state.monster = monster_from_json(*found);
	}
	if (const auto found = document.find("deathCause"); found != document.end() && !found->is_null()) {
		state.death_cause = found->get<std::string>();
	}
	return state;
}

} // namespace rpg::application
