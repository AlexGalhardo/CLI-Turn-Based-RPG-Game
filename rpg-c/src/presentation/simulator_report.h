// The balance report printed by `--simulate`: a plain-text table, byte-identical in every implementation.
#ifndef RPG_PRESENTATION_SIMULATOR_REPORT_H
#define RPG_PRESENTATION_SIMULATOR_REPORT_H

#include "application/simulator.h"

// Appends the table to `out` (no trailing newline).
void render_report(StrBuf *out, const SimulationSummary *summaries, size_t count, const GameData *data);

#endif
