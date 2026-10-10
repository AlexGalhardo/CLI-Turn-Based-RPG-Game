#include "infrastructure/paths.h"

#include <stdlib.h>

void resolve_data_dir_from(const char *cli_value, const char *env_value, const char *home, char out[PATH_SIZE]) {
	if (cli_value != NULL && cli_value[0] != '\0') {
		str_copy(out, PATH_SIZE, cli_value);
	} else if (env_value != NULL && env_value[0] != '\0') {
		str_copy(out, PATH_SIZE, env_value);
	} else {
		path_join(out, home != NULL ? home : ".", DEFAULT_DATA_DIR_NAME);
	}
}

void resolve_data_dir(const char *cli_value, char out[PATH_SIZE]) {
#ifdef _WIN32
	const char *home = getenv("USERPROFILE");
#else
	const char *home = getenv("HOME");
#endif
	resolve_data_dir_from(cli_value, getenv(DATA_DIR_ENV), home, out);
}
