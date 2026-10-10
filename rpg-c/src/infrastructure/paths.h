// The player's data directory (docs/persistence.md). The shared/ folder needs no path: it is embedded.
#ifndef RPG_INFRASTRUCTURE_PATHS_H
#define RPG_INFRASTRUCTURE_PATHS_H

#include "infrastructure/filesystem.h"

#define DATA_DIR_ENV "RPG_DATA_DIR"
#define DEFAULT_DATA_DIR_NAME ".cli-turn-based-rpg"

// `--data-dir`, else RPG_DATA_DIR, else ~/.cli-turn-based-rpg. `cli_value` may be NULL.
void resolve_data_dir(const char *cli_value, char out[PATH_SIZE]);
// The same rule with the environment passed in, so it can be tested without touching the process environment.
void resolve_data_dir_from(const char *cli_value, const char *env_value, const char *home, char out[PATH_SIZE]);

#endif
