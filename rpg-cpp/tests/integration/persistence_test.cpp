#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "application/bot.hpp"
#include "application/game_session.hpp"
#include "application/profile.hpp"
#include "application/save_game.hpp"
#include "infrastructure/repositories.hpp"
#include "support/helpers.hpp"

using namespace rpg;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

const fs::path fixtures_dir{RPG_FIXTURES_DIR};

application::SessionContext context_for(const fs::path& directory, std::string version = "1") {
	return application::SessionContext{
	    infrastructure::file_repositories(directory), std::make_shared<rpg::testing::FakeClock>(), std::move(version)};
}

application::GameSession start(const application::SessionContext& context, std::string vocation, std::string difficulty,
    std::uint64_t seed, std::string name = "Alex") {
	auto started = application::GameSession::start(rpg::testing::test_data(),
	    application::RunConfig{std::move(name), std::move(vocation), std::move(difficulty)}, seed, context);
	REQUIRE(started.has_value());
	return std::move(started->first);
}

json read_json(const fs::path& path) { return json::parse(rpg::testing::read_file(path)); }

} // namespace

TEST_CASE("a new session autosaves at the merchant", "[integration][persistence]") {
	const rpg::testing::TempDir directory;
	const auto context = context_for(directory.path(), "9.9.9");
	auto started = application::GameSession::start(
	    rpg::testing::test_data(), application::RunConfig{"Alex", "archer", "normal"}, 5, context);
	REQUIRE(started.has_value());
	auto& [session, events] = *started;
	REQUIRE(events.front().type == "run_started");
	json save = read_json(directory.path() / "save.json");
	REQUIRE(save["schemaVersion"] == 1);
	REQUIRE(save["implementation"] == "cpp");
	REQUIRE(save["gameVersion"] == "9.9.9");
	REQUIRE(save["session"]["runId"] == session.info.run_id);
	REQUIRE(save["run"]["phase"] == "merchant");
	session.step(application::BuyPotion{"health_potion", 1});
	save = read_json(directory.path() / "save.json");
	REQUIRE(save["run"]["player"]["potions"]["health_potion"] == 6);
	// Written like the reference: tab indentation and a final newline.
	const std::string text = rpg::testing::read_file(directory.path() / "save.json");
	REQUIRE(text.starts_with("{\n\t\"schemaVersion\": 1,"));
	REQUIRE(text.ends_with("}\n"));
}

TEST_CASE("an invalid config does not start a session", "[integration][persistence]") {
	const rpg::testing::TempDir directory;
	const auto started = application::GameSession::start(rpg::testing::test_data(),
	    application::RunConfig{"Alex", "knight", "normal"}, 5, context_for(directory.path()));
	REQUIRE_FALSE(started.has_value());
	REQUIRE_FALSE(fs::exists(directory.path() / "save.json"));
}

TEST_CASE("quitting mid-battle resumes from the last merchant", "[integration][persistence]") {
	const rpg::testing::TempDir directory;
	const auto context = context_for(directory.path());
	auto session = start(context, "warrior", "normal", 5);
	session.step(application::NextFight{});
	session.step(application::Attack{});
	REQUIRE(session.state().phase == domain::Phase::battle);
	session.save_and_quit();

	auto resumed = application::GameSession::resume(rpg::testing::test_data(), context);
	REQUIRE(resumed.has_value());
	REQUIRE(resumed->state().phase == domain::Phase::merchant);
	REQUIRE(resumed->state().round == 0);
	REQUIRE(resumed->info.sessions == 2);
	REQUIRE(resumed->info.play_time_seconds > 0);
	REQUIRE(resumed->info.run_id == session.info.run_id);
}

TEST_CASE("resume without a save", "[integration][persistence]") {
	const rpg::testing::TempDir directory;
	REQUIRE_FALSE(
	    application::GameSession::resume(rpg::testing::test_data(), context_for(directory.path())).has_value());
}

TEST_CASE("death writes history and profile, and deletes the save", "[integration][persistence]") {
	const rpg::testing::TempDir directory;
	const auto context = context_for(directory.path());
	auto session = start(context, "mage", "hard", 3, "Bot");
	const application::GreedyBot bot(rpg::testing::test_data());
	std::vector<std::string> unlocked;
	while (session.state().phase != domain::Phase::game_over) {
		for (const auto& achievement : session.step(bot.choose(session.state())).achievements) {
			unlocked.push_back(achievement.id);
		}
	}
	REQUIRE_FALSE(fs::exists(directory.path() / "save.json"));
	const auto records = context.repositories.history->list();
	REQUIRE(records.size() == 1);
	const application::RunRecord& record = records.front();
	REQUIRE(record.round == session.state().round);
	REQUIRE(record.death_cause == session.state().death_cause.value_or(""));
	REQUIRE(record.implementation_name == "cpp");
	REQUIRE(application::parse_timestamp(record.ended_at) > application::parse_timestamp(record.started_at));
	REQUIRE(record == *session.finished_record);

	const application::Profile profile = context.repositories.profile->load();
	REQUIRE(profile.hall_of_fame.front().run_id == record.run_id);
	REQUIRE(std::ranges::contains(unlocked, "first_blood"));
	REQUIRE(profile.achievements.contains("first_blood"));
	std::int64_t kills = 0;
	for (const auto& [monster_id, entry] : profile.bestiary) {
		kills += entry.kills;
	}
	REQUIRE(kills == session.state().round - 1);
	session.save_and_quit();
	REQUIRE_FALSE(fs::exists(directory.path() / "save.json"));
}

