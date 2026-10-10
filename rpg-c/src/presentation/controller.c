// The state machine itself: input, menus, actions and the HUD queries. The informative screen bodies (character
// sheet, equipment comparison, bestiary...) live in controller_body.c.

// Asks the Windows C runtime to declare rand_s (it must come before <stdlib.h>).
#define _CRT_RAND_S

#include "application/auto_battle.h"
#include "application/loot.h"
#include "application/merchant.h"
#include "domain/character.h"
#include "domain/formulas.h"
#include "presentation/controller_internal.h"
#include "presentation/event_text.h"
#include "presentation/render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ERROR_SIZE 256
#define KEY_BUFFER 24

static const int64_t BATTLE_SPEEDS[] = {1, 2};

static const char *const VIEW_NAMES[VIEW_COUNT] = {
    [VIEW_LANGUAGE] = "language",
    [VIEW_TITLE] = "title",
    [VIEW_SETTINGS] = "settings",
    [VIEW_DIFFICULTY] = "difficulty",
    [VIEW_NAME] = "name",
    [VIEW_VOCATION] = "vocation",
    [VIEW_AUTO_EQUIP] = "auto_equip",
    [VIEW_MERCHANT] = "merchant",
    [VIEW_BUY_POTIONS] = "buy_potions",
    [VIEW_QUANTITY] = "quantity",
    [VIEW_SELL] = "sell",
    [VIEW_EQUIPMENT] = "equipment",
    [VIEW_COMPARE] = "compare",
    [VIEW_EQUIPPED_SLOT] = "equipped_slot",
    [VIEW_STOCK] = "stock",
    [VIEW_CHARACTER] = "character",
    [VIEW_BATTLE] = "battle",
    [VIEW_SPELLS] = "spells",
    [VIEW_POTIONS] = "potions",
    [VIEW_AUTO_BATTLE] = "auto_battle",
    [VIEW_VICTORY] = "victory",
    [VIEW_GAME_OVER] = "game_over",
    [VIEW_HALL_OF_FAME] = "hall_of_fame",
    [VIEW_BESTIARY] = "bestiary",
    [VIEW_ACHIEVEMENTS] = "achievements",
};

const char *view_name(View view) { return VIEW_NAMES[view]; }

bool view_is_paged(View view) {
	return view == VIEW_HALL_OF_FAME || view == VIEW_BESTIARY || view == VIEW_ACHIEVEMENTS || view == VIEW_CHARACTER;
}

bool view_is_text_input(View view) { return view == VIEW_NAME || view == VIEW_QUANTITY; }

static bool view_is_battle(View view) {
	return view == VIEW_BATTLE || view == VIEW_SPELLS || view == VIEW_POTIONS || view == VIEW_AUTO_BATTLE;
}

// ── string lists ────────────────────────────────────────────────────────────

const char *str_list_keep(StrList *list, char *owned) {
	VEC_PUSH(list->items, list->count, list->capacity, owned);
	return owned;
}

void str_list_clear(StrList *list) {
	for (size_t i = 0; i < list->count; i++) {
		free(list->items[i]);
	}
	list->count = 0;
}

static void str_list_free(StrList *list) {
	str_list_clear(list);
	free(list->items);
	memset(list, 0, sizeof(*list));
}

void lines_push(Lines *lines, char *owned_text, const char *color) {
	if (lines->count == lines->capacity) {
		lines->capacity = lines->capacity == 0 ? 16 : lines->capacity * 2;
		lines->text = xrealloc(lines->text, lines->capacity * sizeof(char *));
		lines->color = xrealloc(lines->color, lines->capacity * sizeof(char *));
	}
	lines->text[lines->count] = owned_text;
	lines->color[lines->count] = color == NULL ? NULL : xstrdup(color);
	lines->count++;
}

void lines_clear(Lines *lines) {
	for (size_t i = 0; i < lines->count; i++) {
		free(lines->text[i]);
		free(lines->color[i]);
	}
	lines->count = 0;
}

static void lines_free(Lines *lines) {
	lines_clear(lines);
	free(lines->text);
	free(lines->color);
	memset(lines, 0, sizeof(*lines));
}

static void menu_clear(Menu *menu) {
	str_list_clear(&menu->strings);
	menu->count = 0;
}

// ── i18n ────────────────────────────────────────────────────────────────────

// Every public function starts here: the temporaries of the previous call are no longer needed.
static void begin(Controller *controller) { str_list_clear(&controller->cache->scratch); }

char *controller_text(const Controller *controller, const char *key) {
	return translate(&controller->translator, key, NULL, 0);
}

const char *controller_keep(Controller *controller, char *owned) {
	return str_list_keep(&controller->cache->scratch, owned);
}

const char *controller_label(Controller *controller, const char *prefix, const char *id) {
	StrBuf key = {0};
	sb_append(&key, prefix);
	sb_append(&key, id);
	char *text = controller_text(controller, key.data);
	sb_free(&key);
	return controller_keep(controller, text);
}

const char *controller_t(Controller *controller, const char *key, const Param *params, size_t count) {
	// Translated before the previous result is released: that result may be one of the params.
	char *text = translate(&controller->translator, key, params, count);
	free(controller->cache->translation);
	controller->cache->translation = text;
	return text;
}

static void set_locale(Controller *controller, const char *locale) {
	Translator next;
	if (!translator_init(&next, locale)) {
		fatal("unsupported locale: %s", locale);
	}
	if (controller->translator.fallback != NULL) {
		translator_free(&controller->translator);
	}
	controller->translator = next;
}

// Takes ownership of `owned`.
static void set_message(Controller *controller, char *owned) {
	free(controller->message);
	controller->message = owned;
}

static void log_clear(Controller *controller) {
	for (size_t i = 0; i < controller->log_count; i++) {
		free(controller->log[i]);
	}
	controller->log_count = 0;
}

// Takes ownership of `owned`; the oldest line leaves when the log is full.
static void log_push(Controller *controller, char *owned) {
	if (controller->log_count == MAX_LOG_LINES) {
		free(controller->log[0]);
		memmove(controller->log, controller->log + 1, (MAX_LOG_LINES - 1) * sizeof(char *));
		controller->log_count--;
	}
	controller->log[controller->log_count++] = owned;
}

