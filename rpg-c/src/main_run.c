#include "main_run.h"

#include "application/simulator.h"
#include "infrastructure/data_loader.h"
#include "infrastructure/paths.h"
#include "presentation/cli.h"
#include "presentation/simulator_report.h"
#include "presentation/tui/app.h"

#include <stdlib.h>
#include <string.h>

static int run_simulator(const GameData *data, const CliOptions *options, StrBuf *out, StrBuf *err) {
	int vocation_count = options->vocation != NULL ? 1 : data->vocation_count;
	int difficulty_count = options->difficulty != NULL ? 1 : data->balance.difficulty_count;
	// `options.seed or 1` in the reference: a missing seed and seed 0 both start at 1.
	int64_t base_seed = options->has_seed && options->seed != 0 ? options->seed : 1;

	SimulationSummary *summaries = xcalloc((size_t)(vocation_count * difficulty_count), sizeof(SimulationSummary));
	size_t count = 0;
	int code = 0;
	for (int v = 0; v < vocation_count && code == 0; v++) {
		const char *vocation = options->vocation != NULL ? options->vocation : data->vocations[v].id;
		for (int d = 0; d < difficulty_count && code == 0; d++) {
			const char *difficulty =
			    options->difficulty != NULL ? options->difficulty : data->balance.difficulties[d].id;
			char error[160];
			if (strlen(vocation) >= ID_SIZE || strlen(difficulty) >= ID_SIZE) {
				sb_appendf(
				    err, "error: invalid run config: '%s'\n", strlen(vocation) >= ID_SIZE ? vocation : difficulty);
				code = 2;
			} else if (simulate(data, vocation, difficulty, options->simulate, base_seed, &summaries[count], error,
			               sizeof(error))) {
				count++;
			} else {
				sb_appendf(err, "error: %s\n", error);
				code = 2;
			}
		}
	}
	if (code == 0) {
		render_report(out, summaries, count, data);
		sb_append_char(out, '\n');
	}
	free(summaries);
	return code;
}

int run(int arg_count, const char *const *args, StrBuf *out, StrBuf *err) {
	CliOptions options;
	char error[256];
	if (!parse_cli(arg_count, args, &options, error, sizeof(error))) {
		// Like argparse: the usage lines, then the error, exit code 2.
		const char *usage_end = strstr(HELP_TEXT, "\n\n");
		sb_append_n(err, HELP_TEXT, (size_t)(usage_end - HELP_TEXT));
		sb_appendf(err, "\nrpg: error: %s\n", error);
		return 2;
	}
	if (options.exit) {
		sb_appendf(out, "%s\n", options.output[0] != '\0' ? options.output : HELP_TEXT);
		return 0;
	}

	GameData data;
	if (!load_game_data(&data, error, sizeof(error))) {
		sb_appendf(err, "rpg: %s\n", error);
		return 1;
	}
	int code;
	if (options.simulate > 0) {
		code = run_simulator(&data, &options, out, err);
	} else {
		char data_dir[PATH_SIZE];
		resolve_data_dir(options.data_dir, data_dir);
		const char *no_anim_env = getenv("RPG_NO_ANIM");
		bool animate = !options.no_anim && (no_anim_env == NULL || no_anim_env[0] == '\0');
		code = run_tui(&data, data_dir, &options, animate, err);
	}
	game_data_free(&data);
	return code;
}
