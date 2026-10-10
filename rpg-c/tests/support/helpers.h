// Shared test helpers, mirroring the reference conftest (calm data, test items, engines, temp dirs, fake clock).
#ifndef RPG_TESTS_SUPPORT_HELPERS_H
#define RPG_TESTS_SUPPORT_HELPERS_H

#include "application/engine.h"
#include "application/events.h"
#include "application/ports.h"
#include "domain/definitions.h"
#include "domain/entities.h"
#include "domain/json_types.h"
#include "support/test.h"

#include <string.h>

#define TEST_PATH_SIZE 1024

// The shared game data, loaded once from the embedded assets. Never modify it: take a copy with data_copy().
const GameData *test_data(void);
// A deep copy whose rows a test may edit freely (`copy.balance.enemy_classes[ENEMY_BOSS].dodge = 0`).
// Release it with game_data_free().
GameData data_copy(void);
// Adds deterministic test items (test_helmet, test_ring, test_axe, test_rod) without touching the shared files.
void add_test_items(GameData *data);
// No monster dodge, parry, crit or heal, and no elites: the pre-M8 fight, for tests of other mechanics.
void calm(GameData *data);
// Every non-boss monster spawns as an elite.
void all_elites(GameData *data);
// Appends one item definition and returns it for editing.
ItemDef *add_item(GameData *data, const char *id, const char *name, Slot slot, const char *type);

// A run in the merchant phase ("Tester"). The data must outlive the engine; release with engine_free().
GameEngine new_engine(const GameData *data, const char *vocation, const char *difficulty, int64_t seed);
GameEngine new_engine_auto_equip(const GameData *data, const char *vocation, const char *difficulty, int64_t seed);
// Steps one command and returns its events in a list owned by the helper (valid until the next call).
const EventList *step(GameEngine *engine, Command command);
// Leaves the merchant and returns the spawned monster.
MonsterInstance *fight(GameEngine *engine);
// Replaces the monster's attacks by one fixed attack and makes it practically unkillable.
void fixed_attack(MonsterInstance *monster, int64_t damage, Element element, const StatusOnHit *status);
// A bag/equipment item with no affixes.
ItemInstance make_item(int64_t uid, const char *item_id, const char *rarity, int64_t tier);

const Event *find_event(const EventList *events, const char *type);
int count_events(const EventList *events, const char *type);
bool has_event(const EventList *events, const char *type);
// True when the only event is `error` with this code.
bool is_error(const EventList *events, const char *code);

// Reads and parses a JSON file; fails the test and returns NULL when it cannot.
JsonValue *load_json_file(const char *path);
char *read_file(const char *path);

// A unique temporary directory. temp_dir_remove() deletes it with everything inside.
void temp_dir_create(char out[TEST_PATH_SIZE]);
void temp_dir_remove(const char *path);
void test_setenv(const char *name, const char *value);

// Starts at 2026-09-27T12:00:00Z and advances 10 seconds before every reading, like the reference FakeClock.
typedef struct {
	int64_t current;
} FakeClock;

Clock fake_clock(FakeClock *state);

// ── comparing with JSON literals (the C spelling of Python's `events == [{...}]`) ──────────────────────────────────
// printf into one of a few rotating static buffers: handy for building an expected JSON text inline.
const char *fmt(const char *format, ...) __attribute__((format(printf, 1, 2)));
// The event types joined by commas: "monster_parried,player_died".
const char *event_types(const EventList *events);
bool event_matches(const Event *event, const char *expected_json);
// True when some event of the list equals the JSON object.
bool events_contain(const EventList *events, const char *expected_json);
bool test_check_events(const char *file, int line, const EventList *events, const char *expected_json);
bool test_check_event(const char *file, int line, const Event *event, const char *expected_json);
bool test_check_command(const char *file, int line, Command actual, Command expected);
// Compares a document with a JSON text (object key order is ignored).
bool test_check_json(const char *file, int line, const JsonValue *actual, const char *expected_json);
bool test_check_json_equal(const char *file, int line, const JsonValue *actual, const JsonValue *expected);

#define CHECK_EVENTS(events, expected_json) test_check_events(__FILE__, __LINE__, (events), (expected_json))
#define CHECK_EVENT(event, expected_json) test_check_event(__FILE__, __LINE__, (event), (expected_json))
#define CHECK_COMMAND(actual, expected) test_check_command(__FILE__, __LINE__, (actual), (expected))
#define CHECK_JSON(actual, expected_json) test_check_json(__FILE__, __LINE__, (actual), (expected_json))
#define CHECK_JSON_EQUAL(actual, expected) test_check_json_equal(__FILE__, __LINE__, (actual), (expected))

bool items_equal(const ItemInstance *a, const ItemInstance *b);
ActiveStatus make_status(const char *status_id, int64_t turns, int64_t per_turn);
// Wears `item` directly, without the merchant rules.
void wear(Player *player, Slot slot, ItemInstance item);
// Index of a rarity in `balance.rarities` (the index of its weight in a RarityWeights).
int rarity_index(const GameData *data, const char *rarity_id);

// Writes a text file, creating its parent directories; fails the test when it cannot.
void write_file(const char *path, const char *text);
void write_json_file(const char *path, const JsonValue *document);
void copy_file(const char *from, const char *to);
// `<directory>/<name>` in one of the rotating fmt() buffers.
const char *path_in(const char *directory, const char *name);

#endif
