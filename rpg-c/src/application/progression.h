// Experience, levels, magic levels and spell levels (docs/game-design.md §4 and §9).
#ifndef RPG_APPLICATION_PROGRESSION_H
#define RPG_APPLICATION_PROGRESSION_H

#include "application/events.h"
#include "domain/entities.h"

void gain_experience(const GameData *data, Player *player, int64_t amount, EventList *events);
void after_cast(const GameData *data, Player *player, const SpellDef *spell, int64_t mana_cost, EventList *events);

#endif
