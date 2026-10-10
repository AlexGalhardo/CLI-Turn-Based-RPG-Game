// The informative lines shown above the options: character sheet, run summaries, the equipment screens
// (docs/tui.md "Equipment screen"), Hall of Fame, bestiary and achievements.

#include "domain/character.h"
#include "domain/formulas.h"
#include "presentation/controller_internal.h"
#include "presentation/render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERROR_SIZE 256
// "2026-09-27" out of "2026-09-27T21:04:11Z".
#define DATE_LENGTH 10

static const char *slot_label(Controller *controller, Slot slot) {
	return controller_label(controller, "slot.", slot_name(slot));
}

static const char *stat_label(Controller *controller, Stat stat) {
	return controller_label(controller, "stat.", stat_name(stat));
}

static const char *rarity_label(Controller *controller, const ItemInstance *item) {
	return controller_label(controller, "rarity.", item->rarity);
}

static void push_text(Controller *controller, Lines *out, const char *key) {
	lines_push(out, controller_text(controller, key), NULL);
}

// ── character and run summaries ─────────────────────────────────────────────

static void character_sheet(Controller *controller, Lines *out) {
	const GameData *data = controller->services.data;
	const Balance *balance = &data->balance;
	const Player *player = &controller_state(controller)->player;
	CharacterSheet sheet = build_sheet(player, data);
	lines_push(out,
	    CT(controller, "character.level", P_INT("level", player->level), P_INT("xp", player->xp),
	        P_INT("next", xp_for_level(player->level + 1))),
	    NULL);
	lines_push(out,
	    CT(controller, "character.magic_level", P_INT("magicLevel", player->magic_level),
	        P_INT("spent", player->mana_spent), P_INT("next", mana_for_magic_level(player->magic_level, balance))),
	    NULL);
	lines_push(out,
	    CT(controller, "character.hp_mp", P_INT("hp", player->hp), P_INT("maxHp", sheet.max_hp),
	        P_INT("mp", player->mp), P_INT("maxMp", sheet.max_mp)),
	    NULL);
	lines_push(out,
	    CT(controller, "character.melee", P_INT("min", sheet.melee_min), P_INT("max", sheet.melee_max),
	        P_STR("element", controller_label(controller, "element.", element_name(sheet.weapon_element)))),
	    NULL);
	const struct {
		const char *key;
		int64_t value;
	} stats[] = {
	    {"stat.armor", sheet.armor},
	    {"stat.hpRegen", sheet.hp_regen},
	    {"stat.mpRegen", sheet.mp_regen},
	    {"stat.critChance", sheet.crit_chance},
	    {"stat.critDamage", sheet.crit_damage},
	    {"stat.spellPower", sheet.spell_power},
	    {"stat.physicalDamage", sheet.physical_damage},
	    {"stat.dodge", sheet.dodge},
	    {"stat.parry", sheet.parry},
	    {"stat.lifeLeech", sheet.life_leech},
	    {"stat.manaLeech", sheet.mana_leech},
	};
	for (size_t i = 0; i < ARRAY_LEN(stats); i++) {
		if (stats[i].value != 0) {
			lines_push(out,
			    CT(controller, "character.stat_line", P_STR("stat", controller_label(controller, stats[i].key, "")),
			        P_INT("value", stats[i].value)),
			    NULL);
		}
	}
	for (int element = 0; element < ELEMENT_COUNT; element++) {
		if (sheet.protections[element] != 0) {
			char value[32];
			snprintf(value, sizeof(value), "%lld%%", (long long)sheet.protections[element]);
			lines_push(out,
			    CT(controller, "character.stat_line",
			        P_STR("stat", controller_label(controller, "element.", element_name((Element)element))),
			        P_STR("value", value)),
			    NULL);
		}
	}
	lines_push(out, xstrdup(""), NULL);
	push_text(controller, out, "character.equipment");
	for (int slot = 0; slot < SLOT_COUNT; slot++) {
		const ItemInstance *item = player_equipped(player, (Slot)slot);
		if (item == NULL) {
			lines_push(
			    out, CT(controller, "character.empty_slot", P_STR("slot", slot_label(controller, (Slot)slot))), NULL);
		} else {
			lines_push(out,
			    CT(controller, "character.slot", P_STR("slot", slot_label(controller, (Slot)slot)),
			        P_STR("item", data_item(data, item->item_id)->name),
			        P_STR("rarity", rarity_label(controller, item))),
			    NULL);
		}
	}
	lines_push(out,
	    CT(controller, "character.bag", P_INT("count", player->bag_count), P_INT("capacity", balance->bag_capacity)),
	    NULL);
}

