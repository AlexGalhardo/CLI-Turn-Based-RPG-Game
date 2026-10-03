#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "application/simulator.hpp"
#include "assets/shared_files.hpp"
#include "infrastructure/i18n.hpp"
#include "main_run.hpp"
#include "presentation/cli.hpp"
#include "presentation/simulator_report.hpp"
#include "support/helpers.hpp"
#include "version.hpp"

using namespace rpg;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

namespace {

struct RunOutput {
	int code;
	std::string out;
	std::string err;
};

RunOutput run_main(std::vector<std::string_view> args) {
	std::ostringstream out;
	std::ostringstream err;
	const int code = rpg::run(args, out, err);
	return {code, out.str(), err.str()};
}

presentation::CliOptions parse(std::vector<std::string_view> args) {
	const auto result = presentation::parse_cli(args);
	REQUIRE(result.has_value());
	return result->options;
}

std::string usage_error(std::vector<std::string_view> args) {
	const auto result = presentation::parse_cli(args);
	REQUIRE_FALSE(result.has_value());
	return result.error();
}

} // namespace

TEST_CASE("the simulator summarises runs", "[integration][simulator]") {
	const auto& data = rpg::testing::test_data();
	const auto summary = application::simulate(data, "warrior", "normal", 3, 10);
	REQUIRE(summary.has_value());
	REQUIRE(summary->runs == 3);
	REQUIRE(1 <= summary->min_round);
	REQUIRE(summary->min_round <= summary->median_round);
	REQUIRE(summary->median_round <= summary->max_round);
	std::int64_t deaths = 0;
	for (const auto& [creature_id, count] : summary->top_killers) {
		deaths += count;
	}
	REQUIRE(deaths <= 3);
	const auto invalid = application::simulate(data, "warrior", "normal", 0, 1);
	REQUIRE_FALSE(invalid.has_value());
	REQUIRE_THAT(invalid.error(), ContainsSubstring("positive"));
	REQUIRE_FALSE(application::simulate(data, "knight", "normal", 1, 1).has_value());
}

TEST_CASE("the report is an aligned table", "[integration][simulator]") {
	const auto& data = rpg::testing::test_data();
	const application::SimulationSummary summary{"mage", "hard", 2, 3, 3, 4, 5, 5, 2, {{"rat", 2}}};
	const std::string report = presentation::render_report(std::vector{summary}, data);
	REQUIRE(report == "vocation  difficulty  runs  min  p10  median  p90  max  avg lvl  top killers\n"
	                  "--------  ----------  ----  ---  ---  ------  ---  ---  -------  -----------\n"
	                  "mage      hard        2     3    3    4       5    5    2        Rat (2)");
}

TEST_CASE("main runs the simulator and prints the report", "[integration][cli]") {
	const auto result = run_main({"--simulate", "1", "--vocation", "mage", "--difficulty", "hard", "--seed", "5"});
	REQUIRE(result.code == 0);
	REQUIRE_THAT(result.out, ContainsSubstring("median"));
	REQUIRE_THAT(result.out, ContainsSubstring("mage"));
	const auto all = run_main({"--simulate", "1", "--seed", "0"});
	REQUIRE(all.code == 0);
	REQUIRE_THAT(all.out, ContainsSubstring("archer    normal"));
}

TEST_CASE("main rejects an unknown simulator vocation", "[integration][cli]") {
	const auto result = run_main({"--simulate", "1", "--vocation", "knight"});
	REQUIRE(result.code == 2);
	REQUIRE_THAT(result.err, ContainsSubstring("invalid run config"));
}

