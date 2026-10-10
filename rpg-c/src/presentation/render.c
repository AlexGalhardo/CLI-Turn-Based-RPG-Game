#include "presentation/render.h"

#include <stdio.h>
#include <string.h>

static const char *const ELEMENT_COLORS[ELEMENT_COUNT] = {
    [ELEMENT_PHYSICAL] = "white",
    [ELEMENT_FIRE] = "red",
    [ELEMENT_ICE] = "cyan",
    [ELEMENT_ENERGY] = "magenta",
    [ELEMENT_EARTH] = "green",
    [ELEMENT_HOLY] = "yellow",
    [ELEMENT_DEATH] = "bright_black",
};

typedef struct {
	const char *name;
	const char *color;
} NamedColor;

static const NamedColor RARITY_COLORS[] = {
    {"common", "white"},
    {"rare", "dodger_blue1"},
    {"legendary", "orange1"},
    {"mythic", "medium_purple1"},
};

static const NamedColor STYLE_COLORS[] = {
    {STYLE_WARNING, "yellow"},
    {STYLE_GAIN, "green"},
    {STYLE_LOSS, "red"},
    {STYLE_DIM, "bright_black"},
};

static const char *find_color(const NamedColor *table, size_t count, const char *name) {
	for (size_t i = 0; i < count; i++) {
		if (strcmp(table[i].name, name) == 0) {
			return table[i].color;
		}
	}
	return NULL;
}

const char *element_color(Element element) { return ELEMENT_COLORS[element]; }

const char *rarity_color(const char *rarity) { return find_color(RARITY_COLORS, ARRAY_LEN(RARITY_COLORS), rarity); }

const char *style_color(const char *style) { return find_color(STYLE_COLORS, ARRAY_LEN(STYLE_COLORS), style); }

void bar(StrBuf *out, int64_t current, int64_t maximum, int width) {
	int64_t filled = 0;
	if (maximum > 0) {
		int64_t shown = current < 0 ? 0 : (current > maximum ? maximum : current);
		filled = width * shown / maximum;
		if (current > 0 && filled < 1) {
			filled = 1;
		}
	}
	for (int cell = 0; cell < width; cell++) {
		sb_append(out, cell < filled ? "█" : "░");
	}
	if (out->data == NULL) {
		sb_append(out, "");
	}
}

const char *hp_color(int64_t current, int64_t maximum) {
	if (maximum > 0 && current * 100 > maximum * 50) {
		return "green";
	}
	if (maximum > 0 && current * 100 > maximum * 25) {
		return "yellow";
	}
	return "red";
}

void format_delta(int64_t delta, char out[DELTA_SIZE]) {
	snprintf(out, DELTA_SIZE, delta > 0 ? "+%lld" : "%lld", (long long)delta);
}

const char *delta_style(int64_t delta) {
	if (delta > 0) {
		return STYLE_GAIN;
	}
	return delta < 0 ? STYLE_LOSS : NULL;
}

char list_key(size_t index) {
	if (index >= strlen(LIST_KEYS)) {
		fatal("list index %zu has no key", index);
	}
	return LIST_KEYS[index];
}

int list_index(const char *key) {
	if (strlen(key) != 1) {
		return -1;
	}
	const char *found = strchr(LIST_KEYS, key[0]);
	return found == NULL ? -1 : (int)(found - LIST_KEYS);
}
