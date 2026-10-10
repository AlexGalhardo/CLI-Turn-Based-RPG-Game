#include "domain/character.h"
#include "support/helpers.h"

#include <stdlib.h>

#define SUITE "unit/character"

static Player new_player(const char *vocation) {
	Player player;
	memset(&player, 0, sizeof(player));
	str_copy(player.name, NAME_SIZE, "A");
	id_set(player.vocation_id, vocation);
	player.level = 1;
	player.magic_level = 1;
	return player;
}

TEST(SUITE, sheet_combines_vocation_level_and_equipment_with_caps) {
	GameData data = data_copy();
	add_test_items(&data);
	Player player = new_player("warrior");
	const VocationDef *warrior = data_vocation(&data, "warrior");

	CharacterSheet sheet = build_sheet(&player, &data);
	CHECK_INT(sheet.max_hp, warrior->start_hp);
	CHECK_INT(sheet.melee_min, warrior->melee_min);
	CHECK_INT(sheet.weapon_element, ELEMENT_PHYSICAL);
	CHECK_INT(sheet.protections[ELEMENT_FIRE], 0);

	player.level = 3;
	wear(&player, SLOT_RING, make_item(9, "test_ring", "common", 0));
	ItemInstance helmet = make_item(10, "test_helmet", "common", 0);
	helmet.affix_count = 1;
	helmet.affixes[0] = (AffixRoll){STAT_PROT_FIRE, 500};
	wear(&player, SLOT_HELMET, helmet);
	sheet = build_sheet(&player, &data);
	CHECK_INT(sheet.max_hp, warrior->start_hp + 2 * warrior->hp_per_level + 50);
	CHECK_INT(sheet.melee_max, warrior->melee_max + 2 * warrior->melee_per_level);
	CHECK_INT(sheet.armor, 10);
	CHECK_INT(sheet.crit_chance, data.balance.caps.crit_chance);
	CHECK_INT(sheet.dodge, data.balance.caps.dodge);
	CHECK_INT(sheet.protections[ELEMENT_FIRE], data.balance.caps.protection);
	game_data_free(&data);
}

TEST(SUITE, weapon_with_an_element_changes_the_melee_element) {
	const GameData *data = test_data();
	const ItemDef *elemental = NULL;
	for (int i = 0; i < data->item_count && elemental == NULL; i++) {
		const ItemDef *item = &data->items[i];
		if (item->slot == SLOT_WEAPON && item->has_element && item->element != ELEMENT_PHYSICAL) {
			elemental = item;
		}
	}
	REQUIRE(elemental != NULL);
	Player player = new_player("mage");
	wear(&player, SLOT_WEAPON, make_item(1, elemental->id, "common", 0));
	CHECK_INT(build_sheet(&player, data).weapon_element, elemental->element);
}

TEST(SUITE, equipment_helpers) {
	Player player = new_player("warrior");
	CHECK(player_equipped(&player, SLOT_RING) == NULL);
	wear(&player, SLOT_RING, make_item(9, "test_ring", "common", 0));
	const ItemInstance *ring = player_equipped(&player, SLOT_RING);
	CHECK(ring != NULL && ring->uid == 9);

	ItemInstance first = make_item(1, "sword", "common", 0);
	ItemInstance second = make_item(2, "sword", "rare", 0);
	player_bag_push(&player, &first);
	player_bag_push(&player, &second);
	CHECK_INT(player_bag_index(&player, 2), 1);
	CHECK_INT(player_bag_index(&player, 3), -1);
	player_bag_remove(&player, 0);
	REQUIRE_INT(player.bag_count, 1);
	CHECK_INT(player.bag[0].uid, 2);

	GameData data = data_copy();
	add_test_items(&data);
	CHECK_INT(equipment_score(&player, &data), item_score(ring, &data));
	game_data_free(&data);
}

