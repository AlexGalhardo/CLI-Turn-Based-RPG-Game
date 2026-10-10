// Battle resolution, following docs/game-design.md §6. Every RNG call here is part of the contract.
#ifndef RPG_APPLICATION_BATTLE_H
#define RPG_APPLICATION_BATTLE_H

#include "application/commands.h"
#include "application/events.h"
#include "application/run_state.h"
#include "domain/rng.h"

#define STATUS_STUN_ID "stun"
#define STUN_COOLDOWN_TURNS 2

typedef enum { BATTLE_ONGOING, BATTLE_VICTORY, BATTLE_DEFEAT } BattleOutcome;

// A battle borrows the data, the PRNG and the run state for the length of one turn.
typedef struct {
	const GameData *data;
	Rng *rng;
	RunState *state;
} Battle;

// Fills `error` and returns false for an invalid command. Validation never consumes randomness.
bool battle_validate(const Battle *battle, const Command *command, Event *error);
// `command` must be attack, cast, potion or defend, already validated.
BattleOutcome battle_play_turn(Battle *battle, const Command *command, EventList *events);
// Mana cost of a spell at the player's current spell level.
int64_t spell_cost(const GameData *data, const Player *player, const SpellDef *spell);

#endif
