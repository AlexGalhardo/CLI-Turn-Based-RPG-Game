#include "domain/entities.h"

#include <stdlib.h>
#include <string.h>

// ── Counter ─────────────────────────────────────────────────────────────────

// Index of `key`, or of the position where it would be inserted to keep the entries sorted.
static int counter_position(const Counter *counter, const char *key, bool *found) {
	int low = 0;
	int high = counter->count;
	while (low < high) {
		int middle = (low + high) / 2;
		int order = strcmp(counter->entries[middle].key, key);
		if (order == 0) {
			*found = true;
			return middle;
		}
		if (order < 0) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}
	*found = false;
	return low;
}

int64_t counter_get(const Counter *counter, const char *key) {
	bool found;
	int index = counter_position(counter, key, &found);
	return found ? counter->entries[index].value : 0;
}

bool counter_has(const Counter *counter, const char *key) {
	bool found;
	counter_position(counter, key, &found);
	return found;
}

void counter_set(Counter *counter, const char *key, int64_t value) {
	bool found;
	int index = counter_position(counter, key, &found);
	if (found) {
		counter->entries[index].value = value;
		return;
	}
	if (counter->count == counter->capacity) {
		counter->capacity = counter->capacity == 0 ? 8 : counter->capacity * 2;
		counter->entries = xrealloc(counter->entries, (size_t)counter->capacity * sizeof(CounterEntry));
	}
	memmove(&counter->entries[index + 1], &counter->entries[index],
	    (size_t)(counter->count - index) * sizeof(CounterEntry));
	id_set(counter->entries[index].key, key);
	counter->entries[index].value = value;
	counter->count++;
}

void counter_add(Counter *counter, const char *key, int64_t delta) {
	counter_set(counter, key, counter_get(counter, key) + delta);
}

int64_t counter_total(const Counter *counter) {
	int64_t total = 0;
	for (int i = 0; i < counter->count; i++) {
		total += counter->entries[i].value;
	}
	return total;
}

void counter_free(Counter *counter) {
	free(counter->entries);
	counter->entries = NULL;
	counter->count = 0;
	counter->capacity = 0;
}

Counter counter_clone(const Counter *counter) {
	Counter copy = {0};
	if (counter->count > 0) {
		copy.entries = xmalloc((size_t)counter->count * sizeof(CounterEntry));
		memcpy(copy.entries, counter->entries, (size_t)counter->count * sizeof(CounterEntry));
		copy.count = counter->count;
		copy.capacity = counter->count;
	}
	return copy;
}

JsonValue *counter_to_json(const Counter *counter) {
	JsonValue *object = json_object();
	for (int i = 0; i < counter->count; i++) {
		json_set(object, counter->entries[i].key, json_int(counter->entries[i].value));
	}
	return object;
}

void counter_from_json(const JsonValue *raw, Counter *out, JsonError *error) {
	memset(out, 0, sizeof(*out));
	if (raw == NULL || raw->type != JSON_OBJECT) {
		json_error_set(error, "expected an object of counters");
		return;
	}
	for (size_t i = 0; i < raw->count; i++) {
		if (strlen(raw->keys[i]) >= ID_SIZE) {
			json_error_set(error, "id too long: %.40s", raw->keys[i]);
			return;
		}
		counter_set(out, raw->keys[i], json_as_int(raw->items[i], error));
	}
}

// ── statuses ────────────────────────────────────────────────────────────────

void status_list_remove(StatusList *list, int index) {
	for (int i = index + 1; i < list->count; i++) {
		list->items[i - 1] = list->items[i];
	}
	list->count--;
}

void status_list_push(StatusList *list, ActiveStatus status) {
	if (list->count == MAX_STATUSES) {
		fatal("too many active statuses");
	}
	list->items[list->count++] = status;
}

