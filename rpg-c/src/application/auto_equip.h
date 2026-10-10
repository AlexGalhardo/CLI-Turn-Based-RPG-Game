// Auto-equip with auto-sell (docs/game-design.md §8.1). Consumes no randomness.
#ifndef RPG_APPLICATION_AUTO_EQUIP_H
#define RPG_APPLICATION_AUTO_EQUIP_H

#include "application/events.h"
#include "application/run_state.h"

// Bag index of the highest-score item the player can wear in `slot` now (ties go to the lowest uid), or -1.
int best_bag_item(const RunState *state, const GameData *data, Slot slot);
void auto_equip(RunState *state, const GameData *data, EventList *events);

#endif
