// Framework-independent rendering helpers (bars, colours, list keys), specified in docs/tui.md.
//
// Colours are returned as names ("green", "bright_black", "dodger_blue1"): the terminal renderer maps a name to its
// escape code, so this module stays free of terminal details and is testable as plain text.
#ifndef RPG_PRESENTATION_RENDER_H
#define RPG_PRESENTATION_RENDER_H

#include "domain/base.h"
#include "domain/enums.h"

#define BAR_WIDTH 25
#define MIN_COLUMNS 100
#define MIN_ROWS 30
#define LIST_KEYS "123456789abcdefghijklmnopqrstuvwxyz"

// Semantic colours used by the equipment screen (docs/tui.md): empty slots, score/stat gains and losses.
#define STYLE_WARNING "warning"
#define STYLE_GAIN "gain"
#define STYLE_LOSS "loss"
#define STYLE_DIM "dim"

const char *element_color(Element element);
// NULL for a name that is not a rarity / not a semantic style.
const char *rarity_color(const char *rarity);
const char *style_color(const char *style);

// Appends `width` cells: `█` filled / `░` empty. A living creature always shows at least one filled cell.
void bar(StrBuf *out, int64_t current, int64_t maximum, int width);
const char *hp_color(int64_t current, int64_t maximum);
// Writes "+5", "-3" or "0" into `out`.
#define DELTA_SIZE 24
void format_delta(int64_t delta, char out[DELTA_SIZE]);
// STYLE_GAIN, STYLE_LOSS or NULL for no change.
const char *delta_style(int64_t delta);
// The key of the list entry at `index`: 1-9 then a-z. More entries than keys is a bug.
char list_key(size_t index);
// The position of a list key, or -1 when `key` is not exactly one list key.
int list_index(const char *key);

#endif