static JsonValue *statuses_to_json(const StatusList *list) {
	JsonValue *array = json_array();
	for (int i = 0; i < list->count; i++) {
		JsonValue *status = json_object();
		json_set(status, "statusId", json_string(list->items[i].status_id));
		json_set(status, "turns", json_int(list->items[i].turns));
		json_set(status, "perTurn", json_int(list->items[i].per_turn));
		json_push(array, status);
	}
	return array;
}

static void statuses_from_json(const JsonValue *raw, StatusList *out, JsonError *error) {
	out->count = 0;
	if (raw->count > MAX_STATUSES) {
		json_error_set(error, "too many statuses");
		return;
	}
	for (size_t i = 0; i < raw->count; i++) {
		ActiveStatus *status = &out->items[out->count++];
		json_read_text(raw->items[i], "statusId", status->status_id, ID_SIZE, error);
		status->turns = json_read_int(raw->items[i], "turns", error);
		status->per_turn = json_read_int(raw->items[i], "perTurn", error);
	}
}

// ── items ───────────────────────────────────────────────────────────────────

JsonValue *item_instance_to_json(const ItemInstance *item) {
	JsonValue *object = json_object();
	json_set(object, "uid", json_int(item->uid));
	json_set(object, "itemId", json_string(item->item_id));
	json_set(object, "rarity", json_string(item->rarity));
	json_set(object, "tier", json_int(item->tier));
	JsonValue *affixes = json_array();
	for (int i = 0; i < item->affix_count; i++) {
		JsonValue *affix = json_object();
		json_set(affix, "stat", json_string(stat_name(item->affixes[i].stat)));
		json_set(affix, "value", json_int(item->affixes[i].value));
		json_push(affixes, affix);
	}
	json_set(object, "affixes", affixes);
	return object;
}

void item_instance_from_json(const JsonValue *raw, ItemInstance *out, JsonError *error) {
	memset(out, 0, sizeof(*out));
	out->uid = json_read_int(raw, "uid", error);
	json_read_text(raw, "itemId", out->item_id, ID_SIZE, error);
	json_read_text(raw, "rarity", out->rarity, ID_SIZE, error);
	out->tier = json_read_int(raw, "tier", error);
	const JsonValue *affixes = json_read_array(raw, "affixes", error);
	if (affixes->count > MAX_AFFIXES) {
		json_error_set(error, "too many affixes");
		return;
	}
	for (size_t i = 0; i < affixes->count; i++) {
		AffixRoll *affix = &out->affixes[out->affix_count++];
		const char *stat = json_read_str(affixes->items[i], "stat", error);
		if (!error->failed && !stat_parse(stat, &affix->stat)) {
			json_error_set(error, "unknown stat: %.40s", stat);
		}
		affix->value = json_read_int(affixes->items[i], "value", error);
	}
}

// ── player ──────────────────────────────────────────────────────────────────

const ItemInstance *player_equipped(const Player *player, Slot slot) {
	return player->equipped[slot] ? &player->equipment[slot] : NULL;
}

int player_bag_index(const Player *player, int64_t uid) {
	for (int i = 0; i < player->bag_count; i++) {
		if (player->bag[i].uid == uid) {
			return i;
		}
	}
	return -1;
}

void player_bag_remove(Player *player, int index) {
	for (int i = index + 1; i < player->bag_count; i++) {
		player->bag[i - 1] = player->bag[i];
	}
	player->bag_count--;
}

void player_bag_push(Player *player, const ItemInstance *item) {
	if (player->bag_count == MAX_BAG) {
		fatal("bag overflow");
	}
	player->bag[player->bag_count++] = *item;
}

void player_free(Player *player) {
	counter_free(&player->potions);
	counter_free(&player->spell_uses);
}

Player player_clone(const Player *player) {
	Player copy = *player;
	copy.potions = counter_clone(&player->potions);
	copy.spell_uses = counter_clone(&player->spell_uses);
	return copy;
}

