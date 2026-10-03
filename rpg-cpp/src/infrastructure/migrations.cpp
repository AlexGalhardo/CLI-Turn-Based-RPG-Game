#include "infrastructure/migrations.hpp"

#include <cstdint>
#include <string_view>

#include "domain/enums.hpp"

namespace rpg::infrastructure {

using nlohmann::json;

namespace {

constexpr std::string_view removed_rarity = "epic";
constexpr std::string_view replacement_rarity = "legendary";

std::int64_t version_of(const json& document) { return document.value("schemaVersion", std::int64_t{1}); }

// Python's dict.setdefault: only adds a missing key.
void set_default(json& object, const char* key, json value) {
	if (!object.contains(key)) {
		object[key] = std::move(value);
	}
}

void rename_rarities(json& items) {
	if (!items.is_array()) {
		return;
	}
	for (json& item : items) {
		if (item.is_object() && item.value("rarity", std::string{}) == removed_rarity) {
			item["rarity"] = replacement_rarity;
		}
	}
}

void stats_v1_to_v2(json& stats) {
	set_default(stats, "itemsAutoEquipped", 0);
	set_default(stats, "elitesKilled", 0);
	set_default(stats, "potionsDropped", json::object());
	json& dropped = stats.at("itemsDropped");
	if (const auto epic = dropped.find(std::string(removed_rarity)); epic != dropped.end()) {
		const auto count = epic->get<std::int64_t>();
		dropped.erase(epic);
		const std::int64_t legendary = dropped.value(std::string(replacement_rarity), std::int64_t{0});
		dropped[std::string(replacement_rarity)] = legendary + count;
	}
	if (const auto items = stats.find("droppedItems"); items != stats.end()) {
		rename_rarities(*items);
	}
}

void run_v1_to_v2(json& run) {
	set_default(run.at("config"), "autoEquip", false);
	set_default(run, "won", false);
	if (const auto monster = run.find("monster"); monster != run.end() && monster->is_object()) {
		const bool is_boss = monster->value("isBoss", false);
		set_default(
		    *monster, "enemyClass", std::string(is_boss ? domain::enemy_class::boss : domain::enemy_class::normal));
	}
	json& player = run.at("player");
	if (const auto bag = player.find("bag"); bag != player.end()) {
		rename_rarities(*bag);
	}
	if (const auto equipment = player.find("equipment"); equipment != player.end() && equipment->is_object()) {
		for (auto& [slot, item] : equipment->items()) {
			if (item.is_object() && item.value("rarity", std::string{}) == removed_rarity) {
				item["rarity"] = replacement_rarity;
			}
		}
	}
	if (const auto stock = run.find("merchantStock"); stock != run.end()) {
		rename_rarities(*stock);
	}
	stats_v1_to_v2(run.at("stats"));
}

} // namespace

json& migrate_save(json& document) {
	if (version_of(document) < 2) {
		run_v1_to_v2(document.at("run"));
		document["schemaVersion"] = 2;
	}
	return document;
}

json& migrate_history(json& document) {
	if (version_of(document) < 2) {
		set_default(document, "won", false);
		stats_v1_to_v2(document.at("stats"));
		document["schemaVersion"] = 2;
	}
	return document;
}

json& migrate_profile(json& document) {
	if (version_of(document) < 2) {
		if (const auto hall = document.find("hallOfFame"); hall != document.end() && hall->is_array()) {
			for (json& entry : *hall) {
				set_default(entry, "won", false);
			}
		}
		document["schemaVersion"] = 2;
	}
	return document;
}

json& migrate_settings(json& document) {
	if (version_of(document) < 2) {
		set_default(document, "autoEquip", false);
		set_default(document, "battleSpeed", 1);
		document["schemaVersion"] = 2;
	}
	return document;
}

} // namespace rpg::infrastructure
