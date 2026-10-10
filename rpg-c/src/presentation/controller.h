// Framework-independent UI state machine: which screen is shown, its options and what each key does.
//
// The terminal renderer only draws this controller and forwards keys to controller_press(). The other ports
// implement the same controller, which is what keeps the interfaces practically identical (docs/tui.md).
//
// String ownership: the controller owns every string and array it hands out; the caller never frees anything.
// Each query (title, options, body lines, ...) keeps its own result, which stays valid until the same query is
// called again or the state changes (controller_press, controller_auto_battle_step, controller_run_auto_battle,
// controller_free). A renderer can therefore call every query once per frame and use all the results together.
#ifndef RPG_PRESENTATION_CONTROLLER_H
#define RPG_PRESENTATION_CONTROLLER_H

#include "application/game_session.h"
#include "infrastructure/i18n.h"
#include "infrastructure/repositories.h"

#define MAX_LOG_LINES 50
#define MAX_NAME_LENGTH 16
#define MAX_QUANTITY_DIGITS 2
// PAGE_SIZE in the reference; that name is taken by <limits.h> on some C libraries.
#define LINES_PER_PAGE 10
// Auto-battle pace (docs/tui.md): one turn every 600 ms at 1x, 300 ms at 2x; instant with --no-anim.
#define AUTO_BATTLE_BASE_MS 600
// Safety net: a fight that somehow never ends hands control back to the player.
#define MAX_AUTO_BATTLE_TURNS 10000

typedef enum {
	VIEW_LANGUAGE,
	VIEW_TITLE,
	VIEW_SETTINGS,
	VIEW_DIFFICULTY,
	VIEW_NAME,
	VIEW_VOCATION,
	VIEW_AUTO_EQUIP,
	VIEW_MERCHANT,
	VIEW_BUY_POTIONS,
	VIEW_QUANTITY,
	VIEW_SELL,
	VIEW_EQUIPMENT,
	VIEW_COMPARE,
	VIEW_EQUIPPED_SLOT,
	VIEW_STOCK,
	VIEW_CHARACTER,
	VIEW_BATTLE,
	VIEW_SPELLS,
	VIEW_POTIONS,
	VIEW_AUTO_BATTLE,
	VIEW_VICTORY,
	VIEW_GAME_OVER,
	VIEW_HALL_OF_FAME,
	VIEW_BESTIARY,
	VIEW_ACHIEVEMENTS,
	VIEW_COUNT
} View;

const char *view_name(View view);
// Informative screens page their lines (N/P) and hide the combat log.
bool view_is_paged(View view);
// Screens where keys are typed into `input_buffer` (the name, a potion quantity).
bool view_is_text_input(View view);

// One selectable option. `color` is a rarity id, an element name, a semantic style (render.h) or "green"; NULL
// means the default colour. `detail` (the equipment screen's score delta, "" otherwise) is drawn after the label
// with its own colour.
typedef struct {
	char key[4];
	const char *label;
	const char *color;
	const char *detail;
	const char *detail_color;
} MenuOption;

typedef struct {
	const char *name;
	const char *creature_id;
	int64_t hp;
	int64_t max_hp;
	bool is_boss;
	const char *enemy_class;
	// The element of the monster's main attack (it colours the art).
	const char *element;
	const char *details;
} MonsterView;

typedef struct {
	const char *summary;
	const char *gold;
	int64_t hp;
	int64_t max_hp;
	int64_t mp;
	int64_t max_mp;
	const char *xp;
	const char *statuses;
} PlayerView;

// The collaborators of the controller. Every pointer is borrowed and must outlive the controller.
typedef struct {
	const GameData *data;
	// Where settings.json lives (the repositories carry their own directory).
	const char *data_dir;
	Repositories repositories;
	Clock clock;
	const char *version;
	// --seed: every new run uses it. Without it each run asks `seed_source`.
	bool has_seed;
	int64_t seed;
	// --lang: wins over the saved language and skips the first-launch language screen. NULL when not given.
	const char *locale_override;
	// NULL = random_seed.
	int64_t (*seed_source)(void);
} Services;

