// The game engine: a pure state machine `step(command) -> events` (docs/architecture.md).
// No clock, no I/O, no global state: the same seed and commands always produce the same events.
#ifndef RPG_APPLICATION_ENGINE_H
#define RPG_APPLICATION_ENGINE_H

#include "application/commands.h"
#include "application/events.h"
#include "application/run_state.h"
#include "domain/rng.h"

typedef struct {
	const GameData *data; // borrowed; must outlive the engine
	RunState state;       // owned
	Rng rng;
} GameEngine;

// Starts a run and appends its first events. Returns false (filling `error`) for an unknown vocation or difficulty.
bool engine_new_run(GameEngine *engine, const GameData *data, const RunConfig *config, int64_t seed, EventList *events,
    char *error, size_t error_size);
// Takes ownership of `state` (the caller must not free or reuse it).
void engine_restore(GameEngine *engine, const GameData *data, RunState state, uint32_t rng_state);
// Appends the events of one command to `events`.
void engine_step(GameEngine *engine, const Command *command, EventList *events);
void engine_free(GameEngine *engine);

#endif
