#include "application/simulator.h"
#include "infrastructure/i18n.h"
#include "main_run.h"
#include "presentation/cli.h"
#include "presentation/simulator_report.h"
#include "support/helpers.h"
#include "version.h"

#include <stdlib.h>

#define SUITE "integration/cli_and_simulator"

typedef struct {
	int code;
	StrBuf out;
	StrBuf err;
} MainResult;

// Runs the program in-process. `out.data`/`err.data` are never NULL; release with main_result_free().
static MainResult run_main(int arg_count, const char *const *args) {
	MainResult result = {0};
	sb_append(&result.out, "");
	sb_append(&result.err, "");
	result.code = run(arg_count, args, &result.out, &result.err);
	return result;
}

static void main_result_free(MainResult *result) {
	sb_free(&result->out);
	sb_free(&result->err);
}

#define ARGS(...)                                                                                                      \
	(int)ARRAY_LEN(((const char *const[]){__VA_ARGS__})), (const char *const[]) { __VA_ARGS__ }

// The message of a usage error ("" when the arguments are accepted).
static const char *usage_error(int arg_count, const char *const *args) {
	static char error[256];
	CliOptions options;
	error[0] = '\0';
	if (parse_cli(arg_count, args, &options, error, sizeof(error))) {
		return "";
	}
	return error;
}

TEST(SUITE, simulate_summary) {
	const GameData *data = test_data();
	SimulationSummary summary;
	char error[160] = "";
	REQUIRE(simulate(data, "warrior", "normal", 3, 10, &summary, error, sizeof(error)));
	CHECK_INT(summary.runs, 3);
	CHECK(summary.wins >= 0 && summary.wins <= 3);
	CHECK_INT(summary_win_rate_pct(&summary), summary.wins * 100 / 3);
	CHECK(summary.min_round >= 1);
	CHECK(summary.min_round <= summary.median_round);
	CHECK(summary.median_round <= summary.max_round);
	int64_t deaths = 0;
	for (int i = 0; i < summary.top_killer_count; i++) {
		deaths += summary.top_killers[i].count;
	}
	CHECK(deaths <= 3);
	CHECK(!simulate(data, "warrior", "normal", 0, 1, &summary, error, sizeof(error)));
	CHECK_CONTAINS(error, "positive");
	CHECK(!simulate(data, "knight", "normal", 1, 1, &summary, error, sizeof(error)));
	CHECK_CONTAINS(error, "invalid run config");
}

TEST(SUITE, report_is_an_aligned_table) {
	SimulationSummary summary = {
	    .vocation = "mage",
	    .difficulty = "hard",
	    .runs = 2,
	    .wins = 1,
	    .min_round = 3,
	    .p10_round = 3,
	    .median_round = 4,
	    .p90_round = 5,
	    .max_round = 5,
	    .mean_level = 2,
	    .top_killer_count = 1,
	    .top_killers = {{"rat", 2}},
	};
	StrBuf report = {0};
	render_report(&report, &summary, 1, test_data());
	CHECK_STR(report.data, "vocation  difficulty  runs  wins  win %  min  p10  median  p90  max  avg lvl  top killers\n"
	                       "--------  ----------  ----  ----  -----  ---  ---  ------  ---  ---  -------  -----------\n"
	                       "mage      hard        2     1     50%    3    3    4       5    5    2        Rat (2)");
	sb_free(&report);
}

TEST(SUITE, simulator_counts_won_runs) {
	const GameData *data = test_data();
	SimulationSummary summary;
	char error[160] = "";
	REQUIRE(simulate(data, "archer", "easy", 2, 2002, &summary, error, sizeof(error)));
	CHECK(summary.wins >= 1);
	CHECK_INT(summary.max_round, data->balance.final_round);
}

TEST(SUITE, main_simulate_prints_report) {
	MainResult result = run_main(ARGS("--simulate", "1", "--vocation", "mage", "--difficulty", "hard", "--seed", "5"));
	CHECK_INT(result.code, 0);
	CHECK_CONTAINS(result.out.data, "median");
	CHECK_CONTAINS(result.out.data, "win %");
	CHECK_CONTAINS(result.out.data, "mage");
	CHECK_STR(result.err.data, "");
	main_result_free(&result);

	// Without filters every vocation and difficulty gets a row; seed 0 starts at 1 like a missing seed.
	MainResult all = run_main(ARGS("--simulate", "1", "--seed", "0"));
	CHECK_INT(all.code, 0);
	CHECK_CONTAINS(all.out.data, "archer    normal");
	MainResult unseeded = run_main(ARGS("--simulate", "1"));
	CHECK_STR(all.out.data, unseeded.out.data);
	main_result_free(&unseeded);
	main_result_free(&all);
}

TEST(SUITE, main_simulate_rejects_unknown_vocation) {
	MainResult result = run_main(ARGS("--simulate", "1", "--vocation", "knight"));
	CHECK_INT(result.code, 2);
	CHECK_CONTAINS(result.err.data, "invalid run config");
	CHECK_STR(result.out.data, "");
	main_result_free(&result);
}