TEST_CASE("a newer schema is refused", "[integration][persistence]") {
	const rpg::testing::TempDir directory;
	rpg::testing::write_file(directory.path() / "save.json", R"({"schemaVersion": 99})");
	REQUIRE_THROWS_AS(infrastructure::FileSaveRepository(directory.path()).load(), application::NewerSchemaError);
	rpg::testing::write_file(directory.path() / "profile.json", R"({"schemaVersion": 99})");
	REQUIRE_THROWS_AS(infrastructure::FileProfileRepository(directory.path()).load(), application::NewerSchemaError);
	rpg::testing::write_file(directory.path() / "settings.json", R"({"schemaVersion": 99})");
	REQUIRE_THROWS_AS(infrastructure::SettingsRepository(directory.path()).load(), application::NewerSchemaError);
	rpg::testing::write_file(directory.path() / "history" / "x.json", R"({"schemaVersion": 99})");
	REQUIRE_THROWS_AS(infrastructure::FileHistoryRepository(directory.path()).list(), application::NewerSchemaError);
	rpg::testing::write_file(directory.path() / "save.json", "{ broken");
	REQUIRE_THROWS_AS(infrastructure::FileSaveRepository(directory.path()).load(), std::runtime_error);
}

TEST_CASE("settings round trip", "[integration][persistence]") {
	const rpg::testing::TempDir directory;
	const infrastructure::SettingsRepository repository(directory.path());
	REQUIRE(repository.load() == infrastructure::Settings{});
	repository.save(infrastructure::Settings{"pt-BR"});
	REQUIRE(repository.load() == infrastructure::Settings{"pt-BR"});
	repository.save(infrastructure::Settings{});
	REQUIRE(repository.load() == infrastructure::Settings{});
	rpg::testing::write_file(directory.path() / "settings.json", R"({"schemaVersion": 1, "locale": "fr"})");
	REQUIRE(repository.load() == infrastructure::Settings{});
}

TEST_CASE("profile round trip and Hall of Fame order", "[integration][persistence]") {
	const rpg::testing::TempDir directory;
	application::ProfileService service(rpg::testing::test_data(), application::Profile{});
	for (std::int64_t index = 0; index < 12; ++index) {
		service.record_finished_run(application::HallOfFameEntry{"run" + std::to_string(index), "A", "mage", "normal",
		    index % 5, index, std::format("2026-01-{:02}T00:00:00Z", index + 1)});
	}
	const auto& hall = service.profile.hall_of_fame;
	REQUIRE(hall.size() == 10);
	REQUIRE(hall[0].round == 4);
	REQUIRE(hall[1].round == 4);
	REQUIRE(hall[2].round == 3);
	REQUIRE(hall[0].level > hall[1].level);
	infrastructure::FileProfileRepository repository(directory.path());
	repository.save(service.profile);
	REQUIRE(repository.load() == service.profile);
	REQUIRE_FALSE(service.revealed("rat"));
}

TEST_CASE("run id, timestamps and clock", "[integration][persistence]") {
	const application::TimePoint moment = std::chrono::sys_days{std::chrono::year{2026} / 1 / 2} +
	                                      std::chrono::hours{3} + std::chrono::minutes{4} + std::chrono::seconds{5};
	REQUIRE(application::make_run_id(moment, 42) == "20260102T030405Z-42");
	REQUIRE(application::format_timestamp(moment) == "2026-01-02T03:04:05Z");
	REQUIRE(application::parse_timestamp("2026-01-02T03:04:05Z") == moment);
	REQUIRE_THROWS_AS(application::parse_timestamp("2026-01-02 03:04:05"), std::invalid_argument);
	REQUIRE_THROWS_AS(application::parse_timestamp("2026-13-02T03:04:05Z"), std::invalid_argument);
	REQUIRE_THROWS_AS(application::parse_timestamp("2026-0a-02T03:04:05Z"), std::invalid_argument);
	infrastructure::SystemClock clock;
	REQUIRE(clock.now() > moment);
}

TEST_CASE("a save written by the Python reference continues identically", "[integration][persistence]") {
	const rpg::testing::TempDir directory;
	fs::copy_file(fixtures_dir / "python_save.json", directory.path() / "save.json");
	fs::copy_file(fixtures_dir / "python_profile.json", directory.path() / "profile.json");
	const auto context = context_for(directory.path());

	const auto save = context.repositories.saves->load();
	REQUIRE(save.has_value());
	REQUIRE(save->implementation_name == "python");
	// The parsed run serialises back to exactly the Python document.
	REQUIRE(json::parse(application::to_json(save->run).dump()) == read_json(fixtures_dir / "python_save.json")["run"]);
	REQUIRE(context.repositories.profile->load().bestiary.size() > 0);

	auto session = application::GameSession::resume(rpg::testing::test_data(), context);
	REQUIRE(session.has_value());
	REQUIRE(session->info.sessions == 2);
	const application::GreedyBot bot(rpg::testing::test_data());
	std::int64_t steps = 0;
	while (session->state().phase != domain::Phase::game_over) {
		session->step(bot.choose(session->state()));
		steps += 1;
	}
	const json expected = read_json(fixtures_dir / "python_save_continued.json");
	const auto& state = session->state();
	REQUIRE(steps == expected["steps"]);
	REQUIRE(state.round == expected["round"]);
	REQUIRE(state.player.level == expected["level"]);
	REQUIRE(state.player.gold == expected["gold"]);
	REQUIRE(session->engine.rng_state() == expected["rngState"]);
	REQUIRE(state.death_cause == expected["deathCause"].get<std::string>());
	REQUIRE(json::parse(application::to_json(state.stats).dump()) == expected["stats"]);
}