// ── lifecycle ───────────────────────────────────────────────────────────────

int64_t random_seed(void) {
	uint32_t seed = 0;
#ifdef _WIN32
	unsigned int value = 0;
	if (rand_s(&value) == 0) {
		return (int64_t)value;
	}
#else
	FILE *source = fopen("/dev/urandom", "rb");
	if (source != NULL) {
		size_t read = fread(&seed, sizeof(seed), 1, source);
		fclose(source);
		if (read == 1) {
			return (int64_t)seed;
		}
	}
#endif
	// No OS random source: the clock still gives a different run each time.
	seed = (uint32_t)time(NULL) * 2654435761u + (uint32_t)clock();
	return (int64_t)seed;
}

void controller_init(Controller *controller, const Services *services) {
	memset(controller, 0, sizeof(*controller));
	controller->services = *services;
	controller->cache = xcalloc(1, sizeof(struct ControllerCache));
	char error[ERROR_SIZE];
	if (settings_load(services->data_dir, &controller->settings, error, sizeof(error)) != LOAD_OK) {
		fatal("%s", error);
	}
	const char *override = services->locale_override;
	bool chosen = (override != NULL && override[0] != '\0') || controller->settings.has_locale;
	if (override != NULL && override[0] != '\0') {
		set_locale(controller, override);
	} else {
		set_locale(controller, controller->settings.has_locale ? controller->settings.locale : DEFAULT_LOCALE);
	}
	controller->view = chosen ? VIEW_TITLE : VIEW_LANGUAGE;
	controller->language_return = VIEW_TITLE;
	controller->message = xstrdup("");
	id_set(controller->difficulty, "normal");
	controller->slot = SLOT_WEAPON;
	id_set(controller->auto_mode, AUTO_BATTLE_MELEE);
}

void controller_close_session(Controller *controller) {
	if (controller->has_session) {
		session_free(&controller->session);
		controller->has_session = false;
	}
}

void controller_save_session(Controller *controller) {
	if (controller->has_session) {
		session_save_and_quit(&controller->session);
	}
}

void controller_free(Controller *controller) {
	struct ControllerCache *cache = controller->cache;
	controller_close_session(controller);
	translator_free(&controller->translator);
	log_clear(controller);
	free(controller->message);
	free((void *)controller->animation_cues);
	str_list_free(&cache->scratch);
	str_list_free(&cache->menu.strings);
	free(cache->menu.options);
	free(cache->menu.actions);
	lines_free(&cache->body);
	lines_free(&cache->colors);
	free(cache->title);
	free(cache->header);
	free(cache->prompt);
	free(cache->translation);
	str_list_free(&cache->monster_strings);
	str_list_free(&cache->player_strings);
	free(cache);
	memset(controller, 0, sizeof(*controller));
}

RunState *controller_state(Controller *controller) {
	if (!controller->has_session) {
		fatal("view %s needs an active run", view_name(controller->view));
	}
	return &controller->session.engine.state;
}

// ── shared pieces of the screens ────────────────────────────────────────────

char *controller_status_text(Controller *controller, const StatusList *statuses) {
	StrBuf out = {0};
	for (int i = 0; i < statuses->count; i++) {
		const ActiveStatus *status = &statuses->items[i];
		sb_appendf(&out, "%s%s(%lld)", i > 0 ? " " : "", controller_label(controller, "status.", status->status_id),
		    (long long)status->turns);
	}
	sb_append(&out, "");
	return sb_take(&out);
}

int controller_resistance_names(Controller *controller, StrBuf *out, const MonsterDef *creature, bool weak) {
	int count = 0;
	for (int element = 0; element < ELEMENT_COUNT; element++) {
		int64_t resistance = creature->resistances[element];
		if (weak ? resistance > DEFAULT_RESISTANCE : resistance < DEFAULT_RESISTANCE) {
			sb_append(out, count > 0 ? ", " : "");
			sb_append(out, controller_label(controller, "element.", element_name((Element)element)));
			count++;
		}
	}
	return count;
}

const ItemInstance *controller_compared_item(Controller *controller) {
	const Player *player = &controller_state(controller)->player;
	int index = player_bag_index(player, controller->compare_uid);
	return index < 0 ? NULL : &player->bag[index];
}

bool controller_can_use(Controller *controller, const ItemInstance *item) {
	const GameData *data = controller->services.data;
	const VocationDef *vocation = data_vocation(data, controller_state(controller)->player.vocation_id);
	return can_use(data_item(data, item->item_id), vocation);
}

// Score of `item` minus the score of what is equipped in its slot (0 for an empty slot).
static int64_t score_delta(Controller *controller, const ItemInstance *item) {
	const GameData *data = controller->services.data;
	const ItemInstance *equipped =
	    player_equipped(&controller_state(controller)->player, data_item(data, item->item_id)->slot);
	return item_score(item, data) - (equipped == NULL ? 0 : item_score(equipped, data));
}

// ── queries used by renderers ───────────────────────────────────────────────