TEST(SUITE, main_prints_version_help_and_usage_errors) {
	MainResult version = run_main(ARGS("--version"));
	CHECK_INT(version.code, 0);
	CHECK_STR(version.out.data, "rpg " RPG_VERSION " (c)\n");
	CHECK_STR(version.out.data, "rpg " RPG_VERSION " (c)\n");
	main_result_free(&version);

	MainResult help = run_main(ARGS("--help"));
	CHECK_INT(help.code, 0);
	CHECK(str_starts_with(help.out.data, "usage: rpg [-h] [--version] [--seed SEED]"));
	CHECK_CONTAINS(help.out.data, "Endless turn-based RPG for the terminal.");
	CHECK_CONTAINS(help.out.data, "  --simulate N          run N headless bot games and print a report\n");
	CHECK_STR(help.err.data, "");
	main_result_free(&help);

	// Like argparse: the usage lines, then the error, exit code 2.
	MainResult bad_seed = run_main(ARGS("--seed", "-1"));
	CHECK_INT(bad_seed.code, 2);
	CHECK_STR(bad_seed.out.data, "");
	CHECK_STR(bad_seed.err.data, "usage: rpg [-h] [--version] [--seed SEED] [--lang {en,pt-BR}] [--no-anim]\n"
	                             "           [--data-dir DATA_DIR] [--simulate N] [--vocation VOCATION]\n"
	                             "           [--difficulty DIFFICULTY]\n"
	                             "rpg: error: argument --seed: must be >= 0\n");
	main_result_free(&bad_seed);

	MainResult bogus = run_main(ARGS("--bogus"));
	CHECK_INT(bogus.code, 2);
	CHECK_CONTAINS(bogus.err.data, "rpg: error: unrecognized arguments: --bogus\n");
	main_result_free(&bogus);
}

TEST(SUITE, parse_args_defaults_and_validation) {
	CliOptions options;
	char error[256] = "";
	REQUIRE(parse_cli(0, NULL, &options, error, sizeof(error)));
	CHECK(!options.has_seed);
	CHECK(!options.no_anim);
	CHECK_INT(options.simulate, 0);
	CHECK(options.lang == NULL && options.data_dir == NULL && options.vocation == NULL && options.difficulty == NULL);
	CHECK(!options.exit);

	REQUIRE(parse_cli(
	    ARGS("--seed", "42", "--lang", "pt-BR", "--no-anim", "--data-dir", "x"), &options, error, sizeof(error)));
	CHECK(options.has_seed);
	CHECK_INT(options.seed, 42);
	CHECK(options.lang != NULL && str_eq(options.lang, "pt-BR"));
	CHECK(options.no_anim);
	CHECK(options.data_dir != NULL && str_eq(options.data_dir, "x"));

	REQUIRE(parse_cli(
	    ARGS("--seed=7", "--simulate=3", "--vocation=mage", "--difficulty=easy"), &options, error, sizeof(error)));
	CHECK_INT(options.seed, 7);
	CHECK_INT(options.simulate, 3);
	CHECK(options.vocation != NULL && str_eq(options.vocation, "mage"));
	CHECK(options.difficulty != NULL && str_eq(options.difficulty, "easy"));

	CHECK_STR(usage_error(ARGS("--seed", "-1")), "argument --seed: must be >= 0");
	CHECK_STR(usage_error(ARGS("--seed", "abc")), "argument --seed: invalid _non_negative value: 'abc'");
	CHECK_STR(usage_error(ARGS("--simulate", "0")), "argument --simulate: must be > 0");
	CHECK_STR(usage_error(ARGS("--simulate", "x")), "argument --simulate: invalid _positive value: 'x'");
	CHECK_STR(usage_error(ARGS("--lang", "fr")), "argument --lang: invalid choice: 'fr' (choose from 'en', 'pt-BR')");
	CHECK_STR(usage_error(ARGS("--seed")), "argument --seed: expected one argument");
	CHECK_STR(usage_error(ARGS("--lang")), "argument --lang: expected one argument");
	CHECK_STR(usage_error(ARGS("--simulate")), "argument --simulate: expected one argument");
	CHECK_STR(usage_error(ARGS("--vocation")), "argument --vocation: expected one argument");
	CHECK_STR(usage_error(ARGS("--bogus")), "unrecognized arguments: --bogus");

	REQUIRE(parse_cli(ARGS("-h"), &options, error, sizeof(error)));
	CHECK(options.exit);
	CHECK_STR(options.output, "");
	REQUIRE(parse_cli(ARGS("--seed", "1", "--version"), &options, error, sizeof(error)));
	CHECK(options.exit);
	CHECK_STR(options.output, "rpg " RPG_VERSION " (c)");
}

TEST(SUITE, translator) {
	Translator english;
	Translator portuguese;
	REQUIRE(translator_init(&english, DEFAULT_LOCALE));
	REQUIRE(translator_init(&portuguese, "pt-BR"));
	char *text = TR(&english, "event.gold_looted", P_INT("amount", 5));
	CHECK_STR(text, "You looted 5 gold.");
	free(text);
	text = TR(&portuguese, "event.gold_looted", P_INT("amount", 5));
	CHECK_STR(text, "Voc\xC3\xAA saqueou 5 de ouro.");
	free(text);
	text = translate(&english, "missing.key", NULL, 0);
	CHECK_STR(text, "missing.key");
	free(text);
	text = translate(&english, "event.gold_looted", NULL, 0);
	CHECK_STR(text, "You looted {amount} gold.");
	free(text);
	text = TR(&english, "event.gold_looted", P_STR("amount", "some"), P_INT("unused", 1));
	CHECK_STR(text, "You looted some gold.");
	free(text);
	CHECK(translator_has(&portuguese, "menu.quit"));
	CHECK(!translator_has(&english, "missing.key"));
	CHECK_STR(english.locale, "en");
	CHECK_STR(portuguese.locale, "pt-BR");

	StrBuf line = {0};
	sb_append(&line, "> ");
	translate_into(&line, &english, "event.gold_looted", (const Param[]){P_INT("amount", 7)}, 1);
	CHECK_STR(line.data, "> You looted 7 gold.");
	sb_free(&line);

	Translator french;
	CHECK(!translator_init(&french, "fr"));
	CHECK(locale_supported("en") && locale_supported("pt-BR") && !locale_supported("fr"));
	translator_free(&english);
	translator_free(&portuguese);
}
