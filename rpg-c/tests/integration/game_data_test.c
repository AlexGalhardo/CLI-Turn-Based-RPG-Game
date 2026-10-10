#include "assets/shared_files.h"
#include "infrastructure/data_loader.h"
#include "infrastructure/paths.h"
#include "support/helpers.h"

#include <stdlib.h>

#define SUITE "integration/game_data"

TEST(SUITE, content_requirements) {
	const GameData *data = test_data();
	CHECK(data->monster_count >= 100);
	CHECK_INT(data_tier_count(data), 10);
	CHECK_INT(data->vocation_count, 3);
	CHECK(find_vocation(data, "warrior") != NULL);
	CHECK(find_vocation(data, "archer") != NULL);
	CHECK(find_vocation(data, "mage") != NULL);
	const MonsterDef **tier = xcalloc((size_t)data->monster_count, sizeof(*tier));
	for (int64_t i = 0; i < data_tier_count(data); i++) {
		CHECK(data_monsters_in_tier(data, i, tier) >= 9);
		CHECK(data_boss_of_tier(data, i)->is_boss);
	}
	free(tier);
}

static bool known_family(const GameData *data, const char *family) {
	for (int i = 0; i < data->family_count; i++) {
		if (str_eq(data->families[i], family)) {
			return true;
		}
	}
	return false;
}

static bool known_status(const GameData *data, const char *status) {
	for (int i = 0; i < data->status_count; i++) {
		if (str_eq(data->statuses[i].id, status)) {
			return true;
		}
	}
	return false;
}

static void check_creature(const GameData *data, const MonsterDef *creature) {
	if (!known_family(data, creature->family)) {
		test_fail(__FILE__, __LINE__, "%s: unknown family %s", creature->id, creature->family);
	}
	CHECK(creature->attack_count > 0);
	bool charge_found = false;
	for (int i = 0; i < creature->attack_count; i++) {
		const MonsterAttack *attack = &creature->attacks[i];
		CHECK(attack->min <= attack->max);
		if (attack->has_status && !known_status(data, attack->status.status)) {
			test_fail(__FILE__, __LINE__, "%s: unknown status %s", creature->id, attack->status.status);
		}
		charge_found = charge_found || (creature->has_charge_attack && str_eq(attack->id, creature->charge_attack));
	}
	if (creature->is_boss && !charge_found) {
		test_fail(__FILE__, __LINE__, "%s: the charge attack is not one of its attacks", creature->id);
	}
	// Monsters and bosses share one id space: looking the id up must come back to this definition.
	if (find_creature(data, creature->id) != creature) {
		test_fail(__FILE__, __LINE__, "duplicated creature id %s", creature->id);
	}
}

TEST(SUITE, cross_references_are_valid) {
	const GameData *data = test_data();
	for (int v = 0; v < data->vocation_count; v++) {
		const VocationDef *vocation = &data->vocations[v];
		CHECK(find_item(data, vocation->starter_weapon) != NULL);
		for (int s = 0; s < vocation->spell_count; s++) {
			CHECK(find_spell(data, vocation->spells[s]) != NULL);
		}
	}
	for (int i = 0; i < data->monster_count; i++) {
		CHECK(!data->monsters[i].is_boss);
		check_creature(data, &data->monsters[i]);
	}
	for (int i = 0; i < data->boss_count; i++) {
		CHECK(data->bosses[i].is_boss);
		check_creature(data, &data->bosses[i]);
	}
	for (int i = 0; i < data->spell_count; i++) {
		const Level3Bonus *bonus = &data->spells[i].level3_bonus;
		if (bonus->has_status) {
			CHECK(known_status(data, bonus->status));
			CHECK(data_status(data, bonus->status)->kind < STATUS_KIND_COUNT);
		}
	}
	for (int i = 0; i < data->balance.starting_potion_count; i++) {
		CHECK(find_potion(data, data->balance.starting_potions[i].potion_id) != NULL);
	}
}

TEST(SUITE, balance_m8_tables) {
	const GameData *data = test_data();
	const Balance *balance = &data->balance;
	const char *const rarity_ids[] = {"common", "rare", "legendary", "mythic"};
	const int64_t rarity_stats[] = {100, 150, 200, 300};
	REQUIRE_INT(balance->rarity_count, ARRAY_LEN(rarity_ids));
	for (size_t i = 0; i < ARRAY_LEN(rarity_ids); i++) {
		CHECK_STR(balance->rarities[i].id, rarity_ids[i]);
		CHECK_INT(balance->rarities[i].stat_pct, rarity_stats[i]);
	}
	const int64_t effects[] = {100, 150, 200};
	REQUIRE_INT(balance->spell_level_count, ARRAY_LEN(effects));
	for (size_t i = 0; i < ARRAY_LEN(effects); i++) {
		CHECK_INT(balance->spell_levels[i].effect_pct, effects[i]);
	}

	// The loaded tables are arrays indexed by enums, so the key sets are checked on the file itself.
	JsonError error = {0};
	JsonValue *raw = json_parse(shared_file("data/balance.json"), &error);
	REQUIRE(raw != NULL);
	const JsonValue *rarity_weights = json_read_object(raw, "rarityWeights", &error);
	REQUIRE_INT(rarity_weights->count, 1);
	CHECK_STR(rarity_weights->keys[0], "merchant");
	const JsonValue *classes = json_read_object(raw, "enemyClasses", &error);
	CHECK_INT(classes->count, ENEMY_CLASS_COUNT);
	for (size_t i = 0; i < classes->count; i++) {
		EnemyClass enemy_class;
		CHECK(enemy_class_parse(classes->keys[i], &enemy_class));
		const JsonValue *weights = json_read_object(classes->items[i], "rarityWeights", &error);
		for (size_t j = 0; j < weights->count; j++) {
			CHECK(find_rarity(balance, weights->keys[j]) != NULL);
		}
	}
	const JsonValue *score_weights = json_read_object(raw, "itemScoreWeights", &error);
	CHECK_INT(score_weights->count, STAT_COUNT);
	bool seen[STAT_COUNT] = {false};
	for (size_t i = 0; i < score_weights->count; i++) {
		Stat stat;
		if (stat_parse(score_weights->keys[i], &stat)) {
			CHECK(!seen[stat]);
			seen[stat] = true;
			CHECK_INT(balance->item_score_weights[stat], json_as_int(score_weights->items[i], &error));
		} else {
			test_fail(__FILE__, __LINE__, "unknown stat %s", score_weights->keys[i]);
		}
	}
	CHECK(!error.failed);
	json_free(raw);

	CHECK(balance_enemy_class(balance, ENEMY_ELITE)->stat_pct > balance_enemy_class(balance, ENEMY_NORMAL)->stat_pct);
	CHECK_STR(data_boss_of_tier(data, data_tier_count(data) - 1)->id, "ferumbras");
	CHECK_INT(balance->final_round, balance->rounds_per_tier * data_tier_count(data));
}

