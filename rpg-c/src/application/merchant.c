#include "application/merchant.h"

#include "application/auto_equip.h"
#include "application/loot.h"
#include "domain/character.h"
#include "domain/formulas.h"

int64_t stock_price(const ItemInstance *item, const GameData *data) {
	return pct(item_value(item, data), data->balance.merchant_markup_pct);
}

bool potion_available(const RunState *state, const PotionDef *potion) {
	return potion->unlock_round <= state->round + 1;
}

void merchant_enter(const GameData *data, Rng *rng, RunState *state, EventList *events) {
	const VocationDef *vocation = data_vocation(data, state->player.vocation_id);
	int64_t tier = round_info(state->round + 1, &data->balance, data_tier_count(data)).tier;
	state->stock_count = 0;
	for (int64_t i = 0; i < data->balance.merchant_stock_size; i++) {
		ItemInstance item;
		if (generate_item(
		        data, rng, vocation, tier, &data->balance.merchant_rarity_weights, state->next_item_uid, &item)) {
			run_state_take_item_uid(state);
			state->merchant_stock[state->stock_count++] = item;
		}
	}
	Event entered = event_new("merchant_entered");
	event_int(&entered, "round", state->round);
	events_push(events, entered);
}

static void clamp_resources(RunState *state, const GameData *data) {
	Player *player = &state->player;
	CharacterSheet sheet = build_sheet(player, data);
	player->hp = min_i64(player->hp, sheet.max_hp);
	player->mp = min_i64(player->mp, sheet.max_mp);
}

static void buy_potion(
    const GameData *data, RunState *state, const char *potion_id, int64_t quantity, EventList *events) {
	Player *player = &state->player;
	const PotionDef *potion = find_potion(data, potion_id);
	if (potion == NULL) {
		events_push(events, event_error(ERROR_UNKNOWN_POTION));
		return;
	}
	if (!potion_available(state, potion)) {
		events_push(events, event_error(ERROR_POTION_LOCKED));
		return;
	}
	if (quantity < 1 || quantity > MAX_POTIONS_PER_PURCHASE) {
		events_push(events, event_error(ERROR_INVALID_QUANTITY));
		return;
	}
	int64_t cost = potion->price * quantity;
	if (player->gold < cost) {
		events_push(events, event_error(ERROR_NOT_ENOUGH_GOLD));
		return;
	}
	player->gold -= cost;
	counter_add(&player->potions, potion_id, quantity);
	Event bought = event_new("potion_bought");
	event_str(&bought, "potionId", potion_id);
	event_int(&bought, "quantity", quantity);
	event_int(&bought, "gold", cost);
	events_push(events, bought);
}

static void sell(const GameData *data, RunState *state, int64_t uid, EventList *events) {
	Player *player = &state->player;
	int index = player_bag_index(player, uid);
	if (index < 0) {
		events_push(events, event_error(ERROR_INVALID_ITEM));
		return;
	}
	ItemInstance item = player->bag[index];
	int64_t value = item_value(&item, data);
	player_bag_remove(player, index);
	player->gold += value;
	Event sold = event_new("item_sold");
	event_int(&sold, "uid", uid);
	event_str(&sold, "itemId", item.item_id);
	event_int(&sold, "gold", value);
	events_push(events, sold);
}

static Event equipment_event(const char *type, const ItemInstance *item, Slot slot) {
	Event event = event_new(type);
	event_int(&event, "uid", item->uid);
	event_str(&event, "itemId", item->item_id);
	event_str(&event, "slot", slot_name(slot));
	return event;
}

static void equip(const GameData *data, RunState *state, int64_t uid, EventList *events) {
	Player *player = &state->player;
	int index = player_bag_index(player, uid);
	if (index < 0) {
		events_push(events, event_error(ERROR_INVALID_ITEM));
		return;
	}
	ItemInstance item = player->bag[index];
	const ItemDef *definition = data_item(data, item.item_id);
	if (!can_use(definition, data_vocation(data, player->vocation_id))) {
		events_push(events, event_error(ERROR_CANNOT_EQUIP));
		return;
	}
	if (required_level(&item, data) > player->level) {
		events_push(events, event_error(ERROR_LEVEL_TOO_LOW));
		return;
	}
	Slot slot = definition->slot;
	player_bag_remove(player, index);
	if (player->equipped[slot]) {
		ItemInstance previous = player->equipment[slot];
		player_bag_push(player, &previous);
		events_push(events, equipment_event("item_unequipped", &previous, slot));
	}
	player->equipment[slot] = item;
	player->equipped[slot] = true;
	events_push(events, equipment_event("item_equipped", &item, slot));
	clamp_resources(state, data);
}

static void unequip(const GameData *data, RunState *state, Slot slot, EventList *events) {
	Player *player = &state->player;
	if (!player->equipped[slot]) {
		events_push(events, event_error(ERROR_INVALID_ITEM));
		return;
	}
	if (player->bag_count >= data->balance.bag_capacity) {
		events_push(events, event_error(ERROR_BAG_FULL));
		return;
	}
	ItemInstance item = player->equipment[slot];
	player->equipped[slot] = false;
	player_bag_push(player, &item);
	clamp_resources(state, data);
	events_push(events, equipment_event("item_unequipped", &item, slot));
}

static void buy_stock(const GameData *data, RunState *state, int64_t index, EventList *events) {
	Player *player = &state->player;
	if (index < 0 || index >= state->stock_count) {
		events_push(events, event_error(ERROR_INVALID_ITEM));
		return;
	}
	if (player->bag_count >= data->balance.bag_capacity) {
		events_push(events, event_error(ERROR_BAG_FULL));
		return;
	}
	ItemInstance item = state->merchant_stock[index];
	int64_t price = stock_price(&item, data);
	if (player->gold < price) {
		events_push(events, event_error(ERROR_NOT_ENOUGH_GOLD));
		return;
	}
	player->gold -= price;
	for (int i = (int)index + 1; i < state->stock_count; i++) {
		state->merchant_stock[i - 1] = state->merchant_stock[i];
	}
	state->stock_count--;
	player_bag_push(player, &item);
	Event bought = event_new("item_bought");
	event_int(&bought, "uid", item.uid);
	event_str(&bought, "itemId", item.item_id);
	event_int(&bought, "gold", price);
	events_push(events, bought);
	if (state->config.auto_equip) {
		auto_equip(state, data, events);
	}
}

void merchant_handle(const GameData *data, RunState *state, const Command *command, EventList *events) {
	switch (command->type) {
	case CMD_BUY_POTION:
		buy_potion(data, state, command->id, command->number, events);
		break;
	case CMD_SELL_ITEM:
		sell(data, state, command->number, events);
		break;
	case CMD_EQUIP:
		equip(data, state, command->number, events);
		break;
	case CMD_UNEQUIP:
		unequip(data, state, command->slot, events);
		break;
	case CMD_BUY_STOCK_ITEM:
		buy_stock(data, state, command->number, events);
		break;
	default:
		fatal("not a merchant command");
	}
}
