// Turns engine events into translated sentences. Shared by every presentation (text UI and TUI).
#ifndef RPG_PRESENTATION_EVENT_TEXT_H
#define RPG_PRESENTATION_EVENT_TEXT_H

#include "application/events.h"
#include "application/run_state.h"
#include "infrastructure/i18n.h"

// Borrows both: they must outlive the formatter.
typedef struct {
	const GameData *data;
	const Translator *translator;
} EventFormatter;

// The sentence for `event`; `state` supplies the names the event leaves out (the monster, an item by uid).
// The caller frees the result.
char *event_format(const EventFormatter *formatter, const Event *event, const RunState *state);
// Appends the sentence to `out` instead of allocating a new string.
void event_format_into(StrBuf *out, const EventFormatter *formatter, const Event *event, const RunState *state);

#endif
