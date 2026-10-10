// Monster spawning for a round: tier, cycle, position, enemy class and difficulty scaling (docs/game-design.md §3).
#ifndef RPG_APPLICATION_SPAWNER_H
#define RPG_APPLICATION_SPAWNER_H

#include "domain/entities.h"
#include "domain/formulas.h"
#include "domain/rng.h"

RoundInfo spawn_monster(
    const GameData *data, Rng *rng, int64_t round_number, const DifficultyDef *difficulty, MonsterInstance *out);

#endif
