#include "presentation/cli.h"

#include "infrastructure/i18n.h"
#include "version.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char HELP_TEXT[] = "usage: rpg [-h] [--version] [--seed SEED] [--lang {en,pt-BR}] [--no-anim]\n"
                         "           [--data-dir DATA_DIR] [--simulate N] [--vocation VOCATION]\n"
                         "           [--difficulty DIFFICULTY]\n"
                         "\n"
                         "Endless turn-based RPG for the terminal.\n"
                         "\n"
                         "options:\n"
                         "  -h, --help            show this help message and exit\n"
                         "  --version             show program's version number and exit\n"
                         "  --seed SEED           deterministic run\n"
                         "  --lang {en,pt-BR}     override the saved language\n"
                         "  --no-anim             disable animations\n"
                         "  --data-dir DATA_DIR   saves/profile location\n"
                         "  --simulate N          run N headless bot games and print a report\n"
                         "  --vocation VOCATION   (simulator) restrict to one vocation\n"
                         "  --difficulty DIFFICULTY\n"
                         "                        (simulator) restrict to one difficulty";

// The whole text must be an integer: "12abc" and "" are rejected.
static bool parse_integer(const char *text, int64_t *out) {
	if (text[0] == '\0') {
		return false;
	}
	char *end = NULL;
	errno = 0;
	long long value = strtoll(text, &end, 10);
	if (errno != 0 || *end != '\0') {
		return false;
	}
	*out = value;
	return true;
}

bool parse_cli(int arg_count, const char *const *args, CliOptions *out, char *error, size_t error_size) {
	memset(out, 0, sizeof(*out));
	bool show_help = false;
	bool show_version = false;
	for (int index = 0; index < arg_count; index++) {
		const char *argument = args[index];
		// `--flag=value` and `--flag value` are both accepted.
		char flag[32];
		const char *inline_value = NULL;
		const char *equals = strchr(argument, '=');
		if (str_starts_with(argument, "--") && equals != NULL) {
			inline_value = equals + 1;
			snprintf(flag, sizeof(flag), "%.*s", (int)(equals - argument), argument);
		} else {
			snprintf(flag, sizeof(flag), "%s", argument);
		}

		if (str_eq(flag, "-h") || str_eq(flag, "--help")) {
			show_help = true;
			continue;
		}
		if (str_eq(flag, "--version")) {
			show_version = true;
			continue;
		}
		if (str_eq(flag, "--no-anim")) {
			out->no_anim = true;
			continue;
		}
		bool takes_value = str_eq(flag, "--seed") || str_eq(flag, "--simulate") || str_eq(flag, "--lang") ||
		                   str_eq(flag, "--data-dir") || str_eq(flag, "--vocation") || str_eq(flag, "--difficulty");
		if (!takes_value) {
			snprintf(error, error_size, "unrecognized arguments: %s", argument);
			return false;
		}
		const char *value = inline_value;
		if (value == NULL) {
			if (index + 1 >= arg_count) {
				snprintf(error, error_size, "argument %s: expected one argument", flag);
				return false;
			}
			value = args[++index];
		}

		if (str_eq(flag, "--seed")) {
			if (!parse_integer(value, &out->seed)) {
				snprintf(error, error_size, "argument --seed: invalid _non_negative value: '%s'", value);
				return false;
			}
			if (out->seed < 0) {
				snprintf(error, error_size, "argument --seed: must be >= 0");
				return false;
			}
			out->has_seed = true;
		} else if (str_eq(flag, "--simulate")) {
			if (!parse_integer(value, &out->simulate)) {
				snprintf(error, error_size, "argument --simulate: invalid _positive value: '%s'", value);
				return false;
			}
			if (out->simulate <= 0) {
				snprintf(error, error_size, "argument --simulate: must be > 0");
				return false;
			}
		} else if (str_eq(flag, "--lang")) {
			if (!locale_supported(value)) {
				snprintf(error, error_size, "argument --lang: invalid choice: '%s' (choose from 'en', 'pt-BR')", value);
				return false;
			}
			out->lang = value;
		} else if (str_eq(flag, "--data-dir")) {
			out->data_dir = value;
		} else if (str_eq(flag, "--vocation")) {
			out->vocation = value;
		} else {
			out->difficulty = value;
		}
	}
	// argparse acts on --help/--version as soon as it meets them, so they win over any other flag.
	if (show_help) {
		out->exit = true;
	} else if (show_version) {
		out->exit = true;
		snprintf(out->output, sizeof(out->output), "rpg %s (c)", RPG_VERSION);
	}
	return true;
}
