#include "infrastructure/data_loader.h"

#include "assets/shared_files.h"
#include "domain/entities.h"
#include "domain/json_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── small readers ───────────────────────────────────────────────────────────

static Element read_element(const JsonValue *object, const char *key, JsonError *error) {
	Element element = ELEMENT_PHYSICAL;
	const char *name = json_read_str(object, key, error);
	if (!error->failed && !element_parse(name, &element)) {
		json_error_set(error, "unknown element: %.40s", name);
	}
	return element;
}

static Stat stat_from_key(const char *name, JsonError *error) {
	Stat stat = STAT_ATTACK;
	if (!stat_parse(name, &stat)) {
		json_error_set(error, "unknown stat: %.40s", name);
	}
	return stat;
}

// Reads a list of ids into a fixed array, failing when it has more than `capacity` entries.
static int read_ids(const JsonValue *object, const char *key, Id *out, int capacity, JsonError *error) {
	const JsonValue *list = json_read_array(object, key, error);
	if (list->count > (size_t)capacity) {
		json_error_set(error, "'%s' has more than %d entries", key, capacity);
		return 0;
	}
	for (size_t i = 0; i < list->count; i++) {
		json_copy_text(list->items[i], out[i], ID_SIZE, error);
	}
	return (int)list->count;
}