static char *title_text(Controller *controller) {
	switch (controller->view) {
	case VIEW_LANGUAGE:
		return controller_text(controller, "language.title");
	case VIEW_TITLE:
		return controller_text(controller, "app.title");
	case VIEW_SETTINGS:
		return controller_text(controller, "settings.title");
	case VIEW_DIFFICULTY:
		return controller_text(controller, "new_run.difficulty");
	case VIEW_NAME:
		return controller_text(controller, "new_run.name");
	case VIEW_VOCATION:
		return controller_text(controller, "new_run.vocation");
	case VIEW_AUTO_EQUIP:
		return controller_text(controller, "new_run.auto_equip");
	case VIEW_MERCHANT: {
		int64_t round_number = controller->has_session ? controller->session.engine.state.round : 0;
		if (round_number == 0) {
			return controller_text(controller, "merchant.title_start");
		}
		return CT(controller, "merchant.title", P_INT("round", round_number));
	}
	case VIEW_BUY_POTIONS:
		return controller_text(controller, "merchant.buy_potions");
	case VIEW_QUANTITY:
		return CT(controller, "merchant.quantity",
		    P_STR("name", data_potion(controller->services.data, controller->potion_id)->name));
	case VIEW_SELL:
		return controller_text(controller, "merchant.sell_items");
	case VIEW_EQUIPMENT:
		return controller_text(controller, "merchant.equipment");
	case VIEW_COMPARE:
		return controller_compare_title(controller);
	case VIEW_EQUIPPED_SLOT:
		return xstrdup(controller_label(controller, "slot.", slot_name(controller->slot)));
	case VIEW_STOCK:
		return controller_text(controller, "merchant.stock");
	case VIEW_CHARACTER:
		return controller_text(controller, "merchant.character");
	case VIEW_BATTLE:
		return controller_text(controller, "battle.title");
	case VIEW_SPELLS:
		return controller_text(controller, "battle.spells");
	case VIEW_POTIONS:
		return controller_text(controller, "battle.potions");
	case VIEW_AUTO_BATTLE:
		return controller_text(controller, "auto_battle.title");
	case VIEW_VICTORY:
		return controller_text(controller, "victory.title");
	case VIEW_GAME_OVER: {
		bool won = controller->has_session && controller->session.engine.state.won;
		return controller_text(controller, won ? "gameover.title_won" : "gameover.title");
	}
	case VIEW_HALL_OF_FAME:
		return controller_text(controller, "menu.hall_of_fame");
	case VIEW_BESTIARY:
		return controller_text(controller, "menu.bestiary");
	case VIEW_ACHIEVEMENTS:
		return controller_text(controller, "menu.achievements");
	case VIEW_COUNT:
		break;
	}
	fatal("unknown view %d", (int)controller->view);
}

const char *controller_title(Controller *controller) {
	begin(controller);
	char *title = title_text(controller);
	free(controller->cache->title);
	controller->cache->title = title;
	return title;
}

const char *const *controller_body_lines(Controller *controller, size_t *count) {
	begin(controller);
	Lines *body = &controller->cache->body;
	controller_build_body(controller, body);
	*count = body->count;
	return (const char *const *)body->text;
}

const char *const *controller_body_colors(Controller *controller, size_t *count) {
	begin(controller);
	Lines *colors = &controller->cache->colors;
	controller_build_body(controller, colors);
	*count = colors->count;
	return (const char *const *)colors->color;
}

const char *controller_input_prompt(Controller *controller) {
	if (!view_is_text_input(controller->view)) {
		return NULL;
	}
	StrBuf prompt = {0};
	sb_appendf(&prompt, "> %s_", controller->input_buffer);
	free(controller->cache->prompt);
	controller->cache->prompt = sb_take(&prompt);
	return controller->cache->prompt;
}

const char *controller_header(Controller *controller) {
	begin(controller);
	char *header;
	if (!controller->has_session) {
		header = controller_text(controller, "app.subtitle");
	} else {
		const RunState *state = &controller->session.engine.state;
		const GameData *data = controller->services.data;
		int64_t tier = round_info(max_i64(1, state->round), &data->balance, data_tier_count(data)).tier + 1;
		const char *round_text = controller_keep(controller,
		    CT(controller, "hud.round", P_INT("round", state->round), P_INT("tier", tier),
		        P_STR("difficulty", controller_label(controller, "difficulty.", state->config.difficulty_id))));
		const char *seed_text = controller_keep(controller, CT(controller, "hud.seed", P_INT("seed", state->seed)));
		StrBuf out = {0};
		sb_appendf(&out, "%s · %s", round_text, seed_text);
		header = sb_take(&out);
	}
	free(controller->cache->header);
	controller->cache->header = header;
	return header;
}

const MonsterView *controller_monster_view(Controller *controller) {
	begin(controller);
	if (!controller->has_session || !controller->session.engine.state.has_monster) {
		return NULL;
	}
	struct ControllerCache *cache = controller->cache;
	const MonsterInstance *monster = &controller->session.engine.state.monster;
	const MonsterDef *creature = data_creature(controller->services.data, monster->creature_id);

	// The main attack is the heaviest one (ties go to the highest id); its element colours the art.
	Element main_element = ELEMENT_PHYSICAL;
	const MonsterAttack *main_attack = NULL;
	StrBuf details = {0};
	bool listed[ELEMENT_COUNT] = {false};
	for (int i = 0; i < monster->attack_count; i++) {
		const MonsterAttack *attack = &monster->attacks[i];
		if (main_attack == NULL || attack->weight > main_attack->weight ||
		    (attack->weight == main_attack->weight && strcmp(attack->id, main_attack->id) > 0)) {
			main_attack = attack;
			main_element = attack->element;
		}
		if (!listed[attack->element]) {
			listed[attack->element] = true;
			sb_append(&details, details.length > 0 ? " · " : "");
			sb_append(&details, controller_label(controller, "element.", element_name(attack->element)));
		}
	}
	StrBuf weak = {0};
	if (controller_resistance_names(controller, &weak, creature, true) > 0) {
		sb_append(&details, " · ");
		sb_append(&details, controller_keep(controller, CT(controller, "hud.weak", P_STR("elements", weak.data))));
	}
	sb_free(&weak);
	if (monster->statuses.count > 0) {
		sb_append(&details, " · ");
		sb_append(&details, controller_keep(controller, controller_status_text(controller, &monster->statuses)));
	}
	sb_append(&details, "");

	str_list_clear(&cache->monster_strings);
	cache->monster = (MonsterView){
	    .name = creature->name,
	    .creature_id = creature->id,
	    .hp = monster->hp,
	    .max_hp = monster->max_hp,
	    .is_boss = monster->is_boss,
	    .enemy_class = enemy_class_name(monster->enemy_class),
	    .element = element_name(main_element),
	    .details = str_list_keep(&cache->monster_strings, sb_take(&details)),
	};
	return &cache->monster;
}