TEST_CASE("main prints version, help and usage errors", "[integration][cli]") {
	const auto version_output = run_main({"--version"});
	REQUIRE(version_output.code == 0);
	REQUIRE(version_output.out == "rpg " + std::string(rpg::version) + " (cpp)\n");
	const auto help = run_main({"--help"});
	REQUIRE(help.code == 0);
	REQUIRE_THAT(help.out, StartsWith("usage: rpg"));
	const auto bad_seed = run_main({"--seed", "-1"});
	REQUIRE(bad_seed.code == 2);
	REQUIRE_THAT(bad_seed.err, StartsWith("usage: rpg"));
	REQUIRE_THAT(bad_seed.err, ContainsSubstring("rpg: error: argument --seed: must be >= 0"));
}

TEST_CASE("argument parsing: defaults and validation", "[integration][cli]") {
	const auto defaults = parse({});
	REQUIRE_FALSE(defaults.seed.has_value());
	REQUIRE_FALSE(defaults.no_anim);
	REQUIRE(defaults.simulate == 0);

	const auto options = parse({"--seed", "42", "--lang", "pt-BR", "--no-anim", "--data-dir", "x"});
	REQUIRE(options.seed == 42U);
	REQUIRE(options.lang == "pt-BR");
	REQUIRE(options.no_anim);
	REQUIRE(options.data_dir == "x");
	REQUIRE(parse({"--seed=7", "--simulate=3", "--vocation=mage", "--difficulty=easy"}).seed == 7U);

	REQUIRE(usage_error({"--seed", "-1"}) == "argument --seed: must be >= 0");
	REQUIRE(usage_error({"--seed", "abc"}) == "argument --seed: invalid _non_negative value: 'abc'");
	REQUIRE(usage_error({"--simulate", "0"}) == "argument --simulate: must be > 0");
	REQUIRE(usage_error({"--simulate", "x"}) == "argument --simulate: invalid _positive value: 'x'");
	REQUIRE(usage_error({"--lang", "fr"}) == "argument --lang: invalid choice: 'fr' (choose from 'en', 'pt-BR')");
	REQUIRE(usage_error({"--seed"}) == "argument --seed: expected one argument");
	REQUIRE(usage_error({"--lang"}) == "argument --lang: expected one argument");
	REQUIRE(usage_error({"--simulate"}) == "argument --simulate: expected one argument");
	REQUIRE(usage_error({"--vocation"}) == "argument --vocation: expected one argument");
	REQUIRE(usage_error({"--bogus"}) == "unrecognized arguments: --bogus");
	const auto help = presentation::parse_cli(std::vector<std::string_view>{"-h"});
	REQUIRE(help->exit);
	REQUIRE(help->output == presentation::help_text);
}

TEST_CASE("the translator renders templates and falls back", "[integration][i18n]") {
	const infrastructure::Translator english(assets::embedded_shared());
	const infrastructure::Translator portuguese(assets::embedded_shared(), "pt-BR");
	REQUIRE(english.t("event.gold_looted", {{"amount", 5}}) == "You looted 5 gold.");
	REQUIRE(portuguese.t("event.gold_looted", {{"amount", 5}}) == "Você saqueou 5 de ouro.");
	REQUIRE(english.t("missing.key") == "missing.key");
	REQUIRE(english.t("event.gold_looted") == "You looted {amount} gold.");
	REQUIRE(portuguese.has("menu.quit"));
	REQUIRE_FALSE(english.has("missing.key"));
	REQUIRE(english.locale() == "en");
	REQUIRE_THROWS_WITH(infrastructure::Translator(assets::embedded_shared(), "fr"), ContainsSubstring("unsupported"));

	const assets::SharedFs custom{{"i18n/en.json", R"({"a": "{x} {y_z} {} {not closed", "b": "", "flag": "{f}"})"}};
	const infrastructure::Translator templates(custom);
	REQUIRE(templates.t("a", {{"x", "1"}, {"y_z", std::string("2")}}) == "1 2 {} {not closed");
	REQUIRE(templates.t("b") == "b");
	REQUIRE(templates.t("flag", {{"f", true}}) == "true");
	REQUIRE_THROWS(infrastructure::Translator(custom, "pt-BR"));
}
