#include "application/statistics.h"

#include <stdlib.h>
#include <string.h>

static void dealt(RunStatistics *stats, const Event *event) {
	int64_t damage = event_get_int(event, "damage");
	stats->damage_dealt += damage;
	if (damage > stats->highest_hit) {
		stats->highest_hit = damage;
	}
	if (event_get_bool(event, "crit")) {
		stats->crits++;
	}
}

// Battle counters; returns false when the event is not a battle event.
static bool record_combat(RunStatistics *stats, const Event *event) {
	if (event_is(event, "player_attacked")) {
		stats->normal_attacks++;
		dealt(stats, event);
	} else if (event_is(event, "spell_cast")) {
		counter_add(&stats->spells_cast, event_get_str(event, "spellId"), 1);
		dealt(stats, event);
	} else if (event_is(event, "spell_healed")) {
		counter_add(&stats->spells_cast, event_get_str(event, "spellId"), 1);
		stats->healing_done += event_get_int(event, "amount");
	} else if (event_is(event, "potion_used")) {
		counter_add(&stats->potions_used, event_get_str(event, "potionId"), 1);
		if (str_eq(event_get_str(event, "resource"), "hp")) {
			stats->healing_done += event_get_int(event, "amount");
		}
	} else if (event_is(event, "player_defended")) {
		stats->defends++;
	} else if (event_is(event, "monster_attacked")) {
		stats->damage_taken += event_get_int(event, "damage");
	} else if (event_is(event, "attack_dodged")) {
		stats->dodges++;
	} else if (event_is(event, "attack_parried")) {
		stats->parries++;
		stats->damage_dealt += event_get_int(event, "reflected");
	} else if (event_is(event, "monster_parried")) {
		stats->damage_taken += event_get_int(event, "reflected");
	} else if (event_is(event, "status_ticked")) {
		if (str_eq(event_get_str(event, "target"), "player")) {
			stats->damage_taken += event_get_int(event, "damage");
		} else {
			stats->damage_dealt += event_get_int(event, "damage");
		}
	} else if (event_is(event, "status_applied")) {
		if (str_eq(event_get_str(event, "target"), "monster")) {
			counter_add(&stats->statuses_applied, event_get_str(event, "status"), 1);
		}
	} else if (event_is(event, "monster_killed")) {
		counter_add(&stats->kills, event_get_str(event, "monsterId"), 1);
		if (event_get_bool(event, "isBoss")) {
			stats->bosses_killed++;
		}
		if (str_eq(event_get_str(event, "enemyClass"), "elite")) {
			stats->elites_killed++;
		}
	} else {
		return false;
	}
	return true;
}

static void record_loot(RunStatistics *stats, const Event *event, int64_t current_round) {
	if (event_is(event, "gold_looted")) {
		stats->gold_looted += event_get_int(event, "amount");
	} else if (event_is(event, "item_dropped")) {
		const char *rarity = event_get_str(event, "rarity");
		counter_add(&stats->items_dropped, rarity, 1);
		DroppedItem item = {.round = current_round};
		id_set(item.item_id, event_get_str(event, "itemId"));
		id_set(item.rarity, rarity);
		VEC_PUSH(stats->dropped_items, stats->dropped_count, stats->dropped_capacity, item);
	} else if (event_is(event, "potion_bought")) {
		counter_add(&stats->potions_bought, event_get_str(event, "potionId"), event_get_int(event, "quantity"));
		stats->gold_spent += event_get_int(event, "gold");
	} else if (event_is(event, "item_bought")) {
		stats->gold_spent += event_get_int(event, "gold");
	} else if (event_is(event, "potion_dropped")) {
		counter_add(&stats->potions_dropped, event_get_str(event, "potionId"), 1);
	} else if (event_is(event, "item_auto_equipped")) {
		stats->items_auto_equipped++;
	} else if (event_is(event, "item_sold") || event_is(event, "item_auto_sold")) {
		stats->items_sold++;
		stats->gold_earned += event_get_int(event, "gold");
	}
}

void statistics_record(RunStatistics *stats, const EventList *events, int64_t current_round) {
	for (size_t i = 0; i < events->count; i++) {
		if (!record_combat(stats, &events->items[i])) {
			record_loot(stats, &events->items[i], current_round);
		}
	}
}

void statistics_free(RunStatistics *stats) {
	counter_free(&stats->spells_cast);
	counter_free(&stats->potions_used);
	counter_free(&stats->potions_bought);
	counter_free(&stats->potions_dropped);
	counter_free(&stats->items_dropped);
	counter_free(&stats->kills);
	counter_free(&stats->statuses_applied);
	free(stats->dropped_items);
	stats->dropped_items = NULL;
	stats->dropped_count = 0;
	stats->dropped_capacity = 0;
}

