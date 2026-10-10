#include "presentation/event_text.h"
#include "support/helpers.h"

#include <stdlib.h>

#define SUITE "unit/event_text"

// An English formatter and a new run whose monster is a plain rat.
typedef struct {
	Translator translator;
	EventFormatter formatter;
	GameEngine engine;
} Fixture;

static void fixture_open(Fixture *fixture) {
	if (!translator_init(&fixture->translator, DEFAULT_LOCALE)) {
		fatal("cannot load the default locale");
	}
	fixture->formatter = (EventFormatter){.data = test_data(), .translator = &fixture->translator};
	fixture->engine = new_engine(test_data(), "warrior", "normal", 42);
}

static void fixture_close(Fixture *fixture) {
	engine_free(&fixture->engine);
	translator_free(&fixture->translator);
}

static void spawn_rat(RunState *state) {
	MonsterInstance rat;
	memset(&rat, 0, sizeof(rat));
	id_set(rat.creature_id, "rat");
	rat.enemy_class = ENEMY_NORMAL;
	rat.hp = 10;
	rat.max_hp = 10;
	rat.xp = 1;
	rat.gold_min = 1;
	rat.gold_max = 1;
	state->monster = rat;
	state->has_monster = true;
}

static void check_format(const Fixture *fixture, Event event, const char *expected) {
	char *text = event_format(&fixture->formatter, &event, &fixture->engine.state);
	CHECK_STR(text, expected);
	free(text);
}

static Event player_attacked(int64_t damage, bool crit, const char *element) {
	Event event = event_new("player_attacked");
	event_int(&event, "damage", damage);
	event_bool(&event, "crit", crit);
	event_str(&event, "element", element);
	return event;
}

static Event spell_cast(const char *spell_id, int64_t damage, int64_t mana) {
	Event event = event_new("spell_cast");
	event_str(&event, "spellId", spell_id);
	event_int(&event, "damage", damage);
	event_bool(&event, "crit", false);
	event_str(&event, "element", "fire");
	event_int(&event, "mana", mana);
	return event;
}

static Event round_started(int64_t round, const char *monster_id, bool is_boss, const char *enemy_class, int64_t hp) {
	Event event = event_new("round_started");
	event_int(&event, "round", round);
	event_int(&event, "tier", 0);
	event_int(&event, "cycle", 0);
	event_str(&event, "monsterId", monster_id);
	event_bool(&event, "isBoss", is_boss);
	if (enemy_class != NULL) {
		event_str(&event, "enemyClass", enemy_class);
	}
	event_int(&event, "hp", hp);
	return event;
}

static Event with_int(const char *type, const char *name, int64_t value) {
	Event event = event_new(type);
	event_int(&event, name, value);
	return event;
}

static Event item_event(const char *type, int64_t uid, const char *item_id) {
	Event event = event_new(type);
	event_int(&event, "uid", uid);
	event_str(&event, "itemId", item_id);
	return event;
}