TEST(SUITE, game_data_lookups) {
	const GameData *data = test_data();
	CHECK(find_vocation(data, "mage") != NULL);
	CHECK(find_vocation(data, "knight") == NULL);
	CHECK(find_spell(data, "brutal_strike") != NULL);
	CHECK(find_creature(data, "rat") != NULL);
	CHECK(find_potion(data, "mana_potion") != NULL);
	CHECK(find_potion(data, "elixir") == NULL);
	CHECK(find_item(data, "sword") != NULL);
	CHECK(find_item(data, "excalibur") == NULL);
	CHECK(data_creature(data, "munster")->is_boss);
	CHECK_INT(data_creature(data, "rat")->resistances[ELEMENT_HOLY], DEFAULT_RESISTANCE);
	CHECK(vocation_has_spell(data_vocation(data, "warrior"), "brutal_strike"));
	CHECK(!vocation_has_spell(data_vocation(data, "warrior"), "flame_strike"));
	// The reference raises UnknownIdError from data.vocation("knight"), data.potion("elixir"), data.status("frozen"),
	// data.item("excalibur") and boss_of_tier(99): the plain lookups here stop the program (fatal) for an unknown
	// id, so only the find_* variants above can be checked in-process.

	const MonsterDef **tier = xcalloc((size_t)data->monster_count, sizeof(*tier));
	int count = data_monsters_in_tier(data, 0, tier);
	CHECK(count > 0);
	for (int i = 0; i < count; i++) {
		CHECK_INT(tier[i]->tier, 0);
		CHECK(i == 0 || strcmp(tier[i - 1]->id, tier[i]->id) < 0);
	}
	free(tier);
}

TEST(SUITE, enums_and_counters) {
	CHECK_INT(protection_stat(ELEMENT_FIRE), STAT_PROT_FIRE);
	CHECK_INT(protection_stat(ELEMENT_PHYSICAL), STAT_PROT_PHYSICAL);
	CHECK_STR(phase_name(PHASE_GAME_OVER), "game_over");
	Phase phase = PHASE_MERCHANT;
	CHECK(phase_parse("battle", &phase) && phase == PHASE_BATTLE);
	CHECK(phase_parse("merchant", &phase) && phase == PHASE_MERCHANT);
	CHECK(phase_parse(phase_name(PHASE_GAME_OVER), &phase) && phase == PHASE_GAME_OVER);
	CHECK(!phase_parse("lobby", &phase));

	// Every name parses back to its value.
	for (int i = 0; i < ELEMENT_COUNT; i++) {
		Element value;
		CHECK(element_parse(element_name((Element)i), &value) && value == (Element)i);
	}
	for (int i = 0; i < SLOT_COUNT; i++) {
		Slot value;
		CHECK(slot_parse(slot_name((Slot)i), &value) && value == (Slot)i);
	}
	for (int i = 0; i < STAT_COUNT; i++) {
		Stat value;
		CHECK(stat_parse(stat_name((Stat)i), &value) && value == (Stat)i);
	}
	for (int i = 0; i < ENEMY_CLASS_COUNT; i++) {
		EnemyClass value;
		CHECK(enemy_class_parse(enemy_class_name((EnemyClass)i), &value) && value == (EnemyClass)i);
	}

	Counter counts = {0};
	counter_set(&counts, "a", 2);
	CHECK_INT(counter_get(&counts, "a"), 2);
	CHECK_INT(counter_get(&counts, "b"), 0);
	CHECK(counter_has(&counts, "a") && !counter_has(&counts, "b"));
	counter_add(&counts, "b", 3);
	counter_add(&counts, "0", 1);
	CHECK_INT(counter_total(&counts), 6);
	// Kept sorted by key: the order saves are written in.
	REQUIRE_INT(counts.count, 3);
	CHECK_STR(counts.entries[0].key, "0");
	CHECK_STR(counts.entries[2].key, "b");
	counter_free(&counts);

	MonsterInstance monster;
	memset(&monster, 0, sizeof(monster));
	monster.attack_count = 1;
	id_set(monster.attacks[0].id, "bite");
	monster.attacks[0].min = 1;
	monster.attacks[0].max = 2;
	monster.attacks[0].weight = 1;
	CHECK_INT(monster_attack(&monster, "bite")->max, 2);
	// monster_attack("laser") raises in the reference; here it is fatal.
}