static char *run_stats_line(Controller *controller) {
	const RunState *state = controller_state(controller);
	return CT(controller, "gameover.stats", P_INT("level", state->player.level),
	    P_INT("damage", state->stats.damage_dealt), P_INT("kills", counter_total(&state->stats.kills)),
	    P_INT("elites", state->stats.elites_killed), P_INT("bosses", state->stats.bosses_killed));
}

static void game_over_summary(Controller *controller, Lines *out) {
	const RunState *state = controller_state(controller);
	const char *vocation = controller_label(controller, "vocation.", state->player.vocation_id);
	const char *key = "gameover.summary";
	const char *monster = "?";
	if (state->has_death_cause && state->death_cause[0] != '\0') {
		monster = data_creature(controller->services.data, state->death_cause)->name;
	} else if (state->won) {
		key = "gameover.won_summary";
	}
	lines_push(out,
	    CT(controller, key, P_STR("monster", monster), P_STR("name", state->player.name), P_STR("vocation", vocation),
	        P_INT("round", state->round)),
	    NULL);
	lines_push(out, run_stats_line(controller), NULL);
}

static void victory_summary(Controller *controller, Lines *out) {
	const GameData *data = controller->services.data;
	const RunState *state = controller_state(controller);
	const MonsterDef *boss =
	    data_boss_of_tier(data, round_info(state->round, &data->balance, data_tier_count(data)).tier);
	lines_push(out,
	    CT(controller, "victory.summary", P_STR("name", state->player.name),
	        P_STR("vocation", controller_label(controller, "vocation.", state->player.vocation_id)),
	        P_STR("monster", boss->name), P_INT("round", state->round)),
	    NULL);
	lines_push(out, run_stats_line(controller), NULL);
	lines_push(out, xstrdup(""), NULL);
	push_text(controller, out, "victory.choice");
}

// ── equipment screens (docs/tui.md "Equipment screen") ──────────────────────

static void equipment_body(Controller *controller, Lines *out) {
	const GameData *data = controller->services.data;
	const Player *player = &controller_state(controller)->player;
	lines_push(out, CT(controller, "equipment.equipped_header", P_INT("score", equipment_score(player, data))), NULL);
	for (int i = 0; i < SLOT_COUNT; i++) {
		Slot slot = EQUIPMENT_SLOT_ORDER[i];
		const ItemInstance *item = player_equipped(player, slot);
		if (item == NULL) {
			lines_push(out, CT(controller, "equipment.slot_empty", P_STR("slot", slot_label(controller, slot))),
			    STYLE_WARNING);
			continue;
		}
		lines_push(out,
		    CT(controller, "equipment.slot_line", P_STR("slot", slot_label(controller, slot)),
		        P_STR("name", data_item(data, item->item_id)->name), P_STR("rarity", rarity_label(controller, item)),
		        P_INT("level", required_level(item, data)), P_INT("score", item_score(item, data))),
		    item->rarity);
	}
	lines_push(out, xstrdup(""), NULL);
	push_text(controller, out, "equipment.bag_header");
	bool usable = false;
	for (int i = 0; i < player->bag_count; i++) {
		usable = usable || controller_can_use(controller, &player->bag[i]);
	}
	if (!usable) {
		push_text(controller, out, "equipment.bag_empty");
	}
}

char *controller_compare_title(Controller *controller) {
	const ItemInstance *item = controller_compared_item(controller);
	if (item == NULL) {
		return controller_text(controller, "merchant.equipment");
	}
	const GameData *data = controller->services.data;
	const ItemDef *definition = data_item(data, item->item_id);
	const ItemInstance *current = player_equipped(&controller_state(controller)->player, definition->slot);
	const char *current_name =
	    current == NULL ? controller_label(controller, "equipment.empty", "") : data_item(data, current->item_id)->name;
	return CT(controller, "equipment.compare_title", P_STR("slot", slot_label(controller, definition->slot)),
	    P_STR("current", current_name), P_STR("new", definition->name));
}

