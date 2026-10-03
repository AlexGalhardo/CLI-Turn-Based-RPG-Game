// Golden (parity) tests: replays every shared/golden scenario recorded by the Python reference.
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <nlohmann/json.hpp>

#include "application/bot.hpp"
#include "application/commands.hpp"
#include "application/engine.hpp"
#include "domain/rng.hpp"
#include "support/helpers.hpp"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace rpg;

namespace {

const fs::path golden_dir{RPG_GOLDEN_DIR};

std::vector<std::string> scenario_files(bool bots_only = false) {
	std::vector<std::string> files;
	for (const auto& entry : fs::directory_iterator(golden_dir)) {
		const std::string name = entry.path().filename().string();
		if (entry.path().extension() == ".json" && name != "prng.json" &&
		    (!bots_only || name.starts_with("bot-full-run-"))) {
			files.push_back(name);
		}
	}
	std::ranges::sort(files);
	return files;
}

json load(const std::string& file) { return json::parse(testing::read_file(golden_dir / file)); }

application::RunConfig config_of(const json& scenario) {
	const json& config = scenario.at("config");
	return {config.at("name").get<std::string>(), config.at("vocation").get<std::string>(),
	    config.at("difficulty").get<std::string>(), config.at("autoEquip").get<bool>()};
}

json final_state(const application::GameEngine& engine) {
	const auto& state = engine.state;
	return json{
	    {"phase", domain::to_string(state.phase)},
	    {"round", state.round},
	    {"turn", state.turn},
	    {"level", state.player.level},
	    {"xp", state.player.xp},
	    {"magicLevel", state.player.magic_level},
	    {"hp", state.player.hp},
	    {"mp", state.player.mp},
	    {"gold", state.player.gold},
	    {"rngState", engine.rng_state()},
	    {"nextItemUid", state.next_item_uid},
	    // ordered_json (save format) → json (structural comparison) through its text.
	    {"stats", json::parse(application::to_json(state.stats).dump())},
	};
}

} // namespace

TEST_CASE("PRNG matches the reference vectors", "[golden]") {
	const json document = load("prng.json");
	for (const json& vector : document.at("vectors")) {
		domain::Rng rng(vector.at("seed").get<std::uint64_t>());
		for (const json& expected : vector.at("outputs")) {
			REQUIRE(rng.next_u32() == expected.get<std::uint32_t>());
		}
	}
}

TEST_CASE("golden files exist", "[golden]") {
	REQUIRE(scenario_files().size() >= 12);
	REQUIRE(std::ranges::contains(scenario_files(), "bot-victory-continue-archer-easy.json"));
}

TEST_CASE("every golden scenario replays identically", "[golden]") {
	const std::string file = GENERATE(from_range(scenario_files()));
	CAPTURE(file);
	const json scenario = load(file);
	const auto& data = testing::test_data();

	auto created =
	    application::GameEngine::new_run(data, config_of(scenario), scenario.at("seed").get<std::uint64_t>());
	REQUIRE(created.has_value());
	auto& [engine, first] = *created;
	REQUIRE(testing::events_to_json(first) == scenario.at("events").at(0));

	const json& commands = scenario.at("commands");
	for (std::size_t index = 0; index < commands.size(); ++index) {
		const auto events = engine.step(application::command_from_json(commands[index]));
		const json& expected = scenario.at("events").at(index + 1);
		if (testing::events_to_json(events) != expected) {
			FAIL("command #" << index + 1 << " " << commands[index].dump() << "\n got  "
			                 << testing::events_to_json(events).dump() << "\n want " << expected.dump());
		}
	}

	REQUIRE(final_state(engine) == scenario.at("finalState"));
	// Save-format parity: the C++ run state serialises exactly like the Python one, both ways.
	const json final_run = json::parse(application::to_json(engine.state).dump());
	REQUIRE(final_run == scenario.at("finalRun"));
	const application::RunState restored = application::run_state_from_json(scenario.at("finalRun"));
	REQUIRE(restored == engine.state);
	REQUIRE(json::parse(application::to_json(restored).dump()) == scenario.at("finalRun"));
}

TEST_CASE("the bot issues exactly the recorded Python commands", "[golden]") {
	const std::string file = GENERATE(from_range(scenario_files(true)));
	CAPTURE(file);
	const json scenario = load(file);
	const auto& data = testing::test_data();

	auto created =
	    application::GameEngine::new_run(data, config_of(scenario), scenario.at("seed").get<std::uint64_t>());
	REQUIRE(created.has_value());
	auto& engine = created->first;
	const application::GreedyBot bot(data);
	json commands = json::array();
	while (engine.state.phase != domain::Phase::game_over) {
		const application::Command command = bot.choose(engine.state);
		commands.push_back(application::command_to_json(command));
		engine.step(command);
	}
	REQUIRE(commands == scenario.at("commands"));
}

// The reference generator's `bot_victory_then_continue`: the bot wins the run, probes invalid commands in the victory
// phase, continues and stops at the merchant three rounds later.
TEST_CASE("the victory-continue scenario issues the recorded commands", "[golden]") {
	const json scenario = load("bot-victory-continue-archer-easy.json");
	const auto& data = testing::test_data();
	auto created =
	    application::GameEngine::new_run(data, config_of(scenario), scenario.at("seed").get<std::uint64_t>());
	REQUIRE(created.has_value());
	auto& engine = created->first;
	const application::GreedyBot bot(data);
	std::vector<application::Command> at_victory{
	    application::Attack{}, application::NextFight{}, application::ContinueRun{}};
	constexpr std::int64_t extra_rounds = 3;
	json commands = json::array();
	while (engine.state.phase != domain::Phase::game_over) {
		const auto& state = engine.state;
		application::Command command;
		if (state.phase == domain::Phase::victory) {
			REQUIRE_FALSE(at_victory.empty());
			command = at_victory.front();
			at_victory.erase(at_victory.begin());
		} else if (state.won && state.phase == domain::Phase::merchant &&
		           state.round >= data.balance.final_round + extra_rounds) {
			break;
		} else {
			command = bot.choose(state);
		}
		commands.push_back(application::command_to_json(command));
		engine.step(command);
	}
	REQUIRE(commands == scenario.at("commands"));
	REQUIRE(engine.state.won);
}