static void read_attack(const JsonValue *raw, MonsterAttack *out, JsonError *error) {
	memset(out, 0, sizeof(*out));
	json_read_text(raw, "id", out->id, ID_SIZE, error);
	out->element = read_element(raw, "element", error);
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

static void read_creature(const JsonValue *raw, bool is_boss, MonsterDef *out, JsonError *error) {
	json_read_text(raw, "id", out->id, ID_SIZE, error);
	json_read_text(raw, "name", out->name, NAME_SIZE, error);
	out->tier = json_read_int(raw, "tier", error);
	json_read_text(raw, "family", out->family, ID_SIZE, error);
	out->hp = json_read_int(raw, "hp", error);
	out->xp = json_read_int(raw, "xp", error);
	const JsonValue *gold = json_read_object(raw, "gold", error);
	out->gold_min = json_read_int(gold, "min", error);
	out->gold_max = json_read_int(gold, "max", error);
	const JsonValue *attacks = json_read_array(raw, "attacks", error);
	if (attacks->count > MAX_ATTACKS) {
		json_error_set(error, "monster %s has more than %d attacks", out->id, MAX_ATTACKS);
		return;
	}
	for (size_t i = 0; i < attacks->count; i++) {
		read_attack(attacks->items[i], &out->attacks[out->attack_count++], error);
	}
	for (int element = 0; element < ELEMENT_COUNT; element++) {
		out->resistances[element] = DEFAULT_RESISTANCE;
	}
	const JsonValue *resistances = json_read_object(raw, "resistances", error);
	for (size_t i = 0; i < resistances->count; i++) {
		Element element;
		if (!element_parse(resistances->keys[i], &element)) {
			json_error_set(error, "unknown element: %.40s", resistances->keys[i]);
			return;
		}
		out->resistances[element] = json_as_int(resistances->items[i], error);
	}
	out->is_boss = is_boss;
	const JsonValue *charge = json_get(raw, "chargeAttack");
	if (!json_is_null(charge)) {
		out->has_charge_attack = true;
		json_copy_text(charge, out->charge_attack, ID_SIZE, error);
	}
}

static void read_spell(const JsonValue *raw, SpellDef *out, JsonError *error) {
	json_read_text(raw, "id", out->id, ID_SIZE, error);
	json_read_text(raw, "name", out->name, NAME_SIZE, error);
	json_read_text(raw, "words", out->words, NAME_SIZE, error);
	const char *kind = json_read_str(raw, "kind", error);
	if (!error->failed && !spell_kind_parse(kind, &out->kind)) {
		json_error_set(error, "unknown spell kind: %.40s", kind);
	}
	out->element = read_element(raw, "element", error);
	out->mana = json_read_int(raw, "mana", error);
	out->min = json_read_int(raw, "min", error);
	out->max = json_read_int(raw, "max", error);
	out->per_level = json_read_int(raw, "perLevel", error);
	out->per_magic_level = json_read_int(raw, "perMagicLevel", error);
	const JsonValue *bonus = json_read_object(raw, "level3Bonus", error);
	const JsonValue *status = json_get(bonus, "status");
	if (!json_is_null(status)) {
		out->level3_bonus.has_status = true;
		json_copy_text(status, out->level3_bonus.status, ID_SIZE, error);
	}
	if (json_has(bonus, "chance")) {
		out->level3_bonus.chance = json_read_int(bonus, "chance", error);
	}
	if (json_has(bonus, "cleanse")) {
		out->level3_bonus.cleanse = json_read_bool(bonus, "cleanse", error);
	}
}

static void read_vocation(const JsonValue *raw, VocationDef *out, JsonError *error) {
	json_read_text(raw, "id", out->id, ID_SIZE, error);
	json_read_text(raw, "name", out->name, NAME_SIZE, error);
	out->start_hp = json_read_int(raw, "startHp", error);
	out->start_mp = json_read_int(raw, "startMp", error);
	out->hp_per_level = json_read_int(raw, "hpPerLevel", error);
	out->mp_per_level = json_read_int(raw, "mpPerLevel", error);
	out->hp_regen = json_read_int(raw, "hpRegen", error);
	out->mp_regen = json_read_int(raw, "mpRegen", error);
	out->melee_min = json_read_int(raw, "meleeMin", error);
	out->melee_max = json_read_int(raw, "meleeMax", error);
	out->melee_per_level = json_read_int(raw, "meleePerLevel", error);
	out->weapon_type_count = read_ids(raw, "weaponTypes", out->weapon_types, MAX_TYPES, error);
	out->shield_type_count = read_ids(raw, "shieldTypes", out->shield_types, MAX_TYPES, error);
	json_read_text(raw, "starterWeapon", out->starter_weapon, ID_SIZE, error);
	out->spell_count = read_ids(raw, "spells", out->spells, MAX_VOCATION_SPELLS, error);
}

static void read_potion(const JsonValue *raw, PotionDef *out, JsonError *error) {
	json_read_text(raw, "id", out->id, ID_SIZE, error);
	json_read_text(raw, "name", out->name, NAME_SIZE, error);
	const char *resource = json_read_str(raw, "resource", error);
	if (!error->failed && !resource_parse(resource, &out->resource)) {
		json_error_set(error, "unknown resource: %.40s", resource);
	}
	out->min = json_read_int(raw, "min", error);
	out->max = json_read_int(raw, "max", error);
	out->price = json_read_int(raw, "price", error);
	out->unlock_round = json_read_int(raw, "unlockRound", error);
}

static void read_status(const JsonValue *raw, StatusDef *out, JsonError *error) {
	json_read_text(raw, "id", out->id, ID_SIZE, error);
	const char *kind = json_read_str(raw, "kind", error);
	if (!error->failed && !status_kind_parse(kind, &out->kind)) {
		json_error_set(error, "unknown status kind: %.40s", kind);
	}
	out->element = read_element(raw, "element", error);
	out->turns = json_read_int(raw, "turns", error);
}

static void read_item(const JsonValue *raw, ItemDef *out, JsonError *error) {
	json_read_text(raw, "id", out->id, ID_SIZE, error);
	json_read_text(raw, "name", out->name, NAME_SIZE, error);
	const char *slot = json_read_str(raw, "slot", error);
	if (!error->failed && !slot_parse(slot, &out->slot)) {
		json_error_set(error, "unknown slot: %.40s", slot);
	}
	json_read_text(raw, "type", out->type, ID_SIZE, error);
	out->tier = json_read_int(raw, "tier", error);
	if (!json_is_null(json_get(raw, "element"))) {
		out->has_element = true;
		out->element = read_element(raw, "element", error);
	}
	const JsonValue *stats = json_read_object(raw, "stats", error);
	for (size_t i = 0; i < stats->count; i++) {
		Stat stat = stat_from_key(stats->keys[i], error);
		out->has_stat[stat] = true;
		out->stats[stat] = json_as_int(stats->items[i], error);
	}
	out->value = json_read_int(raw, "value", error);
}

static void read_affix(const JsonValue *raw, AffixDef *out, JsonError *error) {
	json_read_text(raw, "id", out->id, ID_SIZE, error);
	out->stat = stat_from_key(json_read_str(raw, "stat", error), error);
	out->min = json_read_int(raw, "min", error);
	out->max = json_read_int(raw, "max", error);
	out->per_tier = json_read_int(raw, "perTier", error);
	const JsonValue *slots = json_read_array(raw, "slots", error);
	for (size_t i = 0; i < slots->count; i++) {
		Slot slot;
		const char *name = json_as_str(slots->items[i], error);
		if (!slot_parse(name, &slot)) {
			json_error_set(error, "unknown slot: %.40s", name);
			return;
		}
		out->slots[slot] = true;
	}
}

static void read_achievement(const JsonValue *raw, AchievementDef *out, JsonError *error) {
	json_read_text(raw, "id", out->id, ID_SIZE, error);
	json_read_text(raw, "type", out->type, ID_SIZE, error);
	out->value = json_read_int(raw, "value", error);
}

// ── balance ─────────────────────────────────────────────────────────────────

// Reads a `{rarityId: weight}` table into weights parallel to `balance->rarities`.
static void read_weights(const JsonValue *raw, const Balance *balance, RarityWeights *out, JsonError *error) {
	memset(out, 0, sizeof(*out));
	for (int i = 0; i < balance->rarity_count; i++) {
		const JsonValue *weight = json_get(raw, balance->rarities[i].id);
		if (weight != NULL) {
			out->weights[i] = json_as_int(weight, error);
		}
	}
}

static void read_enemy_class(const JsonValue *raw, const Balance *balance, EnemyClassDef *out, JsonError *error) {
	out->stat_pct = json_read_int(raw, "statPct", error);
	out->reward_pct = json_read_int(raw, "rewardPct", error);
	out->dodge = json_read_int(raw, "dodge", error);
	out->parry = json_read_int(raw, "parry", error);
	out->crit = json_read_int(raw, "crit", error);
	out->heal = json_read_int(raw, "heal", error);
	out->drop_chance_pct = json_read_int(raw, "dropChancePct", error);
	out->drops = json_read_int(raw, "drops", error);
	out->potion_drop_pct = json_read_int(raw, "potionDropPct", error);
	read_weights(json_read_object(raw, "rarityWeights", error), balance, &out->rarity_weights, error);
}

// Checks that a JSON list fits a fixed array and returns its length (0 after a failure).
static int bounded(const JsonValue *list, int capacity, const char *what, JsonError *error) {
	if (list->count > (size_t)capacity) {
		json_error_set(error, "'%s' has more than %d entries", what, capacity);
		return 0;
	}
	return (int)list->count;
}

static void read_balance(const JsonValue *raw, Balance *out, JsonError *error) {
	memset(out, 0, sizeof(*out));
	out->rounds_per_tier = json_read_int(raw, "roundsPerTier", error);
	out->cycle_stat_pct = json_read_int(raw, "cycleStatPct", error);
	out->cycle_reward_pct = json_read_int(raw, "cycleRewardPct", error);
	out->position_pct = json_read_int(raw, "positionPct", error);
	out->final_round = json_read_int(raw, "finalRound", error);
	out->elite_chance_pct = json_read_int(raw, "eliteChancePct", error);

	const JsonValue *difficulties = json_read_array(raw, "difficulties", error);
	out->difficulty_count = bounded(difficulties, MAX_DIFFICULTIES, "difficulties", error);
	for (int i = 0; i < out->difficulty_count; i++) {
		DifficultyDef *difficulty = &out->difficulties[i];
		json_read_text(difficulties->items[i], "id", difficulty->id, ID_SIZE, error);
		difficulty->hp_pct = json_read_int(difficulties->items[i], "hpPct", error);
		difficulty->damage_pct = json_read_int(difficulties->items[i], "damagePct", error);
		difficulty->gold_pct = json_read_int(difficulties->items[i], "goldPct", error);
		difficulty->xp_pct = json_read_int(difficulties->items[i], "xpPct", error);
	}

	// Rarities come first: the rarity tables below are stored parallel to them.
	const JsonValue *rarities = json_read_array(raw, "rarities", error);
	out->rarity_count = bounded(rarities, MAX_RARITIES, "rarities", error);
	for (int i = 0; i < out->rarity_count; i++) {
		RarityDef *rarity = &out->rarities[i];
		json_read_text(rarities->items[i], "id", rarity->id, ID_SIZE, error);
		rarity->stat_pct = json_read_int(rarities->items[i], "statPct", error);
		rarity->value_pct = json_read_int(rarities->items[i], "valuePct", error);
		rarity->affix_min = json_read_int(rarities->items[i], "affixMin", error);
		rarity->affix_max = json_read_int(rarities->items[i], "affixMax", error);
		if (rarity->affix_max > MAX_AFFIXES) {
			json_error_set(error, "rarity %s allows more than %d affixes", rarity->id, MAX_AFFIXES);
		}
	}

	const JsonValue *classes = json_read_object(raw, "enemyClasses", error);
	for (int i = 0; i < ENEMY_CLASS_COUNT; i++) {
		out->enemy_classes[i].id = (EnemyClass)i;
		const JsonValue *row = json_read_object(classes, enemy_class_name((EnemyClass)i), error);
		read_enemy_class(row, out, &out->enemy_classes[i], error);
	}

	out->crit_multiplier_pct = json_read_int(raw, "critMultiplierPct", error);
	out->defend_damage_pct = json_read_int(raw, "defendDamagePct", error);
	out->parry_reflect_pct = json_read_int(raw, "parryReflectPct", error);
	out->monster_heal_pct = json_read_int(raw, "monsterHealPct", error);
	out->boss_telegraph_every = json_read_int(raw, "bossTelegraphEvery", error);
	out->boss_charge_damage_pct = json_read_int(raw, "bossChargeDamagePct", error);

	const JsonValue *caps = json_read_object(raw, "caps", error);
	out->caps.crit_chance = json_read_int(caps, "critChance", error);
	out->caps.dodge = json_read_int(caps, "dodge", error);
	out->caps.parry = json_read_int(caps, "parry", error);
	out->caps.leech = json_read_int(caps, "leech", error);
	out->caps.protection = json_read_int(caps, "protection", error);

	const JsonValue *magic = json_read_object(raw, "magicLevel", error);
	out->magic_level_base = json_read_int(magic, "base", error);
	out->magic_level_growth_pct = json_read_int(magic, "growthPct", error);

	const JsonValue *levels = json_read_array(raw, "spellLevels", error);
	out->spell_level_count = bounded(levels, MAX_SPELL_LEVELS, "spellLevels", error);
	for (int i = 0; i < out->spell_level_count; i++) {
		SpellLevelDef *level = &out->spell_levels[i];
		level->level = json_read_int(levels->items[i], "level", error);
		level->uses = json_read_int(levels->items[i], "uses", error);
		level->effect_pct = json_read_int(levels->items[i], "effectPct", error);
		level->mana_pct = json_read_int(levels->items[i], "manaPct", error);
	}
	if (!error->failed && out->spell_level_count == 0) {
		json_error_set(error, "'spellLevels' is empty");
	}

	out->starting_gold = json_read_int(raw, "startingGold", error);
	const JsonValue *potions = json_read_array(raw, "startingPotions", error);
	out->starting_potion_count = bounded(potions, MAX_STARTING_POTIONS, "startingPotions", error);
	for (int i = 0; i < out->starting_potion_count; i++) {
		json_read_text(potions->items[i], "potionId", out->starting_potions[i].potion_id, ID_SIZE, error);
		out->starting_potions[i].quantity = json_read_int(potions->items[i], "quantity", error);
	}

	out->bag_capacity = json_read_int(raw, "bagCapacity", error);
	if (out->bag_capacity > MAX_BAG) {
		json_error_set(error, "'bagCapacity' is larger than %d", MAX_BAG);
	}
	out->item_level_per_tier = json_read_int(raw, "itemLevelPerTier", error);
	const JsonValue *score_weights = json_read_object(raw, "itemScoreWeights", error);
	for (size_t i = 0; i < score_weights->count; i++) {
		Stat stat = stat_from_key(score_weights->keys[i], error);
		out->item_score_weights[stat] = json_as_int(score_weights->items[i], error);
	}

	const JsonValue *tables = json_read_object(raw, "rarityWeights", error);
	read_weights(json_read_object(tables, "merchant", error), out, &out->merchant_rarity_weights, error);
	out->merchant_stock_size = json_read_int(raw, "merchantStockSize", error);
	if (out->merchant_stock_size > MAX_MERCHANT_STOCK) {
		json_error_set(error, "'merchantStockSize' is larger than %d", MAX_MERCHANT_STOCK);
	}
	out->merchant_markup_pct = json_read_int(raw, "merchantMarkupPct", error);
	out->spell_status_damage_pct = json_read_int(raw, "spellStatusDamagePct", error);

	const JsonValue *auto_battle = json_read_object(raw, "autoBattle", error);
	out->auto_battle.heal_below_pct = json_read_int(auto_battle, "healBelowPct", error);
	out->auto_battle.mana_below_pct = json_read_int(auto_battle, "manaBelowPct", error);
	out->auto_battle.emergency_heal_below_pct = json_read_int(auto_battle, "emergencyHealBelowPct", error);
	const JsonValue *modes = json_read_object(auto_battle, "modes", error);
	out->auto_battle.mode_count = bounded(modes, MAX_AUTO_BATTLE_MODES, "autoBattle.modes", error);
	for (int i = 0; i < out->auto_battle.mode_count; i++) {
		AutoBattleModeDef *mode = &out->auto_battle.modes[i];
		if (strlen(modes->keys[i]) >= ID_SIZE) {
			json_error_set(error, "auto-battle mode id too long");
			break;
		}
		id_set(mode->id, modes->keys[i]);
		json_read_text(modes->items[i], "offense", mode->offense, ID_SIZE, error);
		mode->support_every = json_read_int(modes->items[i], "supportEvery", error);
	}
}

// ── files ───────────────────────────────────────────────────────────────────

typedef struct {
	DataSource source;
	void *context;
	JsonError *error;
} Loader;

// Parses data/<name>.json; returns NULL (with the error set) when it is missing or not a JSON object.
static JsonValue *read_document(Loader *loader, const char *name) {
	char path[64];
	snprintf(path, sizeof(path), "data/%s.json", name);
	const char *text = loader->source(path, loader->context);
	if (text == NULL) {
		json_error_set(loader->error, "%s: file not found", path);
		return NULL;
	}
	JsonError parse_error = {0};
	JsonValue *document = json_parse(text, &parse_error);
	if (document == NULL) {
		json_error_set(loader->error, "%s: %s", path, parse_error.message);
		return NULL;
	}
	if (document->type != JSON_OBJECT) {
		json_error_set(loader->error, "%s: expected object", path);
		json_free(document);
		return NULL;
	}
	return document;
}

// Loads `data/<name>.json` and fills a heap array with one definition per entry of its `<name>` list.
#define LOAD_LIST(loader, name, Type, out_items, out_count, read_call)                                                 \
	do {                                                                                                               \
		JsonValue *document = read_document((loader), (name));                                                         \
		if (document != NULL) {                                                                                        \
			const JsonValue *list = json_read_array(document, (name), (loader)->error);                                \
			(out_items) = xcalloc(list->count, sizeof(Type));                                                          \
			(out_count) = (int)list->count;                                                                            \
			for (size_t i = 0; i < list->count; i++) {                                                                 \
				const JsonValue *raw = list->items[i];                                                                 \
				Type *entry = &(out_items)[i];                                                                         \
				read_call;                                                                                             \
			}                                                                                                          \
			json_free(document);                                                                                       \
		}                                                                                                              \
	} while (0)

static const char *embedded_source(const char *path, void *context) {
	(void)context;
	return shared_file(path);
}

bool load_game_data(GameData *out, char *error, size_t error_size) {
	return load_game_data_from(embedded_source, NULL, out, error, error_size);
}

bool load_game_data_from(DataSource source, void *context, GameData *out, char *error, size_t error_size) {
	JsonError json_error = {0};
	Loader loader = {.source = source, .context = context, .error = &json_error};
	memset(out, 0, sizeof(*out));

	JsonValue *balance = read_document(&loader, "balance");
	if (balance != NULL) {
		read_balance(balance, &out->balance, &json_error);
		json_free(balance);
	}
	LOAD_LIST(
	    &loader, "vocations", VocationDef, out->vocations, out->vocation_count, read_vocation(raw, entry, &json_error));
	LOAD_LIST(&loader, "spells", SpellDef, out->spells, out->spell_count, read_spell(raw, entry, &json_error));
	LOAD_LIST(&loader, "monsters", MonsterDef, out->monsters, out->monster_count,
	    read_creature(raw, false, entry, &json_error));
	LOAD_LIST(
	    &loader, "bosses", MonsterDef, out->bosses, out->boss_count, read_creature(raw, true, entry, &json_error));
	LOAD_LIST(&loader, "potions", PotionDef, out->potions, out->potion_count, read_potion(raw, entry, &json_error));
	LOAD_LIST(&loader, "statuses", StatusDef, out->statuses, out->status_count, read_status(raw, entry, &json_error));
	LOAD_LIST(&loader, "items", ItemDef, out->items, out->item_count, read_item(raw, entry, &json_error));
	LOAD_LIST(&loader, "affixes", AffixDef, out->affixes, out->affix_count, read_affix(raw, entry, &json_error));
	LOAD_LIST(&loader, "achievements", AchievementDef, out->achievements, out->achievement_count,
	    read_achievement(raw, entry, &json_error));
	LOAD_LIST(
	    &loader, "families", Id, out->families, out->family_count, json_copy_text(raw, *entry, ID_SIZE, &json_error));

	if (!json_error.failed && (out->boss_count == 0 || out->boss_count > MAX_TIERS)) {
		json_error_set(&json_error, "data/bosses.json: expected between 1 and %d bosses", MAX_TIERS);
	}
	if (json_error.failed) {
		snprintf(error, error_size, "invalid game data: %s", json_error.message);
		game_data_free(out);
		return false;
	}
	return true;
}