JsonValue *player_to_json(const Player *player) {
	JsonValue *object = json_object();
	json_set(object, "name", json_string(player->name));
	json_set(object, "vocationId", json_string(player->vocation_id));
	json_set(object, "hp", json_int(player->hp));
	json_set(object, "mp", json_int(player->mp));
	json_set(object, "gold", json_int(player->gold));
	json_set(object, "level", json_int(player->level));
	json_set(object, "xp", json_int(player->xp));
	json_set(object, "magicLevel", json_int(player->magic_level));
	json_set(object, "manaSpent", json_int(player->mana_spent));
	json_set(object, "potions", counter_to_json(&player->potions));
	JsonValue *equipment = json_object();
	for (int i = 0; i < SLOT_COUNT; i++) {
		Slot slot = SLOTS_BY_NAME[i];
		if (player->equipped[slot]) {
			json_set(equipment, slot_name(slot), item_instance_to_json(&player->equipment[slot]));
		}
	}
	json_set(object, "equipment", equipment);
	JsonValue *bag = json_array();
	for (int i = 0; i < player->bag_count; i++) {
		json_push(bag, item_instance_to_json(&player->bag[i]));
	}
	json_set(object, "bag", bag);
	json_set(object, "spellUses", counter_to_json(&player->spell_uses));
	json_set(object, "statuses", statuses_to_json(&player->statuses));
	json_set(object, "stunCooldown", json_int(player->stun_cooldown));
	json_set(object, "defending", json_bool(player->defending));
	return object;
}

void player_from_json(const JsonValue *raw, Player *out, JsonError *error) {
	memset(out, 0, sizeof(*out));
	json_read_text(raw, "name", out->name, NAME_SIZE, error);
	json_read_text(raw, "vocationId", out->vocation_id, ID_SIZE, error);
	out->hp = json_read_int(raw, "hp", error);
	out->mp = json_read_int(raw, "mp", error);
	out->gold = json_read_int(raw, "gold", error);
	out->level = json_read_int(raw, "level", error);
	out->xp = json_read_int(raw, "xp", error);
	out->magic_level = json_read_int(raw, "magicLevel", error);
	out->mana_spent = json_read_int(raw, "manaSpent", error);
	counter_from_json(json_read_object(raw, "potions", error), &out->potions, error);
	const JsonValue *equipment = json_read_object(raw, "equipment", error);
	for (size_t i = 0; i < equipment->count; i++) {
		Slot slot;
		if (!slot_parse(equipment->keys[i], &slot)) {
			json_error_set(error, "unknown slot: %.40s", equipment->keys[i]);
			break;
		}
		out->equipped[slot] = true;
		item_instance_from_json(equipment->items[i], &out->equipment[slot], error);
	}
	const JsonValue *bag = json_read_array(raw, "bag", error);
	if (bag->count > MAX_BAG) {
		json_error_set(error, "bag too large");
	} else {
		for (size_t i = 0; i < bag->count; i++) {
			item_instance_from_json(bag->items[i], &out->bag[out->bag_count++], error);
		}
	}
	counter_from_json(json_read_object(raw, "spellUses", error), &out->spell_uses, error);
	statuses_from_json(json_read_array(raw, "statuses", error), &out->statuses, error);
	out->stun_cooldown = json_read_int(raw, "stunCooldown", error);
	out->defending = json_read_bool(raw, "defending", error);
}

// ── monster ─────────────────────────────────────────────────────────────────

const MonsterAttack *monster_attack(const MonsterInstance *monster, const char *attack_id) {
	for (int i = 0; i < monster->attack_count; i++) {
		if (strcmp(monster->attacks[i].id, attack_id) == 0) {
			return &monster->attacks[i];
		}
	}
	fatal("unknown attack id: %s", attack_id);
}

