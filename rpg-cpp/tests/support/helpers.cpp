#include "support/helpers.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "assets/shared_files.hpp"
#include "infrastructure/data_loader.hpp"

namespace rpg::testing {

namespace fs = std::filesystem;

const domain::GameData& test_data() {
	static const domain::GameData data = infrastructure::load_game_data(assets::embedded_shared());
	return data;
}

domain::GameData with_test_items(domain::GameData data) {
	using domain::ItemDef;
	data.items.push_back(ItemDef{
	    "test_helmet", "Test Helmet", "helmet", "helmet", 0, std::nullopt, {{"armor", 10}, {"maxHp", 50}}, 100});
	data.items.push_back(
	    ItemDef{"test_ring", "Test Ring", "ring", "ring", 0, std::nullopt, {{"critChance", 80}, {"dodge", 90}}, 100});
	data.items.push_back(ItemDef{"test_axe", "Test Axe", "weapon", "axe", 0, std::nullopt, {{"attack", 20}}, 100});
	data.items.push_back(ItemDef{"test_rod", "Test Rod", "weapon", "rod", 0, std::nullopt, {{"attack", 1}}, 100});
	data.index();
	return data;
}

application::GameEngine new_engine(
    const domain::GameData& data, std::string_view vocation, std::string_view difficulty, std::uint64_t seed) {
	auto created = application::GameEngine::new_run(
	    data, application::RunConfig{"Tester", std::string(vocation), std::string(difficulty)}, seed);
	REQUIRE(created.has_value());
	return std::move(created->first);
}

domain::MonsterInstance& fight(application::GameEngine& engine) {
	engine.step(application::NextFight{});
	REQUIRE(engine.state.monster.has_value());
	return *engine.state.monster;
}

void fixed_attack(domain::MonsterInstance& monster, std::int64_t damage, std::string_view element,
    std::optional<domain::StatusOnHit> status) {
	monster.attacks = {domain::MonsterAttack{"test_hit", std::string(element), damage, damage, 1, std::move(status)}};
	monster.hp = 10'000;
	monster.max_hp = 10'000;
}

std::vector<std::string> types_of(const std::vector<application::Event>& events) {
	std::vector<std::string> types;
	for (const auto& event : events) {
		types.push_back(event.type);
	}
	return types;
}

const application::Event* find_event(const std::vector<application::Event>& events, std::string_view type) {
	const auto found = std::ranges::find(events, type, &application::Event::type);
	return found == events.end() ? nullptr : &*found;
}

bool contains_event(const std::vector<application::Event>& events, const application::Event& expected) {
	return std::ranges::contains(events, expected);
}

nlohmann::json event_to_json(const application::Event& event) {
	nlohmann::json result{{"type", event.type}};
	for (const auto& [field, value] : event.fields) {
		std::visit([&](const auto& content) { result[field] = content; }, value);
	}
	return result;
}

nlohmann::json events_to_json(const std::vector<application::Event>& events) {
	nlohmann::json result = nlohmann::json::array();
	for (const auto& event : events) {
		result.push_back(event_to_json(event));
	}
	return result;
}

TempDir::TempDir() {
	static std::atomic<unsigned> counter{0};
	std::random_device device;
	path_ = fs::temp_directory_path() / ("rpg-cpp-test-" + std::to_string(device()) + "-" + std::to_string(++counter));
	fs::create_directories(path_);
}

TempDir::~TempDir() {
	for (int attempt = 0; attempt < 20; ++attempt) {
		std::error_code error;
		fs::remove_all(path_, error);
		if (!error) {
			return;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

FakeClock::FakeClock() : current_(std::chrono::sys_days{std::chrono::year{2026} / 9 / 27} + std::chrono::hours{12}) {}

application::TimePoint FakeClock::now() {
	current_ += std::chrono::seconds{10};
	return current_;
}

std::string read_file(const fs::path& path) {
	std::ifstream input(path, std::ios::binary);
	std::ostringstream content;
	content << input.rdbuf();
	return content.str();
}

void write_file(const fs::path& path, std::string_view content) {
	fs::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output << content;
}

void set_env(const std::string& name, const std::string& value) {
#ifdef _WIN32
	_putenv_s(name.c_str(), value.c_str());
#else
	setenv(name.c_str(), value.c_str(), 1);
#endif
}

void unset_env(const std::string& name) {
#ifdef _WIN32
	_putenv_s(name.c_str(), "");
#else
	unsetenv(name.c_str());
#endif
}

namespace {

// Tests never touch the real save directory and never animate (docs/testing.md).
class IsolatedEnvironment final : public Catch::EventListenerBase {
public:
	using Catch::EventListenerBase::EventListenerBase;

	void testRunStarting(const Catch::TestRunInfo&) override {
		// ctest runs test cases as parallel processes, so each process gets its own directory.
		std::random_device device;
		data_dir_ = fs::temp_directory_path() / ("rpg-cpp-test-data-" + std::to_string(device()));
		set_env("RPG_DATA_DIR", data_dir_.string());
		set_env("RPG_NO_ANIM", "1");
	}

	void testRunEnded(const Catch::TestRunStats&) override {
		std::error_code ignored;
		fs::remove_all(data_dir_, ignored);
	}

private:
	fs::path data_dir_;
};

} // namespace

CATCH_REGISTER_LISTENER(IsolatedEnvironment)

} // namespace rpg::testing