TEST(SUITE, format_events) {
	Fixture fixture;
	fixture_open(&fixture);
	spawn_rat(&fixture.engine.state);
	Event event;

	check_format(&fixture, player_attacked(12, false, "fire"), "You hit for 12 fire damage.");
	check_format(&fixture, player_attacked(30, true, "physical"), "CRITICAL! You hit for 30 physical damage.");
	check_format(&fixture, spell_cast("flame_strike", 9, 20), "Flame Strike deals 9 fire damage.");

	event = event_new("potion_used");
	event_str(&event, "potionId", "mana_potion");
	event_int(&event, "amount", 80);
	event_str(&event, "resource", "mp");
	check_format(&fixture, event, "Mana Potion restores 80 MP.");

	event = event_new("status_applied");
	event_str(&event, "target", "player");
	event_str(&event, "status", "burn");
	event_int(&event, "turns", 3);
	event_int(&event, "perTurn", 2);
	check_format(&fixture, event, "You are burning (3 turns).");

	event = event_new("monster_killed");
	event_str(&event, "monsterId", "dragon");
	event_bool(&event, "isBoss", false);
	check_format(&fixture, event, "You defeated Dragon!");

	check_format(
	    &fixture, round_started(10, "munster", true, NULL, 5), "Round 10: the boss Munster challenges you! (5 HP)");

	event = item_event("item_sold", 3, "sword");
	event_int(&event, "gold", 25);
	check_format(&fixture, event, "You sold Sword for 25 gold.");

	check_format(&fixture, event_error("not_enough_mana"), "Not enough mana.");
	check_format(&fixture, event_error("level_too_low"), "Your level is too low for that item.");
	check_format(&fixture, round_started(3, "rat", false, "elite", 90), "Round 3: an ELITE Rat appears! (90 HP)");

	event = event_new("monster_attacked");
	event_str(&event, "attackId", "bite");
	event_int(&event, "damage", 9);
	event_str(&event, "element", "physical");
	event_bool(&event, "charged", false);
	event_bool(&event, "crit", true);
	check_format(&fixture, event, "CRITICAL! Rat hits you for 9 physical damage.");

	check_format(&fixture, event_new("monster_dodged"), "Rat dodges your attack!");
	check_format(&fixture, with_int("monster_parried", "reflected", 4), "Rat parries your attack: you take 4 damage!");
	check_format(&fixture, with_int("monster_healed", "amount", 12), "Rat heals 12 HP.");

	event = event_new("attack_parried");
	event_str(&event, "attackId", "bite");
	event_int(&event, "reflected", 3);
	check_format(&fixture, event, "You parry the attack and reflect 3 damage!");

	event = item_event("item_auto_equipped", 5, "sword");
	event_str(&event, "slot", "weapon");
	event_int(&event, "score", 60);
	check_format(&fixture, event, "Auto-equipped Sword (score 60).");

	event = item_event("item_auto_sold", 6, "bow");
	event_int(&event, "gold", 30);
	check_format(&fixture, event, "Sold Bow for 30 gold (auto-sell).");

	event = event_new("potion_dropped");
	event_str(&event, "potionId", "mana_potion");
	check_format(&fixture, event, "Loot: Mana Potion!");

	check_format(&fixture, with_int("run_won", "round", 100), "VICTORY! You defeated the final boss on round 100!");

	event = event_new("run_ended");
	event_bool(&event, "won", true);
	check_format(&fixture, event, "Your victory is recorded in the Hall of Fame.");

	// An id the data does not know is shown as it is.
	event = spell_cast("ghost_spell", 1, 1);
	char *text = event_format(&fixture.formatter, &event, &fixture.engine.state);
	CHECK_CONTAINS(text, "ghost_spell");
	free(text);
	fixture_close(&fixture);
}

TEST(SUITE, monster_name_comes_from_state) {
	Fixture fixture;
	fixture_open(&fixture);
	step(&fixture.engine, cmd_next_fight());
	REQUIRE(fixture.engine.state.has_monster);
	const char *name = data_creature(test_data(), fixture.engine.state.monster.creature_id)->name;
	Event event = event_new("monster_stunned");
	char *text = event_format(&fixture.formatter, &event, &fixture.engine.state);
	CHECK(strncmp(text, name, strlen(name)) == 0);
	free(text);
	fixture_close(&fixture);
}

TEST(SUITE, item_name_by_uid) {
	Fixture fixture;
	fixture_open(&fixture);
	ItemInstance bow = make_item(77, "bow", "rare", 0);
	player_bag_push(&fixture.engine.state.player, &bow);
	Event dropped = item_event("item_dropped", 77, "bow");
	event_str(&dropped, "rarity", "rare");
	check_format(&fixture, dropped, "Loot: Bow [Rare]!");

	Event equipped = event_new("item_equipped");
	event_int(&equipped, "uid", 999);
	event_str(&equipped, "slot", "ring");
	char *text = event_format(&fixture.formatter, &equipped, &fixture.engine.state);
	CHECK_CONTAINS(text, "#999");
	free(text);
	fixture_close(&fixture);
}