// "+2 Critical chance, +3 Dodge" ("" for no item or no affixes); scratch-owned.
static const char *affix_list(Controller *controller, const ItemInstance *item) {
	StrBuf out = {0};
	for (int i = 0; item != NULL && i < item->affix_count; i++) {
		const AffixRoll *affix = &item->affixes[i];
		sb_append(&out, i > 0 ? ", " : "");
		sb_append(&out, controller_keep(controller, CT(controller, "equipment.affix", P_INT("value", affix->value),
		                                                P_STR("stat", stat_label(controller, affix->stat)))));
	}
	sb_append(&out, "");
	return controller_keep(controller, sb_take(&out));
}

static void compare_body(Controller *controller, Lines *out) {
	const ItemInstance *item = controller_compared_item(controller);
	if (item == NULL) {
		return;
	}
	const GameData *data = controller->services.data;
	const Player *player = &controller_state(controller)->player;
	const ItemInstance *current = player_equipped(player, data_item(data, item->item_id)->slot);
	int64_t new_stats[STAT_COUNT];
	bool new_present[STAT_COUNT];
	int64_t old_stats[STAT_COUNT] = {0};
	bool old_present[STAT_COUNT] = {false};
	char delta[DELTA_SIZE];
	item_stats(item, data, new_stats, new_present);
	if (current != NULL) {
		item_stats(current, data, old_stats, old_present);
	}
	for (int stat = 0; stat < STAT_COUNT; stat++) {
		if (!new_present[stat] && !old_present[stat]) {
			continue;
		}
		int64_t old_value = old_present[stat] ? old_stats[stat] : 0;
		int64_t new_value = new_present[stat] ? new_stats[stat] : 0;
		format_delta(new_value - old_value, delta);
		lines_push(out,
		    CT(controller, "equipment.stat_delta", P_STR("stat", stat_label(controller, (Stat)stat)),
		        P_INT("current", old_value), P_INT("new", new_value), P_STR("delta", delta)),
		    delta_style(new_value - old_value));
	}
	const char *gained = affix_list(controller, item);
	const char *lost = affix_list(controller, current);
	if (gained[0] != '\0') {
		lines_push(out, CT(controller, "equipment.affixes_gained", P_STR("affixes", gained)), STYLE_GAIN);
	}
	if (lost[0] != '\0') {
		lines_push(out, CT(controller, "equipment.affixes_lost", P_STR("affixes", lost)), STYLE_LOSS);
	}
	int64_t old_score = current == NULL ? 0 : item_score(current, data);
	int64_t new_score = item_score(item, data);
	format_delta(new_score - old_score, delta);
	lines_push(out,
	    CT(controller, "equipment.score_delta", P_INT("current", old_score), P_INT("new", new_score),
	        P_STR("delta", delta)),
	    delta_style(new_score - old_score));
	int64_t level = required_level(item, data);
	if (level > player->level) {
		lines_push(out,
		    CT(controller, "equipment.level_needed", P_INT("level", level), P_INT("current", player->level)),
		    STYLE_LOSS);
	}
}

static void slot_body(Controller *controller, Lines *out) {
	const GameData *data = controller->services.data;
	const ItemInstance *item = player_equipped(&controller_state(controller)->player, controller->slot);
	if (item == NULL) {
		lines_push(out, controller_text(controller, "equipment.empty"), STYLE_WARNING);
		return;
	}
	lines_push(out,
	    CT(controller, "equipment.item_title", P_STR("name", data_item(data, item->item_id)->name),
	        P_STR("rarity", rarity_label(controller, item)), P_INT("level", required_level(item, data)),
	        P_INT("score", item_score(item, data))),
	    item->rarity);
	int64_t stats[STAT_COUNT];
	bool present[STAT_COUNT];
	item_stats(item, data, stats, present);
	for (int stat = 0; stat < STAT_COUNT; stat++) {
		if (present[stat]) {
			lines_push(out,
			    CT(controller, "character.stat_line", P_STR("stat", stat_label(controller, (Stat)stat)),
			        P_INT("value", stats[stat])),
			    NULL);
		}
	}
}

// ── profile screens ─────────────────────────────────────────────────────────

