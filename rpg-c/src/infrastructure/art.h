// Parses shared/art files: `@animation` headers, frames separated by `%%` (docs/data-format.md).
#ifndef RPG_INFRASTRUCTURE_ART_H
#define RPG_INFRASTRUCTURE_ART_H

#include "domain/definitions.h"

typedef struct {
	char **lines;
	size_t line_count;
} Frame;

typedef struct {
	Id name;
	Frame *frames;
	size_t frame_count;
} Animation;

typedef struct {
	Animation *items;
	size_t count;
} Animations;

// Returns false and fills `error` when content comes before the first `@animation`.
bool parse_art(const char *text, Animations *out, char *error, size_t error_size);
void animations_free(Animations *animations);
const Animation *find_animation(const Animations *animations, const char *name);
// Frame of `animation` at animation tick `tick`, falling back to idle; NULL when there is no art.
const Frame *frame_for(const Animations *animations, const char *animation, int64_t tick);

// The embedded art of a creature: bosses/<id>.txt for a boss, families/<family>.txt otherwise.
// The result is parsed once and cached for the life of the program; NULL when the file does not exist.
const Animations *art_for_creature(const MonsterDef *creature);
const Animations *art_load_file(const char *folder, const char *name);

#endif
