#include "domain/definitions.h"

#include <stdlib.h>
#include <string.h>

void game_data_free(GameData *data) {
	free(data->monsters);
	free(data->bosses);
	free(data->vocations);
	free(data->spells);
	free(data->potions);
	free(data->statuses);
	free(data->items);
	free(data->affixes);
	free(data->achievements);
	free(data->families);
	memset(data, 0, sizeof(*data));
}

// Linear search over an array of structs that start with an `Id id` field.
#define FIND_BY_ID(items, count, wanted)                                                                               \
	do {                                                                                                               \
		for (int i = 0; i < (count); i++) {                                                                            \
			if (strcmp((items)[i].id, (wanted)) == 0) {                                                                \
				return &(items)[i];                                                                                    \
			}                                                                                                          \
		}                                                                                                              \
		return NULL;                                                                                                   \
	} while (0)

const VocationDef *find_vocation(const GameData *data, const char *id) {
	FIND_BY_ID(data->vocations, data->vocation_count, id);
}

const DifficultyDef *find_difficulty(const Balance *balance, const char *id) {
	FIND_BY_ID(balance->difficulties, balance->difficulty_count, id);
}

const SpellDef *find_spell(const GameData *data, const char *id) { FIND_BY_ID(data->spells, data->spell_count, id); }

const PotionDef *find_potion(const GameData *data, const char *id) {
	FIND_BY_ID(data->potions, data->potion_count, id);
}

const ItemDef *find_item(const GameData *data, const char *id) { FIND_BY_ID(data->items, data->item_count, id); }

static const StatusDef *find_status(const GameData *data, const char *id) {
	FIND_BY_ID(data->statuses, data->status_count, id);
}

const RarityDef *find_rarity(const Balance *balance, const char *id) {
	FIND_BY_ID(balance->rarities, balance->rarity_count, id);
}

const AutoBattleModeDef *find_auto_battle_mode(const AutoBattleDef *config, const char *id) {
	FIND_BY_ID(config->modes, config->mode_count, id);
}

const MonsterDef *find_creature(const GameData *data, const char *id) {
	for (int i = 0; i < data->monster_count; i++) {
		if (strcmp(data->monsters[i].id, id) == 0) {
			return &data->monsters[i];
		}
	}
	FIND_BY_ID(data->bosses, data->boss_count, id);
}

#define REQUIRE(found, kind, id)                                                                                       \
	do {                                                                                                               \
		const void *result = (found);                                                                                  \
		if (result == NULL) {                                                                                          \
			fatal("unknown " kind " id: %s", (id));                                                                    \
		}                                                                                                              \
		return result;                                                                                                 \
	} while (0)

const VocationDef *data_vocation(const GameData *data, const char *id) {
	REQUIRE(find_vocation(data, id), "vocation", id);
}

const SpellDef *data_spell(const GameData *data, const char *id) { REQUIRE(find_spell(data, id), "spell", id); }

const MonsterDef *data_creature(const GameData *data, const char *id) {
	REQUIRE(find_creature(data, id), "creature", id);
}

const PotionDef *data_potion(const GameData *data, const char *id) { REQUIRE(find_potion(data, id), "potion", id); }

const StatusDef *data_status(const GameData *data, const char *id) { REQUIRE(find_status(data, id), "status", id); }

const ItemDef *data_item(const GameData *data, const char *id) { REQUIRE(find_item(data, id), "item", id); }

const DifficultyDef *balance_difficulty(const Balance *balance, const char *id) {
	REQUIRE(find_difficulty(balance, id), "difficulty", id);
}

const RarityDef *balance_rarity(const Balance *balance, const char *id) {
	REQUIRE(find_rarity(balance, id), "rarity", id);
}

const EnemyClassDef *balance_enemy_class(const Balance *balance, EnemyClass enemy_class) {
	return &balance->enemy_classes[enemy_class];
}

int data_tier_count(const GameData *data) { return data->boss_count; }

static int compare_monsters_by_id(const void *a, const void *b) {
	const MonsterDef *const *left = a;
	const MonsterDef *const *right = b;
	return strcmp((*left)->id, (*right)->id);
}

int data_monsters_in_tier(const GameData *data, int64_t tier, const MonsterDef **out) {
	int count = 0;
	for (int i = 0; i < data->monster_count; i++) {
		if (data->monsters[i].tier == tier) {
			out[count++] = &data->monsters[i];
		}
	}
	// strcmp orders ASCII ids by code point, the order every implementation uses for "sorted by id".
	qsort(out, (size_t)count, sizeof(*out), compare_monsters_by_id);
	return count;
}

const MonsterDef *data_boss_of_tier(const GameData *data, int64_t tier) {
	// The reference indexes bosses by tier in a dict: a later boss of the same tier wins.
	const MonsterDef *found = NULL;
	for (int i = 0; i < data->boss_count; i++) {
		if (data->bosses[i].tier == tier) {
			found = &data->bosses[i];
		}
	}
	if (found == NULL) {
		fatal("no boss for tier %lld", (long long)tier);
	}
	return found;
}

const MonsterAttack *monster_def_attack(const MonsterDef *monster, const char *attack_id) {
	for (int i = 0; i < monster->attack_count; i++) {
		if (strcmp(monster->attacks[i].id, attack_id) == 0) {
			return &monster->attacks[i];
		}
	}
	fatal("unknown attack id: %s", attack_id);
}

bool vocation_has_spell(const VocationDef *vocation, const char *spell_id) {
	for (int i = 0; i < vocation->spell_count; i++) {
		if (strcmp(vocation->spells[i], spell_id) == 0) {
			return true;
		}
	}
	return false;
}
