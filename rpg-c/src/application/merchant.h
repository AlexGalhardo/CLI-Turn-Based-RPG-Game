// Merchant phase: potions, bag, equipment and rotating stock (docs/game-design.md §10).
#ifndef RPG_APPLICATION_MERCHANT_H
#define RPG_APPLICATION_MERCHANT_H

#include "application/commands.h"
#include "application/events.h"
#include "application/run_state.h"
#include "domain/rng.h"

#define MAX_POTIONS_PER_PURCHASE 99

int64_t stock_price(const ItemInstance *item, const GameData *data);
// A potion can be bought once the next round reaches its unlock round.
bool potion_available(const RunState *state, const PotionDef *potion);

// Generates the rotating stock for the tier of the next round.
void merchant_enter(const GameData *data, Rng *rng, RunState *state, EventList *events);
// `command` must be buy_potion, sell_item, equip, unequip or buy_stock_item.
void merchant_handle(const GameData *data, RunState *state, const Command *command, EventList *events);

#endif
