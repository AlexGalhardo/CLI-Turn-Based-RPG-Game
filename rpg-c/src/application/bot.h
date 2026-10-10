// A deterministic heuristic player used by the simulator and by the end-to-end parity tests.
//
// The bot only reads the run state and returns one command at a time, so it can drive any engine implementation.
// Its decisions are part of the golden "bot full run" files: changing them requires regenerating those files.
#ifndef RPG_APPLICATION_BOT_H
#define RPG_APPLICATION_BOT_H

#include "application/commands.h"
#include "application/run_state.h"

Command bot_choose(const GameData *data, const RunState *state);

#endif
