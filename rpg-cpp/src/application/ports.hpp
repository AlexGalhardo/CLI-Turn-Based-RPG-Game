#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "application/profile.hpp"
#include "application/save_game.hpp"

namespace rpg::application {

// Ports: abstract classes the application needs and the infrastructure implements (Dependency Inversion).
// Persistence failures are exceptions (I/O errors, NewerSchemaError); the UI controller reports them.

// The time port.
class Clock {
public:
	virtual ~Clock();
	[[nodiscard]] virtual TimePoint now() = 0;
};

// Persists the single active run.
class SaveRepository {
public:
	virtual ~SaveRepository();
	// std::nullopt when there is no save.
	virtual std::optional<SaveGame> load() = 0;
	virtual void save(const SaveGame& save) = 0;
	virtual void remove() = 0;
};

// Stores finished runs.
class HistoryRepository {
public:
	virtual ~HistoryRepository();
	virtual void add(const RunRecord& record) = 0;
	virtual std::vector<RunRecord> list() = 0;
};

// Stores the cross-run profile.
class ProfileRepository {
public:
	virtual ~ProfileRepository();
	virtual Profile load() = 0;
	virtual void save(const Profile& profile) = 0;
};

// The persistence ports, shared by the controller and the sessions it creates.
struct Repositories {
	std::shared_ptr<SaveRepository> saves;
	std::shared_ptr<HistoryRepository> history;
	std::shared_ptr<ProfileRepository> profile;
};

} // namespace rpg::application
