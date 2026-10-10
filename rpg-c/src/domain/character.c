#include "domain/character.h"

#include "domain/formulas.h"

#include <string.h>

void item_stats(const ItemInstance *item, const GameData *data, int64_t values[STAT_COUNT], bool present[STAT_COUNT]) {
	const ItemDef *definition = data_item(data, item->item_id);
	const RarityDef *rarity = balance_rarity(&data->balance, item->rarity);
	for (int stat = 0; stat < STAT_COUNT; stat++) {
		values[stat] = definition->has_stat[stat] ? pct(definition->stats[stat], rarity->stat_pct) : 0;
		if (present != NULL) {
			present[stat] = definition->has_stat[stat];
		}
	}
	for (int i = 0; i < item->affix_count; i++) {
		values[item->affixes[i].stat] += item->affixes[i].value;
		if (present != NULL) {
			present[item->affixes[i].stat] = true;
		}
	}
}

int64_t item_value(const ItemInstance *item, const GameData *data) {
	return pct(data_item(data, item->item_id)->value, balance_rarity(&data->balance, item->rarity)->value_pct);
}

int64_t item_score(const ItemInstance *item, const GameData *data) {
	int64_t values[STAT_COUNT];
	item_stats(item, data, values, NULL);
	int64_t score = 0;
	for (int stat = 0; stat < STAT_COUNT; stat++) {
		score += values[stat] * data->balance.item_score_weights[stat];
	}
	return score;
}

int64_t required_level(const ItemInstance *item, const GameData *data) {
	return 1 + item->tier * data->balance.item_level_per_tier;
}

int64_t equipment_score(const Player *player, const GameData *data) {
	int64_t score = 0;
	for (int slot = 0; slot < SLOT_COUNT; slot++) {
		if (player->equipped[slot]) {
			score += item_score(&player->equipment[slot], data);
		}
	}
	return score;
}

CharacterSheet build_sheet(const Player *player, const GameData *data) {
	const VocationDef *vocation = data_vocation(data, player->vocation_id);
	const Caps *caps = &data->balance.caps;
	int64_t totals[STAT_COUNT] = {0};
	for (int slot = 0; slot < SLOT_COUNT; slot++) {
		if (!player->equipped[slot]) {
			continue;
		}
		int64_t values[STAT_COUNT];
		item_stats(&player->equipment[slot], data, values, NULL);
		for (int stat = 0; stat < STAT_COUNT; stat++) {
			totals[stat] += values[stat];
		}
	}

	Element weapon_element = ELEMENT_PHYSICAL;
	if (player->equipped[SLOT_WEAPON]) {
		const ItemDef *weapon = data_item(data, player->equipment[SLOT_WEAPON].item_id);
		if (weapon->has_element) {
			weapon_element = weapon->element;
		}
	}

	int64_t level_bonus = (player->level - 1) * vocation->melee_per_level;
	int64_t attack = totals[STAT_ATTACK];
	CharacterSheet sheet = {
	    .max_hp = vocation->start_hp + (player->level - 1) * vocation->hp_per_level + totals[STAT_MAX_HP],
	    .max_mp = vocation->start_mp + (player->level - 1) * vocation->mp_per_level + totals[STAT_MAX_MP],
	    .hp_regen = vocation->hp_regen + totals[STAT_HP_REGEN],
	    .mp_regen = vocation->mp_regen + totals[STAT_MP_REGEN],
	    .melee_min = vocation->melee_min + level_bonus + attack,
	    .melee_max = vocation->melee_max + level_bonus + attack,
	    .weapon_element = weapon_element,
	    .armor = totals[STAT_ARMOR],
	    .crit_chance = min_i64(totals[STAT_CRIT_CHANCE], caps->crit_chance),
	    .crit_damage = totals[STAT_CRIT_DAMAGE],
	    .spell_power = totals[STAT_SPELL_POWER],
	    .physical_damage = totals[STAT_PHYSICAL_DAMAGE],
	    .dodge = min_i64(totals[STAT_DODGE], caps->dodge),
	    .parry = min_i64(totals[STAT_PARRY], caps->parry),
	    .life_leech = min_i64(totals[STAT_LIFE_LEECH], caps->leech),
	    .mana_leech = min_i64(totals[STAT_MANA_LEECH], caps->leech),
	};
	for (int element = 0; element < ELEMENT_COUNT; element++) {
		sheet.protections[element] = min_i64(totals[protection_stat((Element)element)], caps->protection);
	}
	return sheet;
}