const PlayerView *controller_player_view(Controller *controller) {
	begin(controller);
	if (!controller->has_session) {
		return NULL;
	}
	struct ControllerCache *cache = controller->cache;
	const Player *player = &controller->session.engine.state.player;
	CharacterSheet sheet = build_sheet(player, controller->services.data);
	char *summary = CT(controller, "hud.player", P_STR("name", player->name),
	    P_STR("vocation", controller_label(controller, "vocation.", player->vocation_id)),
	    P_INT("level", player->level), P_INT("magicLevel", player->magic_level));
	char *gold = CT(controller, "hud.gold", P_INT("gold", player->gold));
	char *xp = CT(controller, "hud.xp", P_INT("xp", player->xp), P_INT("next", xp_for_level(player->level + 1)));
	char *statuses = controller_status_text(controller, &player->statuses);

	str_list_clear(&cache->player_strings);
	cache->player = (PlayerView){
	    .summary = str_list_keep(&cache->player_strings, summary),
	    .gold = str_list_keep(&cache->player_strings, gold),
	    .hp = player->hp,
	    .max_hp = sheet.max_hp,
	    .mp = player->mp,
	    .max_mp = sheet.max_mp,
	    .xp = str_list_keep(&cache->player_strings, xp),
	    .statuses = str_list_keep(&cache->player_strings, statuses),
	};
	return &cache->player;
}

// ── actions ─────────────────────────────────────────────────────────────────

static void record(Controller *controller, const EventList *events, const AchievementList *unlocked) {
	const RunState *state = controller_state(controller);
	EventFormatter formatter = {.data = controller->services.data, .translator = &controller->translator};
	controller->animation_cue_count = 0;
	for (size_t i = 0; i < events->count; i++) {
		const Event *event = &events->items[i];
		char *text = event_format(&formatter, event, state);
		if (event_is(event, "error")) {
			set_message(controller, text);
			continue;
		}
		log_push(controller, text);
		const EventField *damage = event_field(event, "damage");
		const char *cue = NULL;
		if ((event_is(event, "player_attacked") || event_is(event, "spell_cast")) && damage != NULL &&
		    damage->integer != 0) {
			cue = "hurt";
		} else if (event_is(event, "monster_attacked")) {
			cue = "attack";
		}
		if (cue != NULL) {
			VEC_PUSH(
			    controller->animation_cues, controller->animation_cue_count, controller->animation_cue_capacity, cue);
		}
	}
	for (size_t i = 0; unlocked != NULL && i < unlocked->count; i++) {
		StrBuf key = {0};
		sb_appendf(&key, "achievement.%s.name", unlocked->items[i]->id);
		char *name = controller_text(controller, key.data);
		log_push(controller, CT(controller, "achievement.unlocked", P_STR("name", name)));
		free(name);
		sb_free(&key);
	}
}

static void step(Controller *controller, Command command) {
	const RunState *state = controller_state(controller);
	EventList events = {0};
	AchievementList unlocked = {0};
	session_step(&controller->session, &command, &events, &unlocked);
	record(controller, &events, &unlocked);
	events_free(&events);
	achievement_list_free(&unlocked);
	if (state->phase == PHASE_BATTLE) {
		controller->view = VIEW_BATTLE;
	} else if (state->phase == PHASE_GAME_OVER) {
		controller->view = VIEW_GAME_OVER;
	} else if (state->phase == PHASE_VICTORY) {
		controller->view = VIEW_VICTORY;
	} else if (view_is_battle(controller->view) || controller->view == VIEW_VICTORY) {
		controller->view = VIEW_MERCHANT;
	}
}

static void save_settings(Controller *controller, Settings settings) {
	controller->settings = settings;
	settings_save(controller->services.data_dir, &settings);
}

static void cycle_battle_speed(Controller *controller) {
	size_t index = 0;
	for (size_t i = 0; i < ARRAY_LEN(BATTLE_SPEEDS); i++) {
		if (BATTLE_SPEEDS[i] == controller->settings.battle_speed) {
			index = i;
		}
	}
	Settings settings = controller->settings;
	settings.battle_speed = BATTLE_SPEEDS[(index + 1) % ARRAY_LEN(BATTLE_SPEEDS)];
	save_settings(controller, settings);
}

static void choose_language(Controller *controller, const char *locale) {
	set_locale(controller, locale);
	Settings settings = controller->settings;
	settings.has_locale = true;
	str_copy(settings.locale, LOCALE_SIZE, locale);
	save_settings(controller, settings);
	controller->view = controller->language_return;
}

static void start_run(Controller *controller, bool auto_equip) {
	const Services *services = &controller->services;
	int64_t seed =
	    services->has_seed ? services->seed : (services->seed_source != NULL ? services->seed_source() : random_seed());
	controller_close_session(controller);
	RunConfig config = run_config(controller->name, controller->vocation, controller->difficulty, auto_equip);
	EventList events = {0};
	char error[ERROR_SIZE];
	if (!session_start(&controller->session, services->data, &config, seed, services->repositories, services->clock,
	        services->version, &events, error, sizeof(error))) {
		fatal("%s", error);
	}
	controller->has_session = true;
	log_clear(controller);
	record(controller, &events, NULL);
	events_free(&events);
	controller->view = VIEW_MERCHANT;
}

static void continue_run(Controller *controller) {
	const Services *services = &controller->services;
	GameSession session;
	char error[ERROR_SIZE];
	LoadResult result = session_resume(
	    &session, services->data, services->repositories, services->clock, services->version, error, sizeof(error));
	if (result == LOAD_FAILED) {
		fatal("%s", error);
	}
	if (result == LOAD_MISSING) {
		return;
	}
	controller_close_session(controller);
	controller->session = session;
	controller->has_session = true;
	const RunState *state = &controller->session.engine.state;
	log_clear(controller);
	log_push(controller,
	    CT(controller, "menu.welcome_back", P_STR("name", state->player.name), P_INT("round", state->round)));
	controller->view = state->phase == PHASE_VICTORY ? VIEW_VICTORY : VIEW_MERCHANT;
}

static void save_and_quit(Controller *controller) {
	controller_save_session(controller);
	controller_close_session(controller);
	controller->view = VIEW_TITLE;
}

static void start_auto_battle(Controller *controller, const char *mode) {
	id_set(controller->auto_mode, mode);
	controller->auto_turns = 0;
	controller->auto_battle_active = true;
	controller->view = VIEW_BATTLE;
	log_push(controller,
	    CT(controller, "auto_battle.started", P_STR("mode", controller_label(controller, "auto_battle.", mode))));
}