// The profile of the open run, or the one on disk (loaded into `storage`, which the caller then releases).
static const Profile *open_profile(Controller *controller, Profile *storage, bool *loaded) {
	*loaded = false;
	if (controller->has_session) {
		return &controller->session.profile;
	}
	const Repositories *repositories = &controller->services.repositories;
	char error[ERROR_SIZE];
	if (repositories->load_profile(repositories->context, storage, error, sizeof(error)) != LOAD_OK) {
		fatal("%s", error);
	}
	*loaded = true;
	return storage;
}

static void close_profile(Profile *storage, bool loaded) {
	if (loaded) {
		profile_free(storage);
	}
}

static void hall_of_fame(Controller *controller, Lines *out) {
	Profile storage;
	bool loaded;
	const Profile *profile = open_profile(controller, &storage, &loaded);
	if (profile->hall_count == 0) {
		push_text(controller, out, "hall.empty");
	}
	for (int i = 0; i < profile->hall_count; i++) {
		const HallOfFameEntry *entry = &profile->hall_of_fame[i];
		char date[DATE_LENGTH + 1];
		snprintf(date, sizeof(date), "%.*s", DATE_LENGTH, entry->ended_at);
		lines_push(out,
		    CT(controller, entry->won ? "hall.entry_won" : "hall.entry", P_INT("position", i + 1),
		        P_STR("name", entry->name),
		        P_STR("vocation", controller_label(controller, "vocation.", entry->vocation)),
		        P_STR("difficulty", controller_label(controller, "difficulty.", entry->difficulty)),
		        P_INT("round", entry->round), P_INT("level", entry->level), P_STR("date", date)),
		    NULL);
	}
	close_profile(&storage, loaded);
}

// Bestiary order: tier, monsters before the boss, then name.
static int compare_creatures(const void *left, const void *right) {
	const MonsterDef *a = *(const MonsterDef *const *)left;
	const MonsterDef *b = *(const MonsterDef *const *)right;
	if (a->tier != b->tier) {
		return a->tier < b->tier ? -1 : 1;
	}
	if (a->is_boss != b->is_boss) {
		return a->is_boss ? 1 : -1;
	}
	int by_name = strcmp(a->name, b->name);
	if (by_name != 0) {
		return by_name;
	}
	// qsort is not stable: equal names keep their file order (both then come from the same array).
	return (a > b) - (a < b);
}

static void bestiary(Controller *controller, Lines *out) {
	const GameData *data = controller->services.data;
	Profile storage;
	bool loaded;
	const Profile *profile = open_profile(controller, &storage, &loaded);
	size_t count = (size_t)data->monster_count + (size_t)data->boss_count;
	const MonsterDef **creatures = xcalloc(count + 1, sizeof(*creatures));
	for (int i = 0; i < data->monster_count; i++) {
		creatures[i] = &data->monsters[i];
	}
	for (int i = 0; i < data->boss_count; i++) {
		creatures[data->monster_count + i] = &data->bosses[i];
	}
	qsort(creatures, count, sizeof(*creatures), compare_creatures);
	for (size_t i = 0; i < count; i++) {
		const MonsterDef *creature = creatures[i];
		const BestiaryEntry *entry = profile_bestiary_entry(profile, creature->id);
		if (entry == NULL) {
			lines_push(out, CT(controller, "bestiary.unknown", P_INT("tier", creature->tier + 1)), NULL);
		} else if (profile_revealed(profile, creature->id)) {
			StrBuf weak = {0};
			StrBuf strong = {0};
			if (controller_resistance_names(controller, &weak, creature, true) == 0) {
				sb_append(&weak, "—");
			}
			if (controller_resistance_names(controller, &strong, creature, false) == 0) {
				sb_append(&strong, "—");
			}
			lines_push(out,
			    CT(controller, "bestiary.entry_revealed", P_STR("name", creature->name),
			        P_INT("tier", creature->tier + 1), P_INT("kills", entry->kills), P_STR("weak", weak.data),
			        P_STR("strong", strong.data)),
			    NULL);
			sb_free(&weak);
			sb_free(&strong);
		} else {
			lines_push(out,
			    CT(controller, "bestiary.entry", P_STR("name", creature->name), P_INT("tier", creature->tier + 1),
			        P_INT("kills", entry->kills)),
			    NULL);
		}
	}
	free((void *)creatures);
	close_profile(&storage, loaded);
}

