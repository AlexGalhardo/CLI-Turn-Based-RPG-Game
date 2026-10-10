// Command-line flags, identical in every implementation (docs/tui.md). It mimics Python's argparse: the same help
// text, the same error messages and exit code 2 for usage errors.
#ifndef RPG_PRESENTATION_CLI_H
#define RPG_PRESENTATION_CLI_H

#include "domain/base.h"

typedef struct {
	bool has_seed;
	int64_t seed;
	const char *lang; // NULL when not given
	bool no_anim;
	const char *data_dir;   // NULL when not given
	int64_t simulate;       // 0 when not given
	const char *vocation;   // NULL when not given
	const char *difficulty; // NULL when not given
	// --help / --version: print `output` and exit with code 0.
	bool exit;
	char output[64];
} CliOptions;

// The argparse help of the reference implementation.
extern const char HELP_TEXT[];

// Parses the arguments (without the program name); the string options point into `args`. On a usage error returns
// false and writes into `error` the message argparse would print after "rpg: error: ".
bool parse_cli(int arg_count, const char *const *args, CliOptions *out, char *error, size_t error_size);

#endif
