#include "support/helpers.h"

#include "application/merchant.h"
#include "infrastructure/data_loader.h"
#include "infrastructure/filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#define process_id() _getpid()
#define pause_briefly() Sleep(20)
#else
#include <unistd.h>
#define process_id() getpid()
#define pause_briefly() nanosleep(&(struct timespec){.tv_nsec = 20000000}, NULL)
#endif

const GameData *test_data(void) {
	static GameData data;
	static bool loaded;
	if (!loaded) {
		char error[256];
		if (!load_game_data(&data, error, sizeof(error))) {
			fatal("%s", error);
		}
		loaded = true;
	}
	return &data;
}

#define COPY_ARRAY(field, count_field, Type)                                                                           \
	do {                                                                                                               \
		copy.field = xcalloc((size_t)source->count_field + 1, sizeof(Type));                                           \
		memcpy(copy.field, source->field, (size_t)source->count_field * sizeof(Type));                                 \
	} while (0)

GameData data_copy(void) {
	const GameData *source = test_data();
	GameData copy = *source;
	COPY_ARRAY(monsters, monster_count, MonsterDef);
	COPY_ARRAY(bosses, boss_count, MonsterDef);
	COPY_ARRAY(vocations, vocation_count, VocationDef);
	COPY_ARRAY(spells, spell_count, SpellDef);
	COPY_ARRAY(potions, potion_count, PotionDef);
	COPY_ARRAY(statuses, status_count, StatusDef);
	COPY_ARRAY(items, item_count, ItemDef);
	COPY_ARRAY(affixes, affix_count, AffixDef);
	COPY_ARRAY(achievements, achievement_count, AchievementDef);
	COPY_ARRAY(families, family_count, Id);
	return copy;
}

ItemDef *add_item(GameData *data, const char *id, const char *name, Slot slot, const char *type) {
	data->items = xrealloc(data->items, (size_t)(data->item_count + 1) * sizeof(ItemDef));
	ItemDef *item = &data->items[data->item_count++];
	memset(item, 0, sizeof(*item));
	id_set(item->id, id);
	str_copy(item->name, NAME_SIZE, name);
	item->slot = slot;
	id_set(item->type, type);
	item->value = 100;
	return item;
}

static void set_stat(ItemDef *item, Stat stat, int64_t value) {
	item->has_stat[stat] = true;
	item->stats[stat] = value;
}

void add_test_items(GameData *data) {
	ItemDef *helmet = add_item(data, "test_helmet", "Test Helmet", SLOT_HELMET, "helmet");
	set_stat(helmet, STAT_ARMOR, 10);
	set_stat(helmet, STAT_MAX_HP, 50);
	ItemDef *ring = add_item(data, "test_ring", "Test Ring", SLOT_RING, "ring");
	set_stat(ring, STAT_CRIT_CHANCE, 80);
	set_stat(ring, STAT_DODGE, 90);
	set_stat(add_item(data, "test_axe", "Test Axe", SLOT_WEAPON, "axe"), STAT_ATTACK, 20);
	set_stat(add_item(data, "test_rod", "Test Rod", SLOT_WEAPON, "rod"), STAT_ATTACK, 1);
}

void calm(GameData *data) {
	for (int i = 0; i < ENEMY_CLASS_COUNT; i++) {
		EnemyClassDef *row = &data->balance.enemy_classes[i];
		row->dodge = 0;
		row->parry = 0;
		row->crit = 0;
		row->heal = 0;
	}
	data->balance.elite_chance_pct = 0;
}

void all_elites(GameData *data) { data->balance.elite_chance_pct = 100; }

static GameEngine engine_with(
    const GameData *data, const char *vocation, const char *difficulty, int64_t seed, bool auto_equip) {
	GameEngine engine;
	EventList events = {0};
	char error[128];
	RunConfig config = run_config("Tester", vocation, difficulty, auto_equip);
	if (!engine_new_run(&engine, data, &config, seed, &events, error, sizeof(error))) {
		fatal("%s", error);
	}
	events_free(&events);
	return engine;
}

GameEngine new_engine(const GameData *data, const char *vocation, const char *difficulty, int64_t seed) {
	return engine_with(data, vocation, difficulty, seed, false);
}

GameEngine new_engine_auto_equip(const GameData *data, const char *vocation, const char *difficulty, int64_t seed) {
	return engine_with(data, vocation, difficulty, seed, true);
}

const EventList *step(GameEngine *engine, Command command) {
	static EventList events;
	events_clear(&events);
	engine_step(engine, &command, &events);
	return &events;
}

