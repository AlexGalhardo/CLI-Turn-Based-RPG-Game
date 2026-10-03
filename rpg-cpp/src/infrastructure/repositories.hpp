#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "application/ports.hpp"

namespace rpg::infrastructure {

// Writes to a temp file then renames it, so a crash never leaves a half-written save.
void write_json_atomic(const std::filesystem::path& path, const nlohmann::ordered_json& document);

// The real clock.
class SystemClock final : public application::Clock {
public:
	application::TimePoint now() override;
};

// The player's preferences (settings.json). An empty optional locale means "ask on first launch".
struct Settings {
	std::optional<std::string> locale;

	bool operator==(const Settings&) const = default;
};

class SettingsRepository {
public:
	explicit SettingsRepository(const std::filesystem::path& data_dir) : path_(data_dir / "settings.json") {}

	// An unknown locale is ignored.
	[[nodiscard]] Settings load() const;
	void save(const Settings& settings) const;

private:
	std::filesystem::path path_;
};

class FileSaveRepository final : public application::SaveRepository {
public:
	explicit FileSaveRepository(const std::filesystem::path& data_dir) : path_(data_dir / "save.json") {}

	std::optional<application::SaveGame> load() override;
	void save(const application::SaveGame& save) override;
	void remove() override;

private:
	std::filesystem::path path_;
};

class FileHistoryRepository final : public application::HistoryRepository {
public:
	explicit FileHistoryRepository(const std::filesystem::path& data_dir) : dir_(data_dir / "history") {}

	void add(const application::RunRecord& record) override;
	// Every finished run, sorted by file name.
	std::vector<application::RunRecord> list() override;

private:
	std::filesystem::path dir_;
};

class FileProfileRepository final : public application::ProfileRepository {
public:
	explicit FileProfileRepository(const std::filesystem::path& data_dir) : path_(data_dir / "profile.json") {}

	// The profile, empty when the file is missing.
	application::Profile load() override;
	void save(const application::Profile& profile) override;

private:
	std::filesystem::path path_;
};

// Wires the three file repositories of a data directory.
application::Repositories file_repositories(const std::filesystem::path& data_dir);

} // namespace rpg::infrastructure