static void run_action(Controller *controller, const Action *action) {
	switch (action->kind) {
	case ACTION_GO:
		controller->view = action->target;
		controller->page = 0;
		break;
	case ACTION_STEP:
		step(controller, action->command);
		break;
	case ACTION_CHOOSE_LANGUAGE:
		choose_language(controller, action->id);
		break;
	case ACTION_OPEN_LANGUAGE:
		controller->language_return = VIEW_SETTINGS;
		controller->view = VIEW_LANGUAGE;
		break;
	case ACTION_TOGGLE_AUTO_EQUIP: {
		Settings settings = controller->settings;
		settings.auto_equip = !settings.auto_equip;
		save_settings(controller, settings);
		break;
	}
	case ACTION_CYCLE_BATTLE_SPEED:
		cycle_battle_speed(controller);
		break;
	case ACTION_QUIT:
		controller->exit_requested = true;
		break;
	case ACTION_NEW_RUN:
		controller_close_session(controller);
		controller->view = VIEW_DIFFICULTY;
		break;
	case ACTION_CHOOSE_DIFFICULTY:
		id_set(controller->difficulty, action->id);
		controller->input_buffer[0] = '\0';
		controller->view = VIEW_NAME;
		break;
	case ACTION_CHOOSE_VOCATION:
		id_set(controller->vocation, action->id);
		controller->view = VIEW_AUTO_EQUIP;
		break;
	case ACTION_START_RUN:
		start_run(controller, action->enabled);
		break;
	case ACTION_CONTINUE:
		continue_run(controller);
		break;
	case ACTION_ASK_QUANTITY:
		id_set(controller->potion_id, action->id);
		controller->input_buffer[0] = '\0';
		controller->view = VIEW_QUANTITY;
		break;
	case ACTION_SAVE_AND_QUIT:
		save_and_quit(controller);
		break;
	case ACTION_OPEN_COMPARE:
		controller->compare_uid = action->uid;
		controller->view = VIEW_COMPARE;
		break;
	case ACTION_OPEN_SLOT:
		controller->slot = action->slot;
		controller->view = VIEW_EQUIPPED_SLOT;
		break;
	case ACTION_EQUIP_COMPARED:
		step(controller, cmd_equip(controller->compare_uid));
		controller->view = VIEW_EQUIPMENT;
		break;
	case ACTION_UNEQUIP_SLOT:
		step(controller, cmd_unequip(controller->slot));
		controller->view = VIEW_EQUIPMENT;
		break;
	case ACTION_START_AUTO_BATTLE:
		start_auto_battle(controller, action->id);
		break;
	}
}

// ── menus ───────────────────────────────────────────────────────────────────

static Action action_of(ActionKind kind) {
	Action action;
	memset(&action, 0, sizeof(action));
	action.kind = kind;
	return action;
}

static Action action_go(View target) {
	Action action = action_of(ACTION_GO);
	action.target = target;
	return action;
}

static Action action_step(Command command) {
	Action action = action_of(ACTION_STEP);
	action.command = command;
	return action;
}

static Action action_with_id(ActionKind kind, const char *id) {
	Action action = action_of(kind);
	id_set(action.id, id);
	return action;
}

// Appends an option; takes ownership of `label` and copies `color` (NULL for the default colour). The returned
// option is only valid until the next one is added.
static MenuOption *add_option(Controller *controller, const char *key, char *label, const char *color, Action action) {
	Menu *menu = &controller->cache->menu;
	if (menu->count == menu->capacity) {
		menu->capacity = menu->capacity == 0 ? 16 : menu->capacity * 2;
		menu->options = xrealloc(menu->options, menu->capacity * sizeof(MenuOption));
		menu->actions = xrealloc(menu->actions, menu->capacity * sizeof(Action));
	}
	MenuOption *option = &menu->options[menu->count];
	memset(option, 0, sizeof(*option));
	str_copy(option->key, sizeof(option->key), key);
	option->label = str_list_keep(&menu->strings, label);
	option->color = color == NULL ? NULL : str_list_keep(&menu->strings, xstrdup(color));
	option->detail = "";
	menu->actions[menu->count++] = action;
	return option;
}

static void add_text_option(Controller *controller, const char *key, const char *label_key, Action action) {
	add_option(controller, key, controller_text(controller, label_key), NULL, action);
}

// An option of a numbered choice: keys "1", "2"... in the order they are added.
static void add_numbered(Controller *controller, char *label, Action action) {
	char key[KEY_BUFFER];
	snprintf(key, sizeof(key), "%zu", controller->cache->menu.count + 1);
	add_option(controller, key, label, NULL, action);
}

// An option of a list screen: keys 1-9 then a-z (render.h), in the order they are added.
static MenuOption *add_listed(Controller *controller, char *label, const char *color, Action action) {
	char key[2] = {list_key(controller->cache->menu.count), '\0'};
	return add_option(controller, key, label, color, action);
}

static void add_back(Controller *controller, View target) {
	add_text_option(controller, "0", "menu.back", action_go(target));
}

// "Fighter — strong and sturdy": the translation of `prefix` + `id` and of its ".description".
static char *described_label(Controller *controller, const char *prefix, const char *id) {
	StrBuf key = {0};
	sb_appendf(&key, "%s%s.description", prefix, id);
	StrBuf label = {0};
	sb_appendf(&label, "%s — %s", controller_label(controller, prefix, id), controller_label(controller, key.data, ""));
	sb_free(&key);
	return sb_take(&label);
}

