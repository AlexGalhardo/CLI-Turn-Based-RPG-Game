// The files compiled into the binary by cmake/embed.cmake: the game's Lua modules and shared/ (data, i18n, art).
#ifndef RPG_HOST_EMBEDDED_H
#define RPG_HOST_EMBEDDED_H

#include <stddef.h>

typedef struct {
	const char *name; // a module name ("rpg.domain.rng") or a path inside shared/ ("data/items.json")
	const char *content;
	size_t size;
} EmbeddedFile;

extern const EmbeddedFile EMBEDDED_MODULES[];
extern const size_t EMBEDDED_MODULE_COUNT;
extern const EmbeddedFile EMBEDDED_ASSETS[];
extern const size_t EMBEDDED_ASSET_COUNT;

#endif
