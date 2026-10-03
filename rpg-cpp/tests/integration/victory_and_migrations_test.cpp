// The victory phase through the session (save, resume, history, Hall of Fame) and the schema 1 → 2 migrations.
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "application/game_session.hpp"
#include "infrastructure/migrations.hpp"
#include "infrastructure/repositories.hpp"
#include "support/helpers.hpp"

using namespace rpg;
using application::Event;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr int max_swings = 200;

application::SessionContext context_for(const fs::path& directory) {
	return application::SessionContext{
	    infrastructure::file_repositories(directory), std::make_shared<rpg::testing::FakeClock>(), "1"};
}

json read_json(const fs::path& path) { return json::parse(rpg::testing::read_file(path)); }

application::GameSession start(
    const application::SessionContext& context, const application::RunConfig& config, std::uint64_t seed) {
	auto started = application::GameSession::start(rpg::testing::test_data(), config, seed, context);
	REQUIRE(started.has_value());
	return std::move(started->first);
}

application::GameSession win_final_fight(const application::SessionContext& context) {
	auto session = start(context, application::RunConfig{"Vic", "warrior", "easy", true}, 8);
	const auto& data = rpg::testing::test_data();
	session.state().round = data.balance.final_round - 1;
	session.step(application::NextFight{});
	REQUIRE(session.state().monster->creature_id == "ferumbras");
	REQUIRE(session.state().monster->enemy_class == "boss");
	for (int swing = 0; swing < max_swings && session.state().phase == domain::Phase::battle; ++swing) {
		session.state().player.hp = 1'000'000;
		session.state().monster->hp = 1;
		session.step(application::Attack{});
	}
	REQUIRE(session.state().phase == domain::Phase::victory);
	return session;
}

// Turns a current save into what version 1 wrote: no M8 fields and the old `epic` rarity.
json as_v1(json document) {
	document["schemaVersion"] = 1;
	json& run = document["run"];
	run["config"].erase("autoEquip");
	run.erase("won");
	run["player"]["equipment"]["weapon"]["rarity"] = "epic";
	json& stats = run["stats"];
	for (const char* key : {"itemsAutoEquipped", "elitesKilled", "potionsDropped"}) {
		stats.erase(key);
	}
	stats["itemsDropped"] = json{{"epic", 2}, {"legendary", 1}};
	stats["droppedItems"] = json::array({json{{"itemId", "sword"}, {"rarity", "epic"}, {"round", 3}}});
	return document;
}

} // namespace

TEST_CASE("a victory is saved, resumed and ended as won", "[integration][victory]") {
	const rpg::testing::TempDir directory;
	const auto context = context_for(directory.path());
	win_final_fight(context);
	const json save = read_json(directory.path() / "save.json");
	REQUIRE(save["run"]["phase"] == "victory");
	REQUIRE(save["run"]["won"] == true);

	auto resumed = application::GameSession::resume(rpg::testing::test_data(), context);
	REQUIRE(resumed.has_value());
	REQUIRE(resumed->state().phase == domain::Phase::victory);
	const auto result = resumed->step(application::EndRun{});
	REQUIRE(result.events == std::vector<Event>{Event{"run_ended", {{"won", true}}}});
	REQUIRE(context.repositories.profile->load().achievements.contains("conqueror"));
	REQUIRE_FALSE(fs::exists(directory.path() / "save.json"));
	const auto records = context.repositories.history->list();
	REQUIRE(records.front().won);
	REQUIRE(records.front().death_cause.empty());
	REQUIRE(context.repositories.profile->load().hall_of_fame.front().won);
}

TEST_CASE("continuing after a victory keeps the run won", "[integration][victory]") {
	const rpg::testing::TempDir directory;
	auto session = win_final_fight(context_for(directory.path()));
	const auto& data = rpg::testing::test_data();
	REQUIRE(session.step(application::ContinueRun{}).events ==
	        std::vector<Event>{Event{"merchant_entered", {{"round", data.balance.final_round}}}});
	REQUIRE(session.state().phase == domain::Phase::merchant);
	REQUIRE(session.state().won);
	session.step(application::NextFight{});
	REQUIRE(session.state().round == data.balance.final_round + 1);
}