static void title_menu(Controller *controller) {
	const Repositories *repositories = &controller->services.repositories;
	SaveGame save;
	char error[ERROR_SIZE];
	LoadResult result = repositories->load_save(repositories->context, &save, error, sizeof(error));
	if (result == LOAD_FAILED) {
		fatal("%s", error);
	}
	if (result == LOAD_OK) {
		save_game_free(&save);
		add_text_option(controller, "1", "menu.continue", action_of(ACTION_CONTINUE));
	}
	add_text_option(controller, "2", "menu.new_run", action_of(ACTION_NEW_RUN));
	add_text_option(controller, "3", "menu.hall_of_fame", action_go(VIEW_HALL_OF_FAME));
	add_text_option(controller, "4", "menu.bestiary", action_go(VIEW_BESTIARY));
	add_text_option(controller, "5", "menu.achievements", action_go(VIEW_ACHIEVEMENTS));
	add_text_option(controller, "6", "menu.settings", action_go(VIEW_SETTINGS));
	add_text_option(controller, "0", "menu.quit", action_of(ACTION_QUIT));
}

static void settings_menu(Controller *controller) {
	const Settings *settings = &controller->settings;
	add_option(controller, "1",
	    CT(controller, "settings.language",
	        P_STR("language", controller_label(controller, "language.", controller->translator.locale))),
	    NULL, action_of(ACTION_OPEN_LANGUAGE));
	add_option(controller, "2",
	    CT(controller, "settings.auto_equip",
	        P_STR("state", controller_label(controller, settings->auto_equip ? "settings.on" : "settings.off", ""))),
	    NULL, action_of(ACTION_TOGGLE_AUTO_EQUIP));
	add_option(controller, "3", CT(controller, "settings.battle_speed", P_INT("speed", settings->battle_speed)), NULL,
	    action_of(ACTION_CYCLE_BATTLE_SPEED));
	add_back(controller, VIEW_TITLE);
}

static void auto_equip_menu(Controller *controller) {
	for (int choice = 0; choice < 2; choice++) {
		bool enabled = choice == 0;
		char *label = controller_text(controller, enabled ? "new_run.auto_equip_on" : "new_run.auto_equip_off");
		if (enabled == controller->settings.auto_equip) {
			StrBuf marked = {0};
			sb_appendf(&marked, "%s %s", label, controller_label(controller, "new_run.default", ""));
			free(label);
			label = sb_take(&marked);
		}
		Action action = action_of(ACTION_START_RUN);
		action.enabled = enabled;
		add_option(controller, enabled ? "1" : "2", label, NULL, action);
	}
	add_back(controller, VIEW_VOCATION);
}

static void merchant_menu(Controller *controller) {
	add_text_option(controller, "1", "merchant.buy_potions", action_go(VIEW_BUY_POTIONS));
	add_text_option(controller, "2", "merchant.sell_items", action_go(VIEW_SELL));
	add_text_option(controller, "3", "merchant.equipment", action_go(VIEW_EQUIPMENT));
	add_text_option(controller, "4", "merchant.stock", action_go(VIEW_STOCK));
	add_text_option(controller, "5", "merchant.character", action_go(VIEW_CHARACTER));
	add_text_option(controller, "0", "merchant.next_fight", action_step(cmd_next_fight()));
	add_text_option(controller, "q", "battle.save_quit", action_of(ACTION_SAVE_AND_QUIT));
}

// "Name [Rarity] · Slot · N gold": the label of an item that is sold or bought.
static char *item_label(Controller *controller, const char *template_key, const ItemInstance *item, int64_t gold) {
	const ItemDef *definition = data_item(controller->services.data, item->item_id);
	return CT(controller, template_key, P_STR("name", definition->name),
	    P_STR("rarity", controller_label(controller, "rarity.", item->rarity)),
	    P_STR("slot", controller_label(controller, "slot.", slot_name(definition->slot))), P_INT("gold", gold));
}

static void potion_shop(Controller *controller) {
	const GameData *data = controller->services.data;
	const RunState *state = controller_state(controller);
	for (int i = 0; i < data->potion_count; i++) {
		const PotionDef *potion = &data->potions[i];
		if (!potion_available(state, potion)) {
			continue;
		}
		char *label = CT(controller, "merchant.potion_option", P_STR("name", potion->name),
		    P_INT("price", potion->price), P_INT("count", counter_get(&state->player.potions, potion->id)));
		add_listed(controller, label, NULL, action_with_id(ACTION_ASK_QUANTITY, potion->id));
	}
}

static void sell_menu(Controller *controller) {
	const GameData *data = controller->services.data;
	const Player *player = &controller_state(controller)->player;
	for (int i = 0; i < player->bag_count; i++) {
		const ItemInstance *item = &player->bag[i];
		char *label = item_label(controller, "merchant.sell_option", item, item_value(item, data));
		add_listed(controller, label, item->rarity, action_step(cmd_sell_item(item->uid)));
	}
}

// Usable bag items first (keys 1..n, as in docs/tui.md), then the equipped slots.
static void equipment_menu(Controller *controller) {
	const GameData *data = controller->services.data;
	const Player *player = &controller_state(controller)->player;
	Menu *menu = &controller->cache->menu;
	for (int i = 0; i < player->bag_count; i++) {
		const ItemInstance *item = &player->bag[i];
		if (!controller_can_use(controller, item)) {
			continue;
		}
		const ItemDef *definition = data_item(data, item->item_id);
		int64_t level = required_level(item, data);
		char *label = CT(controller, "equipment.bag_option", P_STR("name", definition->name),
		    P_STR("rarity", controller_label(controller, "rarity.", item->rarity)),
		    P_STR("slot", controller_label(controller, "slot.", slot_name(definition->slot))), P_INT("level", level),
		    P_INT("score", item_score(item, data)));
		bool too_high = level > player->level;
		if (too_high) {
			StrBuf marked = {0};
			sb_appendf(&marked, "%s · %s", label,
			    controller_keep(controller, CT(controller, "equipment.requires_level", P_INT("level", level))));
			free(label);
			label = sb_take(&marked);
		}
		int64_t delta = score_delta(controller, item);
		char detail[DELTA_SIZE];
		format_delta(delta, detail);
		Action action = action_of(ACTION_OPEN_COMPARE);
		action.uid = item->uid;
		MenuOption *option = add_listed(controller, label, too_high ? STYLE_DIM : item->rarity, action);
		option->detail = str_list_keep(&menu->strings, xstrdup(detail));
		option->detail_color = delta_style(delta);
	}
	for (int i = 0; i < SLOT_COUNT; i++) {
		Slot slot = EQUIPMENT_SLOT_ORDER[i];
		const ItemInstance *equipped = player_equipped(player, slot);
		if (equipped == NULL) {
			continue;
		}
		char *label = CT(controller, "equipment.slot_option",
		    P_STR("slot", controller_label(controller, "slot.", slot_name(slot))),
		    P_STR("name", data_item(data, equipped->item_id)->name),
		    P_STR("rarity", controller_label(controller, "rarity.", equipped->rarity)));
		Action action = action_of(ACTION_OPEN_SLOT);
		action.slot = slot;
		add_listed(controller, label, equipped->rarity, action);
	}
}