// Scratch translation of "achievement.<id>.<part>".
static const char *achievement_text(Controller *controller, const AchievementDef *achievement, const char *part) {
	StrBuf key = {0};
	sb_appendf(&key, "achievement.%s.%s", achievement->id, part);
	char *text = CT(controller, key.data, P_INT("value", achievement->value));
	sb_free(&key);
	return controller_keep(controller, text);
}

static void achievements(Controller *controller, Lines *out) {
	const GameData *data = controller->services.data;
	Profile storage;
	bool loaded;
	const Profile *profile = open_profile(controller, &storage, &loaded);
	for (int i = 0; i < data->achievement_count; i++) {
		const AchievementDef *achievement = &data->achievements[i];
		const char *name = achievement_text(controller, achievement, "name");
		const char *description = achievement_text(controller, achievement, "description");
		const Unlock *unlock = profile_unlock(profile, achievement->id);
		if (unlock == NULL) {
			lines_push(out,
			    CT(controller, "achievements.locked", P_STR("name", name), P_STR("description", description)), NULL);
		} else {
			char date[DATE_LENGTH + 1];
			snprintf(date, sizeof(date), "%.*s", DATE_LENGTH, unlock->unlocked_at);
			lines_push(out,
			    CT(controller, "achievements.unlocked", P_STR("name", name), P_STR("description", description),
			        P_STR("date", date)),
			    NULL);
		}
	}
	close_profile(&storage, loaded);
}

// ── the body of the current view ────────────────────────────────────────────

static bool owns_a_potion(const Player *player) {
	for (int i = 0; i < player->potions.count; i++) {
		if (player->potions.entries[i].value != 0) {
			return true;
		}
	}
	return false;
}

// Keeps the lines of the current page and appends a blank line and "Page 2/12".
static void keep_page(Controller *controller, Lines *out) {
	int64_t pages = ((int64_t)out->count + LINES_PER_PAGE - 1) / LINES_PER_PAGE;
	controller->page = min_i64(controller->page, pages - 1);
	size_t start = (size_t)controller->page * LINES_PER_PAGE;
	size_t kept = 0;
	for (size_t i = 0; i < out->count; i++) {
		if (i >= start && kept < LINES_PER_PAGE) {
			out->text[kept] = out->text[i];
			out->color[kept] = out->color[i];
			kept++;
		} else {
			free(out->text[i]);
			free(out->color[i]);
		}
	}
	out->count = kept;
	lines_push(out, xstrdup(""), NULL);
	lines_push(out, CT(controller, "menu.page", P_INT("page", controller->page + 1), P_INT("pages", pages)), NULL);
}

void controller_build_body(Controller *controller, Lines *out) {
	lines_clear(out);
	switch (controller->view) {
	case VIEW_EQUIPMENT:
		equipment_body(controller, out);
		break;
	case VIEW_COMPARE:
		compare_body(controller, out);
		break;
	case VIEW_EQUIPPED_SLOT:
		slot_body(controller, out);
		break;
	case VIEW_CHARACTER:
		character_sheet(controller, out);
		break;
	case VIEW_GAME_OVER:
		game_over_summary(controller, out);
		break;
	case VIEW_VICTORY:
		victory_summary(controller, out);
		break;
	case VIEW_HALL_OF_FAME:
		hall_of_fame(controller, out);
		break;
	case VIEW_BESTIARY:
		bestiary(controller, out);
		break;
	case VIEW_ACHIEVEMENTS:
		achievements(controller, out);
		break;
	case VIEW_MERCHANT: {
		const Player *player = &controller_state(controller)->player;
		lines_push(
		    out, CT(controller, "merchant.welcome", P_STR("name", player->name), P_INT("gold", player->gold)), NULL);
		break;
	}
	case VIEW_SELL:
		if (controller_state(controller)->player.bag_count == 0) {
			push_text(controller, out, "merchant.empty_bag");
		}
		break;
	case VIEW_STOCK:
		if (controller_state(controller)->stock_count == 0) {
			push_text(controller, out, "merchant.empty_stock");
		}
		break;
	case VIEW_POTIONS:
		if (!owns_a_potion(&controller_state(controller)->player)) {
			push_text(controller, out, "battle.no_potions");
		}
		break;
	default:
		break;
	}
	if (view_is_paged(controller->view) && out->count > LINES_PER_PAGE) {
		keep_page(controller, out);
	}
}