// The reference raises UnknownIdError; here the plain lookups are fatal, so the `find_*` variants (and the enum
// parsers) are the ones that report an unknown id. data.creature("rat").attack("laser") has no such variant.
TEST(SUITE, lookup_errors) {
	const GameData *data = test_data();
	CHECK(find_spell(data, "avada_kedavra") == NULL);
	CHECK(find_difficulty(&data->balance, "nightmare") == NULL);
	CHECK(find_rarity(&data->balance, "epic") == NULL);
	EnemyClass enemy_class;
	CHECK(!enemy_class_parse("champion", &enemy_class));
	CHECK(find_auto_battle_mode(&data->balance.auto_battle, "berserk") == NULL);
	CHECK(find_creature(data, "rat") != NULL);
}

typedef struct {
	const char *path;
	const char *content; // NULL = the file does not exist
} Override;

static const char *overridden_source(const char *path, void *context) {
	const Override *override = context;
	return str_eq(path, override->path) ? override->content : shared_file(path);
}

static void check_load_fails(Override override, const char *expected_part) {
	GameData data;
	char error[256] = "";
	if (load_game_data_from(overridden_source, &override, &data, error, sizeof(error))) {
		test_fail(__FILE__, __LINE__, "broken %s was accepted", override.path);
		game_data_free(&data);
		return;
	}
	CHECK_CONTAINS(error, "invalid game data");
	CHECK_CONTAINS(error, expected_part);
}

TEST(SUITE, invalid_data_is_reported) {
	JsonError error = {0};
	JsonValue *document = json_parse(shared_file("data/vocations.json"), &error);
	REQUIRE(document != NULL);
	const JsonValue *vocations = json_read_array(document, "vocations", &error);
	REQUIRE(vocations->count > 0);
	json_remove(vocations->items[0], "startHp");
	char *missing_field = json_dump(document, false);
	json_free(document);
	check_load_fails((Override){"data/vocations.json", missing_field}, "startHp");
	free(missing_field);

	check_load_fails((Override){"data/vocations.json", "{ not json"}, "vocations.json");
	check_load_fails((Override){"data/vocations.json", NULL}, "data/vocations.json: file not found");
	check_load_fails((Override){"data/vocations.json", "[]"}, "expected object");
	check_load_fails((Override){"data/bosses.json", "{\"bosses\": []}"}, "bosses");

	// The untouched files still load through the same entry point.
	GameData data;
	char message[256] = "";
	Override none = {"data/none.json", NULL};
	CHECK(load_game_data_from(overridden_source, &none, &data, message, sizeof(message)));
	CHECK_INT(data.monster_count, test_data()->monster_count);
	game_data_free(&data);
}

// The shared folder is embedded in the binary, so the reference's find_shared_dir() checks have no counterpart.
TEST(SUITE, paths) {
	char path[PATH_SIZE];
	resolve_data_dir("custom", path);
	CHECK_STR(path, "custom");

	const char *current = getenv(DATA_DIR_ENV);
	char *previous = current != NULL ? xstrdup(current) : NULL;
	test_setenv(DATA_DIR_ENV, "from-env/x");
	resolve_data_dir(NULL, path);
	CHECK_STR(path, "from-env/x");
	// An empty value counts as unset (and is how Windows removes a variable).
	test_setenv(DATA_DIR_ENV, "");
	resolve_data_dir(NULL, path);
	size_t length = strlen(path);
	size_t name_length = strlen(DEFAULT_DATA_DIR_NAME);
	CHECK(length > name_length && str_eq(path + length - name_length, ".cli-turn-based-rpg"));
	CHECK(path[length - name_length - 1] == '/');
	test_setenv(DATA_DIR_ENV, previous != NULL ? previous : "");
	free(previous);

	resolve_data_dir_from("flag", "env", "home", path);
	CHECK_STR(path, "flag");
	resolve_data_dir_from(NULL, "env", "home", path);
	CHECK_STR(path, "env");
	resolve_data_dir_from(NULL, NULL, "home", path);
	CHECK_STR(path, "home/.cli-turn-based-rpg");
}