struct ControllerCache;

typedef struct {
	// ── state the renderer and the tests read ───────────────────────────────
	Services services;
	// The loaded settings; every change from the Settings screen is saved at once.
	Settings settings;
	// The current language is `translator.locale`.
	Translator translator;
	View view;
	// `session` is only meaningful while `has_session` is set.
	bool has_session;
	GameSession session;
	// The combat log, oldest line first.
	char *log[MAX_LOG_LINES];
	size_t log_count;
	// The error of the last key press ("" when there is none).
	char *message;
	char input_buffer[NAME_SIZE];
	bool exit_requested;
	// Animations triggered by the last step: "hurt" (the monster took damage) and "attack" (it attacked), in event
	// order. The renderer consumes them by setting `animation_cue_count` back to 0.
	const char **animation_cues;
	size_t animation_cue_count;
	size_t animation_cue_capacity;
	int64_t page;
	// While set, key presses are ignored and the renderer drives the fight with controller_auto_battle_step().
	bool auto_battle_active;

	// ── selections carried between screens (internal) ───────────────────────
	View language_return;
	Id difficulty;
	char name[NAME_SIZE];
	Id vocation;
	Id potion_id;
	int64_t compare_uid;
	Slot slot;
	Id auto_mode;
	int64_t auto_turns;
	// The results handed out by the queries (see "String ownership" above).
	struct ControllerCache *cache;
} Controller;

// A seed from the OS random source (presentation-level randomness only; the engine never sees it).
int64_t random_seed(void);

// Loads the settings and opens the title screen (or the language screen on the first launch). An unreadable
// settings file stops the program, like every unreadable save or profile met later.
void controller_init(Controller *controller, const Services *services);
// Releases everything, including an open session (without saving it: see controller_save_session).
void controller_free(Controller *controller);
// Persists the open run, if any (what the renderer does when the player closes the window).
void controller_save_session(Controller *controller);
// Drops the open run without saving it.
void controller_close_session(Controller *controller);

// Translates a key. The result is replaced by the next controller_t call (a previous result may be one of the
// params of that call).
const char *controller_t(Controller *controller, const char *key, const Param *params, size_t count);
// `CONTROLLER_T(c, "app.resize", P_INT("columns", 100), P_INT("rows", 30))`, at least one param.
#define CONTROLLER_T(controller, key, ...)                                                                             \
	controller_t(                                                                                                      \
	    (controller), (key), (const Param[]){__VA_ARGS__}, sizeof((const Param[]){__VA_ARGS__}) / sizeof(Param))

// ── queries used by renderers ───────────────────────────────────────────────
const char *controller_title(Controller *controller);
const MenuOption *controller_options(Controller *controller, size_t *count);
// Informative lines shown above the options (paged for long lists).
const char *const *controller_body_lines(Controller *controller, size_t *count);
// Colour of each line of controller_body_lines(): a semantic style from render.h, a rarity id or NULL.
const char *const *controller_body_colors(Controller *controller, size_t *count);
// "> text_" on a text input screen, NULL elsewhere.
const char *controller_input_prompt(Controller *controller);
const char *controller_header(Controller *controller);
// NULL when there is no monster / no run.
const MonsterView *controller_monster_view(Controller *controller);
const PlayerView *controller_player_view(Controller *controller);

// ── input ───────────────────────────────────────────────────────────────────
// `key` is a short name: "1", "a", "enter", "escape", "backspace", "up"... or one typed character (UTF-8).
void controller_press(Controller *controller, const char *key);

// ── auto-battle (docs/game-design.md §13, docs/tui.md) ──────────────────────
int64_t controller_auto_battle_interval_ms(const Controller *controller);
// Plays one auto-battle turn. Returns true while the fight goes on (the renderer's timer keeps ticking).
bool controller_auto_battle_step(Controller *controller);
// Instant mode (--no-anim): plays the whole fight at once.
void controller_run_auto_battle(Controller *controller);

#endif
