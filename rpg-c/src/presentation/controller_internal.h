// Private to controller.c and controller_body.c: the result caches and the helpers both files share.
#ifndef RPG_PRESENTATION_CONTROLLER_INTERNAL_H
#define RPG_PRESENTATION_CONTROLLER_INTERNAL_H

#include "application/commands.h"
#include "presentation/controller.h"

// A list of heap strings that owns its items.
typedef struct {
	char **items;
	size_t count;
	size_t capacity;
} StrList;

// Takes ownership of `owned` and returns it.
const char *str_list_keep(StrList *list, char *owned);
void str_list_clear(StrList *list);

// Body lines with their colours (NULL for the default colour). Both arrays are owned.
typedef struct {
	char **text;
	char **color;
	size_t count;
	size_t capacity;
} Lines;

// Takes ownership of `owned_text`; `color` is copied.
void lines_push(Lines *lines, char *owned_text, const char *color);
void lines_clear(Lines *lines);

// What a menu option does. Python binds a closure to each option; C stores the kind of action and its argument.
typedef enum {
	ACTION_GO,
	ACTION_STEP,
	ACTION_CHOOSE_LANGUAGE,
	ACTION_OPEN_LANGUAGE,
	ACTION_TOGGLE_AUTO_EQUIP,
	ACTION_CYCLE_BATTLE_SPEED,
	ACTION_QUIT,
	ACTION_NEW_RUN,
	ACTION_CHOOSE_DIFFICULTY,
	ACTION_CHOOSE_VOCATION,
	ACTION_START_RUN,
	ACTION_CONTINUE,
	ACTION_ASK_QUANTITY,
	ACTION_SAVE_AND_QUIT,
	ACTION_OPEN_COMPARE,
	ACTION_OPEN_SLOT,
	ACTION_EQUIP_COMPARED,
	ACTION_UNEQUIP_SLOT,
	ACTION_START_AUTO_BATTLE,
} ActionKind;

typedef struct {
	ActionKind kind;
	View target;     // go
	Command command; // step
	Id id;           // locale, difficulty, vocation, potion or auto-battle mode
	int64_t uid;     // open compare
	Slot slot;       // open slot
	bool enabled;    // start run: auto-equip on/off
} Action;

typedef struct {
	MenuOption *options;
	Action *actions;
	size_t count;
	size_t capacity;
	StrList strings;
} Menu;

struct ControllerCache {
	// Temporary strings of the call in progress (nested translations); emptied when a public function starts.
	StrList scratch;
	Menu menu;
	Lines body;
	Lines colors;
	char *title;
	char *header;
	char *prompt;
	char *translation;
	MonsterView monster;
	StrList monster_strings;
	PlayerView player;
	StrList player_strings;
};

// Owned translation with params: `CT(c, "hud.gold", P_INT("gold", 5))`.
#define CT(controller, key, ...) TR(&(controller)->translator, (key), __VA_ARGS__)
// Owned translation without params.
char *controller_text(const Controller *controller, const char *key);
// Hands `owned` to the scratch list: valid until the public call in progress returns.
const char *controller_keep(Controller *controller, char *owned);
// Scratch translation of the key `prefix` + `id` ("vocation." + "mage").
const char *controller_label(Controller *controller, const char *prefix, const char *id);
// "burn(2) stun(1)"; owned.
char *controller_status_text(Controller *controller, const StatusList *statuses);
// Appends the translated names of the elements a creature is weak (or, with `weak` false, strong) against,
// separated by ", ". Returns how many there are.
int controller_resistance_names(Controller *controller, StrBuf *out, const MonsterDef *creature, bool weak);

// The open run; a screen that needs one without it is a bug.
RunState *controller_state(Controller *controller);
// The bag item being compared, or NULL when it is gone.
const ItemInstance *controller_compared_item(Controller *controller);
// True when the player's vocation can wear the item.
bool controller_can_use(Controller *controller, const ItemInstance *item);

// controller_body.c
void controller_build_body(Controller *controller, Lines *out);
// Owned.
char *controller_compare_title(Controller *controller);

#endif
