#include "application/loot.h"

#include <stdlib.h>
#include <string.h>

static bool contains(const Id *types, int count, const char *type) {
	for (int i = 0; i < count; i++) {
		if (strcmp(types[i], type) == 0) {
			return true;
		}
	}
	return false;
}

bool can_use(const ItemDef *item, const VocationDef *vocation) {
	if (item->slot == SLOT_WEAPON) {
		return contains(vocation->weapon_types, vocation->weapon_type_count, item->type);
	}
	if (item->slot == SLOT_SHIELD) {
		return contains(vocation->shield_types, vocation->shield_type_count, item->type);
	}
	return true;
}

const RarityDef *roll_rarity(const GameData *data, Rng *rng, const RarityWeights *weights) {
	const RarityDef *options[MAX_RARITIES];
	int64_t option_weights[MAX_RARITIES];
	size_t count = 0;
	for (int i = 0; i < data->balance.rarity_count; i++) {
		if (weights->weights[i] > 0) {
			options[count] = &data->balance.rarities[i];
			option_weights[count] = weights->weights[i];
			count++;
		}
	}
	if (count == 0) {
		fatal("rarity table without a positive weight");
	}
	if (count == 1) {
		return options[0];
	}
	return options[rng_weighted(rng, option_weights, count)];
}

static int compare_items_by_id(const void *a, const void *b) {
	const ItemDef *const *left = a;
	const ItemDef *const *right = b;
	return strcmp((*left)->id, (*right)->id);
}

static int compare_affixes_by_id(const void *a, const void *b) {
	const AffixDef *const *left = a;
	const AffixDef *const *right = b;
	return strcmp((*left)->id, (*right)->id);
}

bool generate_item(const GameData *data, Rng *rng, const VocationDef *vocation, int64_t tier,
    const RarityWeights *weights, int64_t uid, ItemInstance *out) {
	int64_t lowest_tier = tier - 1 < 0 ? 0 : tier - 1;
	const ItemDef **candidates = xmalloc((size_t)data->item_count * sizeof(*candidates));
	size_t candidate_count = 0;
	for (int i = 0; i < data->item_count; i++) {
		const ItemDef *item = &data->items[i];
		if (lowest_tier <= item->tier && item->tier <= tier && can_use(item, vocation)) {
			candidates[candidate_count++] = item;
		}
	}
	if (candidate_count == 0) {
		free(candidates);
		return false;
	}
	qsort(candidates, candidate_count, sizeof(*candidates), compare_items_by_id);
	const ItemDef *base = candidates[rng_pick(rng, candidate_count)];
	free(candidates);

	const RarityDef *rarity = roll_rarity(data, rng, weights);
	int64_t affix_count = rng_roll(rng, rarity->affix_min, rarity->affix_max);

	memset(out, 0, sizeof(*out));
	out->uid = uid;
	id_set(out->item_id, base->id);
	id_set(out->rarity, rarity->id);
	out->tier = tier;

	bool used_stats[STAT_COUNT] = {false};
	const AffixDef **pool = xmalloc((size_t)data->affix_count * sizeof(*pool));
	for (int64_t n = 0; n < affix_count && out->affix_count < MAX_AFFIXES; n++) {
		size_t pool_count = 0;
		for (int i = 0; i < data->affix_count; i++) {
			const AffixDef *affix = &data->affixes[i];
			if (affix->slots[base->slot] && !used_stats[affix->stat]) {
				pool[pool_count++] = affix;
			}
		}
		if (pool_count == 0) {
			break;
		}
		qsort(pool, pool_count, sizeof(*pool), compare_affixes_by_id);
		const AffixDef *affix = pool[rng_pick(rng, pool_count)];
		used_stats[affix->stat] = true;
		AffixRoll *roll = &out->affixes[out->affix_count++];
		roll->stat = affix->stat;
		roll->value = rng_roll(rng, affix->min, affix->max) + tier * affix->per_tier;
	}
	free(pool);
	return true;
}