static void stock_menu(Controller *controller) {
	const GameData *data = controller->services.data;
	const RunState *state = controller_state(controller);
	for (int i = 0; i < state->stock_count; i++) {
		const ItemInstance *item = &state->merchant_stock[i];
		char *label = item_label(controller, "merchant.stock_option", item, stock_price(item, data));
		add_listed(controller, label, item->rarity, action_step(cmd_buy_stock_item(i)));
	}
}

static void spell_menu(Controller *controller) {
	const GameData *data = controller->services.data;
	const Player *player = &controller_state(controller)->player;
	const VocationDef *vocation = data_vocation(data, player->vocation_id);
	for (int i = 0; i < vocation->spell_count; i++) {
		const SpellDef *spell = data_spell(data, vocation->spells[i]);
		int64_t uses = counter_get(&player->spell_uses, spell->id);
		const SpellLevelDef *level = spell_level_for_uses(uses, &data->balance);
		char *label = CT(controller, "battle.spell_option", P_STR("name", spell->name), P_STR("words", spell->words),
		    P_INT("mana", pct(spell->mana, level->mana_pct)), P_INT("level", level->level), P_INT("uses", uses));
		const char *color = spell->kind == SPELL_HEAL ? "green" : element_name(spell->element);
		add_listed(controller, label, color, action_step(cmd_cast(spell->id)));
	}
}

static void battle_potions(Controller *controller) {
	const GameData *data = controller->services.data;
	const Player *player = &controller_state(controller)->player;
	for (int i = 0; i < data->potion_count; i++) {
		const PotionDef *potion = &data->potions[i];
		int64_t owned = counter_get(&player->potions, potion->id);
		if (owned <= 0) {
			continue;
		}
		char *label = CT(controller, "battle.potion_option", P_STR("name", potion->name), P_INT("count", owned));
		add_listed(controller, label, NULL, action_step(cmd_use_potion(potion->id)));
	}
}

static void build_menu(Controller *controller) {
	const GameData *data = controller->services.data;
	menu_clear(&controller->cache->menu);
	switch (controller->view) {
	case VIEW_LANGUAGE:
		for (size_t i = 0; i < SUPPORTED_LOCALE_COUNT; i++) {
			const char *locale = SUPPORTED_LOCALES[i];
			add_numbered(controller, xstrdup(controller_label(controller, "language.", locale)),
			    action_with_id(ACTION_CHOOSE_LANGUAGE, locale));
		}
		break;
	case VIEW_TITLE:
		title_menu(controller);
		break;
	case VIEW_SETTINGS:
		settings_menu(controller);
		break;
	case VIEW_DIFFICULTY:
		for (int i = 0; i < data->balance.difficulty_count; i++) {
			const char *id = data->balance.difficulties[i].id;
			add_numbered(controller, described_label(controller, "difficulty.", id),
			    action_with_id(ACTION_CHOOSE_DIFFICULTY, id));
		}
		add_back(controller, VIEW_TITLE);
		break;
	case VIEW_VOCATION:
		for (int i = 0; i < data->vocation_count; i++) {
			const char *id = data->vocations[i].id;
			add_numbered(
			    controller, described_label(controller, "vocation.", id), action_with_id(ACTION_CHOOSE_VOCATION, id));
		}
		add_back(controller, VIEW_DIFFICULTY);
		break;
	case VIEW_AUTO_EQUIP:
		auto_equip_menu(controller);
		break;
	case VIEW_MERCHANT:
		merchant_menu(controller);
		break;
	case VIEW_BUY_POTIONS:
		potion_shop(controller);
		add_back(controller, VIEW_MERCHANT);
		break;
	case VIEW_SELL:
		sell_menu(controller);
		add_back(controller, VIEW_MERCHANT);
		break;
	case VIEW_EQUIPMENT:
		equipment_menu(controller);
		add_back(controller, VIEW_MERCHANT);
		break;
	case VIEW_COMPARE:
		add_text_option(controller, "1", "equipment.equip", action_of(ACTION_EQUIP_COMPARED));
		add_back(controller, VIEW_EQUIPMENT);
		break;
	case VIEW_EQUIPPED_SLOT:
		add_text_option(controller, "1", "equipment.unequip", action_of(ACTION_UNEQUIP_SLOT));
		add_back(controller, VIEW_EQUIPMENT);
		break;
	case VIEW_STOCK:
		stock_menu(controller);
		add_back(controller, VIEW_MERCHANT);
		break;
	case VIEW_CHARACTER:
		add_back(controller, VIEW_MERCHANT);
		break;
	case VIEW_BATTLE:
		add_text_option(controller, "1", "battle.attack", action_step(cmd_attack()));
		add_text_option(controller, "2", "battle.spells", action_go(VIEW_SPELLS));
		add_text_option(controller, "3", "battle.potions", action_go(VIEW_POTIONS));
		add_text_option(controller, "4", "battle.defend", action_step(cmd_defend()));
		add_text_option(controller, "5", "battle.auto", action_go(VIEW_AUTO_BATTLE));
		add_text_option(controller, "q", "battle.save_quit", action_of(ACTION_SAVE_AND_QUIT));
		break;
	case VIEW_AUTO_BATTLE:
		// The modes are listed in the order of `balance.autoBattle.modes`.
		for (int i = 0; i < data->balance.auto_battle.mode_count; i++) {
			const char *mode = data->balance.auto_battle.modes[i].id;
			add_numbered(controller, xstrdup(controller_label(controller, "auto_battle.", mode)),
			    action_with_id(ACTION_START_AUTO_BATTLE, mode));
		}
		add_back(controller, VIEW_BATTLE);
		break;
	case VIEW_VICTORY:
		add_text_option(controller, "1", "victory.end_run", action_step(cmd_end_run()));
		add_text_option(controller, "2", "victory.continue", action_step(cmd_continue_run()));
		break;
	case VIEW_SPELLS:
		spell_menu(controller);
		add_back(controller, VIEW_BATTLE);
		break;
	case VIEW_POTIONS:
		battle_potions(controller);
		add_back(controller, VIEW_BATTLE);
		break;
	case VIEW_GAME_OVER:
		add_text_option(controller, "1", "gameover.new_run", action_of(ACTION_NEW_RUN));
		add_text_option(controller, "2", "gameover.title_screen", action_go(VIEW_TITLE));
		break;
	case VIEW_HALL_OF_FAME:
	case VIEW_BESTIARY:
	case VIEW_ACHIEVEMENTS:
		add_back(controller, VIEW_TITLE);
		break;
	case VIEW_NAME:
	case VIEW_QUANTITY:
	case VIEW_COUNT:
		break;
	}
}