TEST_CASE("a version 1 save is migrated", "[integration][migrations]") {
	const rpg::testing::TempDir directory;
	start(context_for(directory.path()), application::RunConfig{"Old", "warrior", "normal", false}, 4);
	const json current = read_json(directory.path() / "save.json");
	rpg::testing::write_file(directory.path() / "save.json", as_v1(current).dump());

	const auto loaded = infrastructure::FileSaveRepository(directory.path()).load();
	REQUIRE(loaded.has_value());
	const application::RunState& run = loaded->run;
	REQUIRE_FALSE(run.config.auto_equip);
	REQUIRE_FALSE(run.won);
	REQUIRE(run.player.equipment.begin()->second.rarity == "legendary");
	REQUIRE(run.stats.items_dropped == domain::CountMap{{"legendary", 3}});
	REQUIRE(run.stats.dropped_items.front().rarity == "legendary");
	REQUIRE(run.stats.elites_killed == 0);
}

TEST_CASE("a version 1 monster gets its class from isBoss", "[integration][migrations]") {
	const rpg::testing::TempDir directory;
	auto session = start(context_for(directory.path()), application::RunConfig{"Old", "mage", "normal", false}, 4);
	session.state().round = 9;
	session.step(application::NextFight{});
	json document = read_json(directory.path() / "save.json");
	document["run"]["monster"] = json::parse(application::to_json(session.state()).dump())["monster"];
	json old = as_v1(document);
	old["run"]["monster"].erase("enemyClass");
	rpg::testing::write_file(directory.path() / "save.json", old.dump());
	const auto loaded = infrastructure::FileSaveRepository(directory.path()).load();
	REQUIRE(loaded.has_value());
	REQUIRE(loaded->run.monster.has_value());
	REQUIRE(loaded->run.monster->enemy_class == "boss");
}

TEST_CASE("version 1 history and profile are migrated", "[integration][migrations]") {
	const rpg::testing::TempDir directory;
	const fs::path won = directory.path() / "won";
	auto session = win_final_fight(context_for(won));
	session.step(application::EndRun{});
	fs::path record_path;
	for (const auto& entry : fs::directory_iterator(won / "history")) {
		record_path = entry.path();
	}
	json record = read_json(record_path);
	record["schemaVersion"] = 1;
	record.erase("won");
	for (const char* key : {"itemsAutoEquipped", "elitesKilled", "potionsDropped"}) {
		record["stats"].erase(key);
	}
	const fs::path old = directory.path() / "old";
	rpg::testing::write_file(old / "history" / record_path.filename(), record.dump());
	REQUIRE_FALSE(infrastructure::FileHistoryRepository(old).list().front().won);

	json profile = read_json(won / "profile.json");
	profile["schemaVersion"] = 1;
	for (json& entry : profile["hallOfFame"]) {
		entry.erase("won");
	}
	rpg::testing::write_file(old / "profile.json", profile.dump());
	REQUIRE_FALSE(infrastructure::FileProfileRepository(old).load().hall_of_fame.front().won);
}

TEST_CASE("migrations leave current documents alone", "[integration][migrations]") {
	json settings{{"schemaVersion", 2}, {"autoEquip", true}, {"battleSpeed", 2}};
	const json before = settings;
	REQUIRE(infrastructure::migrate_settings(settings) == before);
	json profile{{"schemaVersion", 2}, {"hallOfFame", json::array()}};
	REQUIRE(infrastructure::migrate_profile(profile)["schemaVersion"] == 2);
	json old_settings{{"locale", "en"}};
	REQUIRE(infrastructure::migrate_settings(old_settings) ==
	        json{{"schemaVersion", 2}, {"locale", "en"}, {"autoEquip", false}, {"battleSpeed", 1}});
}