RunStatistics statistics_clone(const RunStatistics *stats) {
	RunStatistics copy = *stats;
	copy.spells_cast = counter_clone(&stats->spells_cast);
	copy.potions_used = counter_clone(&stats->potions_used);
	copy.potions_bought = counter_clone(&stats->potions_bought);
	copy.potions_dropped = counter_clone(&stats->potions_dropped);
	copy.items_dropped = counter_clone(&stats->items_dropped);
	copy.kills = counter_clone(&stats->kills);
	copy.statuses_applied = counter_clone(&stats->statuses_applied);
	copy.dropped_items = NULL;
	copy.dropped_capacity = stats->dropped_count;
	if (stats->dropped_count > 0) {
		copy.dropped_items = xmalloc(stats->dropped_count * sizeof(DroppedItem));
		memcpy(copy.dropped_items, stats->dropped_items, stats->dropped_count * sizeof(DroppedItem));
	}
	return copy;
}

JsonValue *statistics_to_json(const RunStatistics *stats) {
	JsonValue *object = json_object();
	json_set(object, "damageDealt", json_int(stats->damage_dealt));
	json_set(object, "damageTaken", json_int(stats->damage_taken));
	json_set(object, "healingDone", json_int(stats->healing_done));
	json_set(object, "highestHit", json_int(stats->highest_hit));
	json_set(object, "normalAttacks", json_int(stats->normal_attacks));
	json_set(object, "crits", json_int(stats->crits));
	json_set(object, "dodges", json_int(stats->dodges));
	json_set(object, "parries", json_int(stats->parries));
	json_set(object, "defends", json_int(stats->defends));
	json_set(object, "goldLooted", json_int(stats->gold_looted));
	json_set(object, "goldSpent", json_int(stats->gold_spent));
	json_set(object, "goldEarned", json_int(stats->gold_earned));
	json_set(object, "itemsSold", json_int(stats->items_sold));
	json_set(object, "itemsAutoEquipped", json_int(stats->items_auto_equipped));
	json_set(object, "bossesKilled", json_int(stats->bosses_killed));
	json_set(object, "elitesKilled", json_int(stats->elites_killed));
	json_set(object, "spellsCast", counter_to_json(&stats->spells_cast));
	json_set(object, "potionsUsed", counter_to_json(&stats->potions_used));
	json_set(object, "potionsBought", counter_to_json(&stats->potions_bought));
	json_set(object, "potionsDropped", counter_to_json(&stats->potions_dropped));
	json_set(object, "itemsDropped", counter_to_json(&stats->items_dropped));
	json_set(object, "kills", counter_to_json(&stats->kills));
	json_set(object, "statusesApplied", counter_to_json(&stats->statuses_applied));
	JsonValue *dropped = json_array();
	for (size_t i = 0; i < stats->dropped_count; i++) {
		JsonValue *item = json_object();
		json_set(item, "itemId", json_string(stats->dropped_items[i].item_id));
		json_set(item, "rarity", json_string(stats->dropped_items[i].rarity));
		json_set(item, "round", json_int(stats->dropped_items[i].round));
		json_push(dropped, item);
	}
	json_set(object, "droppedItems", dropped);
	return object;
}

void statistics_from_json(const JsonValue *raw, RunStatistics *out, JsonError *error) {
	memset(out, 0, sizeof(*out));
	out->damage_dealt = json_read_int(raw, "damageDealt", error);
	out->damage_taken = json_read_int(raw, "damageTaken", error);
	out->healing_done = json_read_int(raw, "healingDone", error);
	out->highest_hit = json_read_int(raw, "highestHit", error);
	out->normal_attacks = json_read_int(raw, "normalAttacks", error);
	out->crits = json_read_int(raw, "crits", error);
	out->dodges = json_read_int(raw, "dodges", error);
	out->parries = json_read_int(raw, "parries", error);
	out->defends = json_read_int(raw, "defends", error);
	out->gold_looted = json_read_int(raw, "goldLooted", error);
	out->gold_spent = json_read_int(raw, "goldSpent", error);
	out->gold_earned = json_read_int(raw, "goldEarned", error);
	out->items_sold = json_read_int(raw, "itemsSold", error);
	out->items_auto_equipped = json_read_int(raw, "itemsAutoEquipped", error);
	out->bosses_killed = json_read_int(raw, "bossesKilled", error);
	out->elites_killed = json_read_int(raw, "elitesKilled", error);
	counter_from_json(json_read_object(raw, "spellsCast", error), &out->spells_cast, error);
	counter_from_json(json_read_object(raw, "potionsUsed", error), &out->potions_used, error);
	counter_from_json(json_read_object(raw, "potionsBought", error), &out->potions_bought, error);
	counter_from_json(json_read_object(raw, "potionsDropped", error), &out->potions_dropped, error);
	counter_from_json(json_read_object(raw, "itemsDropped", error), &out->items_dropped, error);
	counter_from_json(json_read_object(raw, "kills", error), &out->kills, error);
	counter_from_json(json_read_object(raw, "statusesApplied", error), &out->statuses_applied, error);
	const JsonValue *dropped = json_read_array(raw, "droppedItems", error);
	for (size_t i = 0; i < dropped->count; i++) {
		DroppedItem item = {0};
		json_read_text(dropped->items[i], "itemId", item.item_id, ID_SIZE, error);
		json_read_text(dropped->items[i], "rarity", item.rarity, ID_SIZE, error);
		item.round = json_read_int(dropped->items[i], "round", error);
		VEC_PUSH(out->dropped_items, out->dropped_count, out->dropped_capacity, item);
	}
}