const MenuOption *controller_options(Controller *controller, size_t *count) {
	begin(controller);
	build_menu(controller);
	*count = controller->cache->menu.count;
	return controller->cache->menu.options;
}

// ── input ───────────────────────────────────────────────────────────────────

// One typed character: a single UTF-8 code point that is not a control character.
static bool is_printable_character(const char *key) {
	const unsigned char *bytes = (const unsigned char *)key;
	if (bytes[0] == '\0' || utf8_length(key) != 1) {
		return false;
	}
	bool c1_control = bytes[0] == 0xC2 && bytes[1] < 0xA0;
	return bytes[0] >= 0x20 && bytes[0] != 0x7F && !c1_control;
}

static bool is_space(char character) { return character == ' ' || (character >= '\t' && character <= '\r'); }

static void submit_text(Controller *controller) {
	// The buffer is trimmed in place (it is emptied right after).
	char text[NAME_SIZE];
	const char *start = controller->input_buffer;
	while (is_space(*start)) {
		start++;
	}
	str_copy(text, sizeof(text), start);
	size_t length = strlen(text);
	while (length > 0 && is_space(text[length - 1])) {
		text[--length] = '\0';
	}
	controller->input_buffer[0] = '\0';
	if (controller->view == VIEW_NAME) {
		size_t characters = utf8_length(text);
		if (characters < 1 || characters > MAX_NAME_LENGTH) {
			set_message(controller, controller_text(controller, "new_run.name_invalid"));
			return;
		}
		str_copy(controller->name, NAME_SIZE, text);
		controller->view = VIEW_VOCATION;
		return;
	}
	controller->view = VIEW_BUY_POTIONS;
	long quantity = strtol(text, NULL, 10);
	if (quantity > 0) {
		step(controller, cmd_buy_potion(controller->potion_id, quantity));
	}
}

static void text_input(Controller *controller, const char *key) {
	char *buffer = controller->input_buffer;
	size_t length = strlen(buffer);
	if (str_eq(key, "escape")) {
		buffer[0] = '\0';
		controller->view = controller->view == VIEW_NAME ? VIEW_DIFFICULTY : VIEW_BUY_POTIONS;
	} else if (str_eq(key, "backspace")) {
		// Drops the last character: its continuation bytes (10xxxxxx) and then its first byte.
		while (length > 0 && ((unsigned char)buffer[length - 1] & 0xC0) == 0x80) {
			length--;
		}
		if (length > 0) {
			length--;
		}
		buffer[length] = '\0';
	} else if (str_eq(key, "enter")) {
		submit_text(controller);
	} else if (controller->view == VIEW_NAME && is_printable_character(key)) {
		if (utf8_length(buffer) < MAX_NAME_LENGTH && length + strlen(key) < NAME_SIZE) {
			memcpy(buffer + length, key, strlen(key) + 1);
		}
	} else if (controller->view == VIEW_QUANTITY && strlen(key) == 1 && key[0] >= '0' && key[0] <= '9' &&
	           length < MAX_QUANTITY_DIGITS) {
		buffer[length] = key[0];
		buffer[length + 1] = '\0';
	}
}

void controller_press(Controller *controller, const char *key) {
	if (controller->auto_battle_active) {
		return;
	}
	begin(controller);
	set_message(controller, xstrdup(""));
	if (view_is_text_input(controller->view)) {
		text_input(controller, key);
		return;
	}
	if (view_is_paged(controller->view) && (str_eq(key, "n") || str_eq(key, "p"))) {
		controller->page = max_i64(0, controller->page + (str_eq(key, "n") ? 1 : -1));
		return;
	}
	if (str_eq(key, "escape")) {
		key = "0";
	}
	build_menu(controller);
	const Menu *menu = &controller->cache->menu;
	for (size_t i = 0; i < menu->count; i++) {
		if (str_eq(menu->options[i].key, key)) {
			// Copied first: the action may rebuild the menu it came from.
			Action action = menu->actions[i];
			run_action(controller, &action);
			return;
		}
	}
}

// ── auto-battle (docs/game-design.md §13, docs/tui.md) ──────────────────────

int64_t controller_auto_battle_interval_ms(const Controller *controller) {
	return AUTO_BATTLE_BASE_MS / controller->settings.battle_speed;
}

bool controller_auto_battle_step(Controller *controller) {
	begin(controller);
	if (!controller->auto_battle_active || !controller->has_session ||
	    controller->session.engine.state.phase != PHASE_BATTLE) {
		controller->auto_battle_active = false;
		return false;
	}
	const RunState *state = &controller->session.engine.state;
	controller->auto_turns++;
	step(controller, auto_battle_choose(controller->services.data, controller->auto_mode, state));
	if (state->phase != PHASE_BATTLE || controller->auto_turns >= MAX_AUTO_BATTLE_TURNS) {
		controller->auto_battle_active = false;
	}
	return controller->auto_battle_active;
}

void controller_run_auto_battle(Controller *controller) {
	while (controller_auto_battle_step(controller)) {
	}
}