MonsterInstance *fight(GameEngine *engine) {
	step(engine, cmd_next_fight());
	if (!engine->state.has_monster) {
		fatal("next_fight did not spawn a monster");
	}
	return &engine->state.monster;
}

void fixed_attack(MonsterInstance *monster, int64_t damage, Element element, const StatusOnHit *status) {
	MonsterAttack attack;
	memset(&attack, 0, sizeof(attack));
	id_set(attack.id, "test_hit");
	attack.element = element;
	attack.min = damage;
	attack.max = damage;
	attack.weight = 1;
	if (status != NULL) {
		attack.has_status = true;
		attack.status = *status;
	}
	monster->attack_count = 1;
	monster->attacks[0] = attack;
	monster->hp = 1000000;
	monster->max_hp = 1000000;
}

ItemInstance make_item(int64_t uid, const char *item_id, const char *rarity, int64_t tier) {
	ItemInstance item;
	memset(&item, 0, sizeof(item));
	item.uid = uid;
	id_set(item.item_id, item_id);
	id_set(item.rarity, rarity);
	item.tier = tier;
	return item;
}

const Event *find_event(const EventList *events, const char *type) {
	for (size_t i = 0; i < events->count; i++) {
		if (event_is(&events->items[i], type)) {
			return &events->items[i];
		}
	}
	return NULL;
}

int count_events(const EventList *events, const char *type) {
	int count = 0;
	for (size_t i = 0; i < events->count; i++) {
		count += event_is(&events->items[i], type) ? 1 : 0;
	}
	return count;
}

bool has_event(const EventList *events, const char *type) { return find_event(events, type) != NULL; }

bool is_error(const EventList *events, const char *code) {
	return events->count == 1 && event_is(&events->items[0], "error") &&
	       str_eq(event_get_str(&events->items[0], "code"), code);
}

char *read_file(const char *path) {
	char *text = fs_read_text(path);
	if (text == NULL) {
		test_fail(__FILE__, __LINE__, "cannot read %s", path);
	}
	return text;
}

JsonValue *load_json_file(const char *path) {
	char *text = read_file(path);
	if (text == NULL) {
		return NULL;
	}
	JsonError error = {0};
	JsonValue *document = json_parse(text, &error);
	free(text);
	if (document == NULL) {
		test_fail(__FILE__, __LINE__, "%s: %s", path, error.message);
	}
	return document;
}

void temp_dir_create(char out[TEST_PATH_SIZE]) {
	static int counter;
	const char *base = getenv("TEMP");
	if (base == NULL) {
		base = getenv("TMPDIR");
	}
	if (base == NULL) {
		base = "/tmp";
	}
	snprintf(out, TEST_PATH_SIZE, "%s/rpg-c-test-%d-%ld-%d", base, (int)process_id(), (long)time(NULL), counter++);
	if (!fs_make_directories(out)) {
		fatal("cannot create %s", out);
	}
}

void temp_dir_remove(const char *path) {
	// On Windows a file that was just deleted can linger for a moment (antivirus, indexer), and removing its
	// directory then fails: try again a few times instead of leaving empty directories behind.
	for (int attempt = 0; attempt < 50 && !fs_remove_tree(path); attempt++) {
		pause_briefly();
	}
}

void test_setenv(const char *name, const char *value) {
#ifdef _WIN32
	_putenv_s(name, value);
#else
	setenv(name, value, 1);
#endif
}

static int64_t fake_now(void *context) {
	FakeClock *clock = context;
	// Like the reference: the clock moves first, so the first reading is 12:00:10.
	clock->current += 10;
	return clock->current;
}

Clock fake_clock(FakeClock *state) {
	state->current = 1790510400; // 2026-09-27T12:00:00Z
	Clock clock = {.now = fake_now, .context = state};
	return clock;
}

#define FMT_BUFFERS 8
#define FMT_BUFFER_SIZE 2048

const char *fmt(const char *format, ...) {
	static char buffers[FMT_BUFFERS][FMT_BUFFER_SIZE];
	static int next;
	char *buffer = buffers[next];
	next = (next + 1) % FMT_BUFFERS;
	va_list args;
	va_start(args, format);
	int written = vsnprintf(buffer, FMT_BUFFER_SIZE, format, args);
	va_end(args);
	if (written < 0 || written >= FMT_BUFFER_SIZE) {
		fatal("fmt(): the text does not fit in %d bytes", FMT_BUFFER_SIZE);
	}
	return buffer;
}

