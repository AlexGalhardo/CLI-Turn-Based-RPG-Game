// Auto-battle policy (docs/game-design.md §13): picks the player's battle commands from the run state only.
//
// It lives in the application layer, not in the engine: the commands it returns are ordinary commands, so a fight
// played by the policy replays like any other. Every command it returns is valid (affordable spells, owned potions).
#ifndef RPG_APPLICATION_AUTO_BATTLE_H
#define RPG_APPLICATION_AUTO_BATTLE_H

#include "application/commands.h"
#include "application/run_state.h"

#define AUTO_BATTLE_MELEE "melee"
#define AUTO_BATTLE_SPELLS "spells"
#define AUTO_BATTLE_BALANCED "balanced"

// `mode_id` is a key of `balance.autoBattle.modes`.
Command auto_battle_choose(const GameData *data, const char *mode_id, const RunState *state);

#endif
