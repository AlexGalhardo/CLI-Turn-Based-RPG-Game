#include "application/auto_equip.h"

#include "application/loot.h"
#include "domain/character.h"
#include "domain/formulas.h"

int best_bag_item(const RunState *state, const GameData *data, Slot slot) {
	const Player *player = &state->player;
	const VocationDef *vocation = data_vocation(data, player->vocation_id);
	int best = -1;
	int64_t best_score = 0;
	for (int i = 0; i < player->bag_count; i++) {
		const ItemInstance *item = &player->bag[i];
		const ItemDef *definition = data_item(data, item->item_id);
		if (definition->slot != slot || !can_use(definition, vocation) || required_level(item, data) > player->level) {
			continue;
		}
		int64_t score = item_score(item, data);
		if (best < 0 || score > best_score || (score == best_score && item->uid < player->bag[best].uid)) {
			best = i;
			best_score = score;
		}
	}
	return best;
}

void auto_equip(RunState *state, const GameData *data, EventList *events) {
	Player *player = &state->player;
	bool changed = false;
	for (int i = 0; i < SLOT_COUNT; i++) {
		Slot slot = EQUIPMENT_SLOT_ORDER[i];
		int best_index = best_bag_item(state, data, slot);
		if (best_index < 0) {
			continue;
		}
		ItemInstance best = player->bag[best_index];
		int64_t score = item_score(&best, data);
		bool had_item = player->equipped[slot];
		ItemInstance current = player->equipment[slot];
		if (had_item && score <= item_score(&current, data)) {
			continue;
		}
		player_bag_remove(player, best_index);
		player->equipment[slot] = best;
		player->equipped[slot] = true;
		changed = true;
		Event equipped = event_new("item_auto_equipped");
		event_int(&equipped, "uid", best.uid);
		event_str(&equipped, "itemId", best.item_id);
		event_str(&equipped, "slot", slot_name(slot));
		event_int(&equipped, "score", score);
		events_push(events, equipped);
		if (had_item) {
			int64_t gold = item_value(&current, data);
			player->gold += gold;
			Event sold = event_new("item_auto_sold");
			event_int(&sold, "uid", current.uid);
			event_str(&sold, "itemId", current.item_id);
			event_int(&sold, "gold", gold);
			events_push(events, sold);
		}
	}
	if (changed) {
		CharacterSheet sheet = build_sheet(player, data);
		player->hp = min_i64(player->hp, sheet.max_hp);
		player->mp = min_i64(player->mp, sheet.max_mp);
	}
}
