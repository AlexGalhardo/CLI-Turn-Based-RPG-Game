#include "application/bot.h"

#include "application/battle.h"
#include "application/loot.h"
#include "application/merchant.h"
#include "domain/character.h"
#include "domain/formulas.h"

#include <string.h>

// The bot's own heuristics (not game balance): they are frozen by the golden files.
#define HEAL_THRESHOLD_PCT 45
#define MANA_POTION_THRESHOLD_PCT 25
#define MAX_POTION_STOCK 20

// True when (value, id) is greater than the best pair so far: Python's max() over (value, id) tuples.
static bool better(int64_t value, const char *id, int64_t best_value, const char *best_id) {
	return best_id == NULL || value > best_value || (value == best_value && strcmp(id, best_id) > 0);
}

static bool charge_incoming(const GameData *data, const MonsterInstance *monster) {
	if (!monster->is_boss) {
		return false;
	}
	int64_t every = data->balance.boss_telegraph_every;
	return monster->boss_actions % (every + 1) == every;
}

static const PotionDef *best_owned_potion(const GameData *data, const RunState *state, Resource resource) {
	const PotionDef *best = NULL;
	for (int i = 0; i < data->potion_count; i++) {
		const PotionDef *potion = &data->potions[i];
		if (potion->resource != resource || counter_get(&state->player.potions, potion->id) <= 0) {
			continue;
		}
		if (better(potion->max, potion->id, best == NULL ? 0 : best->max, best == NULL ? NULL : best->id)) {
			best = potion;
		}
	}
	return best;
}

// Best affordable spell of `kind`. Healing spells rank by `max`; attack spells by expected damage against
// `creature`, skipping elements it is immune to.
static const SpellDef *best_spell(
    const GameData *data, const RunState *state, SpellKind kind, const MonsterDef *creature) {
	const VocationDef *vocation = data_vocation(data, state->player.vocation_id);
	const SpellDef *best = NULL;
	int64_t best_value = 0;
	for (int i = 0; i < vocation->spell_count; i++) {
		const SpellDef *spell = data_spell(data, vocation->spells[i]);
		if (spell->kind != kind || spell_cost(data, &state->player, spell) > state->player.mp) {
			continue;
		}
		int64_t value = spell->max;
		if (creature != NULL) {
			int64_t resistance = creature->resistances[spell->element];
			if (resistance <= 0) {
				continue;
			}
			value = (spell->min + spell->max) * resistance;
		}
		if (better(value, spell->id, best_value, best == NULL ? NULL : best->id)) {
			best = spell;
			best_value = value;
		}
	}
	return best;
}

static Command battle(const GameData *data, const RunState *state) {
	const Player *player = &state->player;
	if (!state->has_monster) {
		fatal("battle without a monster");
	}
	const MonsterInstance *monster = &state->monster;
	CharacterSheet sheet = build_sheet(player, data);

	if (charge_incoming(data, monster)) {
		return cmd_defend();
	}
	if (player->hp * 100 < sheet.max_hp * HEAL_THRESHOLD_PCT) {
		const SpellDef *heal = best_spell(data, state, SPELL_HEAL, NULL);
		if (heal != NULL) {
			return cmd_cast(heal->id);
		}
		const PotionDef *potion = best_owned_potion(data, state, RESOURCE_HP);
		if (potion != NULL) {
			return cmd_use_potion(potion->id);
		}
	}
	if (player->mp * 100 < sheet.max_mp * MANA_POTION_THRESHOLD_PCT) {
		const PotionDef *potion = best_owned_potion(data, state, RESOURCE_MP);
		if (potion != NULL) {
			return cmd_use_potion(potion->id);
		}
	}
	const SpellDef *spell = best_spell(data, state, SPELL_ATTACK, data_creature(data, monster->creature_id));
	return spell != NULL ? cmd_cast(spell->id) : cmd_attack();
}

static bool potion_purchase(const GameData *data, const RunState *state, Resource resource, Command *out) {
	const PotionDef *best = NULL;
	int64_t owned = 0;
	for (int i = 0; i < data->potion_count; i++) {
		const PotionDef *potion = &data->potions[i];
		if (potion->resource != resource || !potion_available(state, potion)) {
			continue;
		}
		owned += counter_get(&state->player.potions, potion->id);
		if (better(potion->max, potion->id, best == NULL ? 0 : best->max, best == NULL ? NULL : best->id)) {
			best = potion;
		}
	}
	if (best == NULL) {
		return false;
	}
	int64_t target = min_i64(MAX_POTION_STOCK, 5 + state->round / 5);
	int64_t budget = resource == RESOURCE_MP ? state->player.gold / 2 : state->player.gold;
	int64_t quantity = min_i64(target - owned, budget / best->price);
	if (quantity <= 0) {
		return false;
	}
	*out = cmd_buy_potion(best->id, quantity);
	return true;
}

static Command merchant(const GameData *data, const RunState *state) {
	const Player *player = &state->player;
	const VocationDef *vocation = data_vocation(data, player->vocation_id);
	// Walk the bag in uid order without sorting it: take the smallest uid above the previous one each time.
	int64_t previous_uid = INT64_MIN;
	int64_t lowest_uid = INT64_MIN;
	for (int n = 0; n < player->bag_count; n++) {
		const ItemInstance *item = NULL;
		for (int i = 0; i < player->bag_count; i++) {
			const ItemInstance *candidate = &player->bag[i];
			if (candidate->uid > previous_uid && (item == NULL || candidate->uid < item->uid)) {
				item = candidate;
			}
		}
		if (item == NULL) {
			break;
		}
		if (n == 0) {
			lowest_uid = item->uid;
		}
		previous_uid = item->uid;
		const ItemDef *definition = data_item(data, item->item_id);
		if (!can_use(definition, vocation) || required_level(item, data) > player->level) {
			continue;
		}
		const ItemInstance *current = player_equipped(player, definition->slot);
		if (current == NULL || item_score(item, data) > item_score(current, data)) {
			return cmd_equip(item->uid);
		}
	}
	if (player->bag_count > 0) {
		return cmd_sell_item(lowest_uid);
	}
	Command purchase;
	if (potion_purchase(data, state, RESOURCE_HP, &purchase) || potion_purchase(data, state, RESOURCE_MP, &purchase)) {
		return purchase;
	}
	return cmd_next_fight();
}

Command bot_choose(const GameData *data, const RunState *state) {
	if (state->phase == PHASE_BATTLE) {
		return battle(data, state);
	}
	if (state->phase == PHASE_VICTORY) {
		return cmd_end_run();
	}
	return merchant(data, state);
}