const char *event_types(const EventList *events) {
	static StrBuf types;
	sb_clear(&types);
	sb_append(&types, "");
	for (size_t i = 0; i < events->count; i++) {
		if (i > 0) {
			sb_append_char(&types, ',');
		}
		sb_append(&types, events->items[i].type);
	}
	return types.data;
}

static JsonValue *parse_expected(const char *expected_json) {
	JsonError error = {0};
	JsonValue *expected = json_parse(expected_json, &error);
	if (expected == NULL) {
		fatal("bad expected JSON in a test (%s): %s", error.message, expected_json);
	}
	return expected;
}

bool event_matches(const Event *event, const char *expected_json) {
	JsonValue *expected = parse_expected(expected_json);
	JsonValue *actual = event_to_json(event);
	bool equal = json_equal(actual, expected);
	json_free(actual);
	json_free(expected);
	return equal;
}

bool events_contain(const EventList *events, const char *expected_json) {
	for (size_t i = 0; i < events->count; i++) {
		if (event_matches(&events->items[i], expected_json)) {
			return true;
		}
	}
	return false;
}

bool test_check_json_equal(const char *file, int line, const JsonValue *actual, const JsonValue *expected) {
	if (actual != NULL && expected != NULL && json_equal(actual, expected)) {
		return true;
	}
	char *actual_text = actual != NULL ? json_dump(actual, false) : xstrdup("(nothing)");
	char *expected_text = expected != NULL ? json_dump(expected, false) : xstrdup("(nothing)");
	test_fail(file, line, "JSON differs\n  expected %s\n  got      %s", expected_text, actual_text);
	free(actual_text);
	free(expected_text);
	return false;
}

bool test_check_json(const char *file, int line, const JsonValue *actual, const char *expected_json) {
	JsonValue *expected = parse_expected(expected_json);
	bool equal = test_check_json_equal(file, line, actual, expected);
	json_free(expected);
	return equal;
}

bool test_check_events(const char *file, int line, const EventList *events, const char *expected_json) {
	JsonValue *actual = events_to_json(events);
	bool equal = test_check_json(file, line, actual, expected_json);
	json_free(actual);
	return equal;
}

bool test_check_event(const char *file, int line, const Event *event, const char *expected_json) {
	if (event == NULL) {
		test_fail(file, line, "no event to compare with %s", expected_json);
		return false;
	}
	JsonValue *actual = event_to_json(event);
	bool equal = test_check_json(file, line, actual, expected_json);
	json_free(actual);
	return equal;
}

bool test_check_command(const char *file, int line, Command actual, Command expected) {
	if (command_equal(&actual, &expected)) {
		return true;
	}
	JsonValue *actual_json = command_to_json(&actual);
	JsonValue *expected_json = command_to_json(&expected);
	test_check_json_equal(file, line, actual_json, expected_json);
	json_free(actual_json);
	json_free(expected_json);
	return false;
}

bool items_equal(const ItemInstance *a, const ItemInstance *b) {
	JsonValue *left = item_instance_to_json(a);
	JsonValue *right = item_instance_to_json(b);
	bool equal = json_equal(left, right);
	json_free(left);
	json_free(right);
	return equal;
}

ActiveStatus make_status(const char *status_id, int64_t turns, int64_t per_turn) {
	ActiveStatus status;
	memset(&status, 0, sizeof(status));
	id_set(status.status_id, status_id);
	status.turns = turns;
	status.per_turn = per_turn;
	return status;
}

void wear(Player *player, Slot slot, ItemInstance item) {
	player->equipment[slot] = item;
	player->equipped[slot] = true;
}

int rarity_index(const GameData *data, const char *rarity_id) {
	for (int i = 0; i < data->balance.rarity_count; i++) {
		if (str_eq(data->balance.rarities[i].id, rarity_id)) {
			return i;
		}
	}
	fatal("unknown rarity in a test: %s", rarity_id);
}

void write_file(const char *path, const char *text) {
	if (!fs_write_text_atomic(path, text)) {
		test_fail(__FILE__, __LINE__, "cannot write %s", path);
	}
}

void write_json_file(const char *path, const JsonValue *document) {
	char *text = json_dump(document, false);
	write_file(path, text);
	free(text);
}

void copy_file(const char *from, const char *to) {
	char *text = read_file(from);
	if (text != NULL) {
		write_file(to, text);
		free(text);
	}
}

const char *path_in(const char *directory, const char *name) { return fmt("%s/%s", directory, name); }
