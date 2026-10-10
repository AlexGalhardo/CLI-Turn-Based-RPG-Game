#include "presentation/event_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Event fields that hold ids: they are replaced by display names before formatting.
static const char *const NAME_FIELDS[][2] = {
    {"spellId", "spell"},
    {"potionId", "potion"},
    {"monsterId", "monster"},
    {"itemId", "item"},
};
// Event fields replaced by their translated labels ("element.fire", "rarity.rare").
static const char *const LABEL_FIELDS[] = {"element", "status", "rarity", "resource"};

#define MAX_PARAMS (EVENT_MAX_FIELDS + ARRAY_LEN(NAME_FIELDS) + ARRAY_LEN(LABEL_FIELDS) + 2)
#define KEY_SIZE (2 * ID_SIZE + 16)

static bool flag_is_set(const Event *event, const char *name) {
	const EventField *field = event_field(event, name);
	return field != NULL && field->kind == EVENT_BOOL && field->integer != 0;
}

static bool text_field_is(const Event *event, const char *name, const char *value) {
	const EventField *field = event_field(event, name);
	return field != NULL && field->kind == EVENT_STR && strcmp(field->text, value) == 0;
}

static void event_key(const Event *event, char out[KEY_SIZE]) {
	if (event_is(event, "error")) {
		snprintf(out, KEY_SIZE, "error.%s", event_get_str(event, "code"));
		return;
	}
	const char *variant = "";
	const EventField *target = event_field(event, "target");
	if (flag_is_set(event, "crit")) {
		variant = "_crit";
	} else if (flag_is_set(event, "charged")) {
		variant = "_charged";
	} else if (event_is(event, "round_started") && flag_is_set(event, "isBoss")) {
		variant = "_boss";
	} else if (event_is(event, "round_started") && text_field_is(event, "enemyClass", "elite")) {
		variant = "_elite";
	} else if (target != NULL && target->kind == EVENT_STR) {
		snprintf(out, KEY_SIZE, "event.%s_%s", event->type, target->text);
		return;
	}
	snprintf(out, KEY_SIZE, "event.%s%s", event->type, variant);
}

// An id the data does not know is shown as it is.
static const char *display_name(const GameData *data, const char *field, const char *identifier) {
	if (strcmp(field, "spellId") == 0) {
		const SpellDef *spell = find_spell(data, identifier);
		return spell != NULL ? spell->name : identifier;
	}
	if (strcmp(field, "potionId") == 0) {
		const PotionDef *potion = find_potion(data, identifier);
		return potion != NULL ? potion->name : identifier;
	}
	if (strcmp(field, "monsterId") == 0) {
		const MonsterDef *creature = find_creature(data, identifier);
		return creature != NULL ? creature->name : identifier;
	}
	const ItemDef *item = find_item(data, identifier);
	return item != NULL ? item->name : identifier;
}

// The item may be in the bag, worn or in the merchant stock; NULL when it is nowhere.
static const char *item_name_by_uid(const GameData *data, int64_t uid, const RunState *state) {
	const Player *player = &state->player;
	for (int i = 0; i < player->bag_count; i++) {
		if (player->bag[i].uid == uid) {
			return data_item(data, player->bag[i].item_id)->name;
		}
	}
	for (int slot = 0; slot < SLOT_COUNT; slot++) {
		const ItemInstance *item = player_equipped(player, (Slot)slot);
		if (item != NULL && item->uid == uid) {
			return data_item(data, item->item_id)->name;
		}
	}
	for (int i = 0; i < state->stock_count; i++) {
		if (state->merchant_stock[i].uid == uid) {
			return data_item(data, state->merchant_stock[i].item_id)->name;
		}
	}
	return NULL;
}

static bool has_param(const Param *params, size_t count, const char *name) {
	for (size_t i = 0; i < count; i++) {
		if (strcmp(params[i].name, name) == 0) {
			return true;
		}
	}
	return false;
}

void event_format_into(StrBuf *out, const EventFormatter *formatter, const Event *event, const RunState *state) {
	// A later param with the same name wins (see translate_into), so replacements are simply appended.
	Param params[MAX_PARAMS];
	size_t count = 0;
	char *labels[ARRAY_LEN(LABEL_FIELDS)] = {NULL};
	char uid_text[32];

	for (int i = 0; i < event->field_count; i++) {
		const EventField *field = &event->fields[i];
		switch (field->kind) {
		case EVENT_INT:
			params[count++] = P_INT(field->name, field->integer);
			break;
		case EVENT_STR:
			params[count++] = P_STR(field->name, field->text);
			break;
		case EVENT_BOOL:
			params[count++] = P_STR(field->name, field->integer != 0 ? "True" : "False");
			break;
		}
	}
	for (size_t i = 0; i < ARRAY_LEN(NAME_FIELDS); i++) {
		const EventField *field = event_field(event, NAME_FIELDS[i][0]);
		if (field != NULL && field->kind == EVENT_STR) {
			params[count++] = P_STR(NAME_FIELDS[i][1], display_name(formatter->data, field->name, field->text));
		}
	}
	for (size_t i = 0; i < ARRAY_LEN(LABEL_FIELDS); i++) {
		const EventField *field = event_field(event, LABEL_FIELDS[i]);
		if (field != NULL && field->kind == EVENT_STR) {
			char key[KEY_SIZE];
			snprintf(key, sizeof(key), "%s.%s", field->name, field->text);
			labels[i] = translate(formatter->translator, key, NULL, 0);
			params[count++] = P_STR(field->name, labels[i]);
		}
	}
	if (!has_param(params, count, "monster") && state->has_monster) {
		params[count++] = P_STR("monster", data_creature(formatter->data, state->monster.creature_id)->name);
	}
	const EventField *uid = event_field(event, "uid");
	if (uid != NULL && uid->kind == EVENT_INT && !has_param(params, count, "item")) {
		const char *name = item_name_by_uid(formatter->data, uid->integer, state);
		if (name == NULL) {
			snprintf(uid_text, sizeof(uid_text), "#%lld", (long long)uid->integer);
			name = uid_text;
		}
		params[count++] = P_STR("item", name);
	}

	char key[KEY_SIZE];
	event_key(event, key);
	translate_into(out, formatter->translator, key, params, count);
	for (size_t i = 0; i < ARRAY_LEN(labels); i++) {
		free(labels[i]);
	}
}

char *event_format(const EventFormatter *formatter, const Event *event, const RunState *state) {
	StrBuf out = {0};
	event_format_into(&out, formatter, event, state);
	return sb_take(&out);
}
