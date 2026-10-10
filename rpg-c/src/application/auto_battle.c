#include "application/auto_battle.h"

#include "application/battle.h"
#include "domain/character.h"
#include "domain/formulas.h"

#include <string.h>

#define OFFENSE_ATTACK "attack"

// Highest `max`; ties go to the lowest id.
static bool stronger(int64_t max, const char *id, int64_t best_max, const char *best_id) {
	return best_id == NULL || max > best_max || (max == best_max && strcmp(id, best_id) < 0);
}

static const SpellDef *strongest_spell(const GameData *data, const RunState *state, SpellKind kind) {
	const Player *player = &state->player;
	const VocationDef *vocation = data_vocation(data, player->vocation_id);
	const SpellDef *best = NULL;
	for (int i = 0; i < vocation->spell_count; i++) {
		const SpellDef *spell = data_spell(data, vocation->spells[i]);
		if (spell->kind != kind || spell_cost(data, player, spell) > player->mp) {
			continue;
		}
		if (stronger(spell->max, spell->id, best == NULL ? 0 : best->max, best == NULL ? NULL : best->id)) {
			best = spell;
		}
	}
	return best;
}

static const PotionDef *best_potion(const GameData *data, const RunState *state, Resource resource) {
	const PotionDef *best = NULL;
	for (int i = 0; i < data->potion_count; i++) {
		const PotionDef *potion = &data->potions[i];
		if (potion->resource != resource || counter_get(&state->player.potions, potion->id) <= 0) {
			continue;
		}
		if (stronger(potion->max, potion->id, best == NULL ? 0 : best->max, best == NULL ? NULL : best->id)) {
			best = potion;
		}
	}
	return best;
}

static bool heal(const GameData *data, const RunState *state, Command *out) {
	const SpellDef *spell = strongest_spell(data, state, SPELL_HEAL);
	if (spell != NULL) {
		*out = cmd_cast(spell->id);
		return true;
	}
	const PotionDef *potion = best_potion(data, state, RESOURCE_HP);
	if (potion != NULL) {
		*out = cmd_use_potion(potion->id);
		return true;
	}
	return false;
}

// The boss announced its charged attack: its next action is the charge.
static bool telegraph_pending(const GameData *data, const RunState *state) {
	if (!state->has_monster || !state->monster.is_boss) {
		return false;
	}
	int64_t every = data->balance.boss_telegraph_every;
	return state->monster.boss_actions % (every + 1) == every;
}

Command auto_battle_choose(const GameData *data, const char *mode_id, const RunState *state) {
	const AutoBattleDef *config = &data->balance.auto_battle;
	const AutoBattleModeDef *mode = find_auto_battle_mode(config, mode_id);
	if (mode == NULL) {
		fatal("unknown auto-battle mode: %s", mode_id);
	}
	const Player *player = &state->player;
	CharacterSheet sheet = build_sheet(player, data);
	Command command;

	if (player->hp * 100 < sheet.max_hp * config->emergency_heal_below_pct && heal(data, state, &command)) {
		return command;
	}
	int64_t every = mode->support_every;
	if (state->turn % every == every - 1) {
		if (player->hp * 100 < sheet.max_hp * config->heal_below_pct && heal(data, state, &command)) {
			return command;
		}
		if (player->mp * 100 < sheet.max_mp * config->mana_below_pct) {
			const PotionDef *potion = best_potion(data, state, RESOURCE_MP);
			if (potion != NULL) {
				return cmd_use_potion(potion->id);
			}
		}
		if (telegraph_pending(data, state)) {
			return cmd_defend();
		}
	}
	if (strcmp(mode->offense, OFFENSE_ATTACK) == 0) {
		return cmd_attack();
	}
	const SpellDef *spell = strongest_spell(data, state, SPELL_ATTACK);
	return spell == NULL ? cmd_attack() : cmd_cast(spell->id);
}
