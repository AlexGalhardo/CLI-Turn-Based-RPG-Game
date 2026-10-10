#include "infrastructure/migrations.h"

#include <string.h>

#define REMOVED_RARITY "epic"
#define REPLACEMENT_RARITY "legendary"

static int64_t version_of(const JsonValue *document) {
	const JsonValue *version = json_get(document, "schemaVersion");
	return version != NULL && version->type == JSON_INT ? version->integer : 1;
}

// Sets `key` only when the object does not have it yet (takes ownership of `value` either way).
static void set_default(JsonValue *object, const char *key, JsonValue *value) {
	if (object == NULL || object->type != JSON_OBJECT || json_has(object, key)) {
		json_free(value);
		return;
	}
	json_set(object, key, value);
}

static void rename_rarity(JsonValue *item) {
	const JsonValue *rarity = json_get(item, "rarity");
	if (rarity != NULL && rarity->type == JSON_STRING && strcmp(rarity->string, REMOVED_RARITY) == 0) {
		json_set(item, "rarity", json_string(REPLACEMENT_RARITY));
	}
}

// Works on a list of items and on an object whose values are items (the equipment).
static void rename_rarities(JsonValue *items) {
	if (items == NULL || (items->type != JSON_ARRAY && items->type != JSON_OBJECT)) {
		return;
	}
	for (size_t i = 0; i < items->count; i++) {
		rename_rarity(items->items[i]);
	}
}

static void stats_v1_to_v2(JsonValue *stats) {
	if (stats == NULL || stats->type != JSON_OBJECT) {
		return;
	}
	set_default(stats, "itemsAutoEquipped", json_int(0));
	set_default(stats, "elitesKilled", json_int(0));
	set_default(stats, "potionsDropped", json_object());
	JsonValue *dropped = json_get(stats, "itemsDropped");
	const JsonValue *epic = json_get(dropped, REMOVED_RARITY);
	if (epic != NULL && epic->type == JSON_INT) {
		const JsonValue *legendary = json_get(dropped, REPLACEMENT_RARITY);
		int64_t total = epic->integer + (legendary != NULL && legendary->type == JSON_INT ? legendary->integer : 0);
		json_remove(dropped, REMOVED_RARITY);
		json_set(dropped, REPLACEMENT_RARITY, json_int(total));
		json_sort_keys(dropped);
	}
	rename_rarities(json_get(stats, "droppedItems"));
}

static void run_v1_to_v2(JsonValue *run) {
	if (run == NULL || run->type != JSON_OBJECT) {
		return;
	}
	set_default(json_get(run, "config"), "autoEquip", json_bool(false));
	set_default(run, "won", json_bool(false));
	JsonValue *monster = json_get(run, "monster");
	if (monster != NULL && monster->type == JSON_OBJECT) {
		const JsonValue *is_boss = json_get(monster, "isBoss");
		bool boss = is_boss != NULL && is_boss->type == JSON_BOOL && is_boss->boolean;
		set_default(monster, "enemyClass", json_string(boss ? "boss" : "normal"));
	}
	JsonValue *player = json_get(run, "player");
	rename_rarities(json_get(player, "bag"));
	rename_rarities(json_get(player, "equipment"));
	rename_rarities(json_get(run, "merchantStock"));
	stats_v1_to_v2(json_get(run, "stats"));
}

void migrate_save(JsonValue *document) {
	if (version_of(document) < 2) {
		run_v1_to_v2(json_get(document, "run"));
		json_set(document, "schemaVersion", json_int(2));
	}
}

void migrate_history(JsonValue *document) {
	if (version_of(document) < 2) {
		set_default(document, "won", json_bool(false));
		stats_v1_to_v2(json_get(document, "stats"));
		json_set(document, "schemaVersion", json_int(2));
	}
}

void migrate_profile(JsonValue *document) {
	if (version_of(document) < 2) {
		JsonValue *hall = json_get(document, "hallOfFame");
		if (hall != NULL && hall->type == JSON_ARRAY) {
			for (size_t i = 0; i < hall->count; i++) {
				set_default(hall->items[i], "won", json_bool(false));
			}
		}
		json_set(document, "schemaVersion", json_int(2));
	}
}

void migrate_settings(JsonValue *document) {
	if (version_of(document) < 2) {
		set_default(document, "autoEquip", json_bool(false));
		set_default(document, "battleSpeed", json_int(1));
		json_set(document, "schemaVersion", json_int(2));
	}
}
