// Loads shared/data/*.json (embedded in the binary) into immutable domain definitions.
#ifndef RPG_INFRASTRUCTURE_DATA_LOADER_H
#define RPG_INFRASTRUCTURE_DATA_LOADER_H

#include "domain/definitions.h"

// Returns the content of "data/<name>.json", or NULL when the file does not exist.
typedef const char *(*DataSource)(const char *path, void *context);

// Loads the game data embedded at build time. On failure returns false and fills `error`.
bool load_game_data(GameData *out, char *error, size_t error_size);
// Same, reading the files through `source` (tests use it to feed broken documents).
bool load_game_data_from(DataSource source, void *context, GameData *out, char *error, size_t error_size);

#endif