static JsonValue *attack_to_json(const MonsterAttack *attack) {
	JsonValue *object = json_object();
	json_set(object, "id", json_string(attack->id));
	json_set(object, "element", json_string(element_name(attack->element)));
	json_set(object, "min", json_int(attack->min));
	json_set(object, "max", json_int(attack->max));
	json_set(object, "weight", json_int(attack->weight));
	if (attack->has_status) {
		JsonValue *status = json_object();
		json_set(status, "id", json_string(attack->status.status));
		json_set(status, "chance", json_int(attack->status.chance));
		json_set(status, "damagePct", json_int(attack->status.damage_pct));
		json_set(object, "status", status);
	}
	return object;
}

void monster_attack_from_json(const JsonValue *raw, MonsterAttack *out, JsonError *error) {
	memset(out, 0, sizeof(*out));
	json_read_text(raw, "id", out->id, ID_SIZE, error);
	const char *element = json_read_str(raw, "element", error);
	if (!error->failed && !element_parse(element, &out->element)) {
		json_error_set(error, "unknown element: %.40s", element);
	}
	out->min = json_read_int(raw, "min", error);
	out->max = json_read_int(raw, "max", error);
	out->weight = json_read_int(raw, "weight", error);
	const JsonValue *status = json_get(raw, "status");
	if (!json_is_null(status)) {
		out->has_status = true;
		json_read_text(status, "id", out->status.status, ID_SIZE, error);
		out->status.chance = json_read_int(status, "chance", error);
		out->status.damage_pct = json_read_int(status, "damagePct", error);
	}
}

JsonValue *monster_instance_to_json(const MonsterInstance *monster) {
	JsonValue *object = json_object();
	json_set(object, "creatureId", json_string(monster->creature_id));
	json_set(object, "isBoss", json_bool(monster->is_boss));
	json_set(object, "enemyClass", json_string(enemy_class_name(monster->enemy_class)));
	json_set(object, "hp", json_int(monster->hp));
	json_set(object, "maxHp", json_int(monster->max_hp));
	json_set(object, "xp", json_int(monster->xp));
	json_set(object, "goldMin", json_int(monster->gold_min));
	json_set(object, "goldMax", json_int(monster->gold_max));
	JsonValue *attacks = json_array();
	for (int i = 0; i < monster->attack_count; i++) {
		json_push(attacks, attack_to_json(&monster->attacks[i]));
	}
	json_set(object, "attacks", attacks);
	json_set(object, "statuses", statuses_to_json(&monster->statuses));
	json_set(object, "stunCooldown", json_int(monster->stun_cooldown));
	json_set(object, "bossActions", json_int(monster->boss_actions));
	return object;
}

void monster_instance_from_json(const JsonValue *raw, MonsterInstance *out, JsonError *error) {
	memset(out, 0, sizeof(*out));
	json_read_text(raw, "creatureId", out->creature_id, ID_SIZE, error);
	out->is_boss = json_read_bool(raw, "isBoss", error);
	const char *enemy_class = json_read_str(raw, "enemyClass", error);
	if (!error->failed && !enemy_class_parse(enemy_class, &out->enemy_class)) {
		json_error_set(error, "unknown enemy class: %.40s", enemy_class);
	}
	out->hp = json_read_int(raw, "hp", error);
	out->max_hp = json_read_int(raw, "maxHp", error);
	out->xp = json_read_int(raw, "xp", error);
	out->gold_min = json_read_int(raw, "goldMin", error);
	out->gold_max = json_read_int(raw, "goldMax", error);
	const JsonValue *attacks = json_read_array(raw, "attacks", error);
	if (attacks->count > MAX_ATTACKS) {
		json_error_set(error, "too many attacks");
	} else {
		for (size_t i = 0; i < attacks->count; i++) {
			monster_attack_from_json(attacks->items[i], &out->attacks[out->attack_count++], error);
		}
	}
	statuses_from_json(json_read_array(raw, "statuses", error), &out->statuses, error);
	out->stun_cooldown = json_read_int(raw, "stunCooldown", error);
	out->boss_actions = json_read_int(raw, "bossActions", error);
}
