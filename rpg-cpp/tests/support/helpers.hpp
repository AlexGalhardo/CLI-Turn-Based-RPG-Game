#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "application/engine.hpp"
#include "application/events.hpp"
#include "application/ports.hpp"
#include "domain/definitions.hpp"
#include "domain/entities.hpp"

namespace rpg::testing {

// The shared game data, loaded once from the embedded assets. Tests that mutate definitions use a copy.
const domain::GameData& test_data();

// A copy of the data plus deterministic test items (one per slot with every stat), like the reference conftest.
domain::GameData with_test_items(domain::GameData data);

// A run in the merchant phase. The data must outlive the engine.
application::GameEngine new_engine(const domain::GameData& data, std::string_view vocation = "warrior",
    std::string_view difficulty = "normal", std::uint64_t seed = 42);

// Leaves the merchant and returns the spawned monster.
domain::MonsterInstance& fight(application::GameEngine& engine);

// Replaces the monster's attacks by one fixed attack and makes it practically unkillable.
void fixed_attack(domain::MonsterInstance& monster, std::int64_t damage,
    std::string_view element = domain::element::physical, std::optional<domain::StatusOnHit> status = std::nullopt);

std::vector<std::string> types_of(const std::vector<application::Event>& events);
const application::Event* find_event(const std::vector<application::Event>& events, std::string_view type);
bool contains_event(const std::vector<application::Event>& events, const application::Event& expected);

// Events as JSON, for structural comparison with golden files.
nlohmann::json event_to_json(const application::Event& event);
nlohmann::json events_to_json(const std::vector<application::Event>& events);

// A unique temporary directory removed on destruction (retrying briefly: Windows may hold files for a moment).
class TempDir {
public:
	TempDir();
	~TempDir();
	TempDir(const TempDir&) = delete;
	TempDir& operator=(const TempDir&) = delete;
	TempDir(TempDir&&) = delete;
	TempDir& operator=(TempDir&&) = delete;

	[[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
	std::filesystem::path path_;
};

// Starts at 2026-09-27T12:00:00Z and advances 10 seconds on every call, like the reference FakeClock.
class FakeClock final : public application::Clock {
public:
	FakeClock();
	application::TimePoint now() override;

private:
	application::TimePoint current_;
};

std::string read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, std::string_view content);
void set_env(const std::string& name, const std::string& value);
void unset_env(const std::string& name);

} // namespace rpg::testing
