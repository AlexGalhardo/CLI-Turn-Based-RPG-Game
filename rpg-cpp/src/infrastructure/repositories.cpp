#include "infrastructure/repositories.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "infrastructure/i18n.hpp"

namespace rpg::infrastructure {

namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::ordered_json;

namespace {

// The parsed file, or std::nullopt when it does not exist.
std::optional<json> read_json(const fs::path& path) {
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		if (!fs::exists(path)) {
			return std::nullopt;
		}
		throw std::runtime_error("cannot read " + path.filename().string());
	}
	std::ostringstream content;
	content << input.rdbuf();
	try {
		return json::parse(content.str());
	} catch (const json::exception& error) {
		throw std::runtime_error("parse " + path.filename().string() + ": " + error.what());
	}
}

} // namespace

void write_json_atomic(const fs::path& path, const ordered_json& document) {
	fs::create_directories(path.parent_path());
	fs::path temporary = path;
	temporary += ".tmp";
	{
		// Binary mode: "\n" line endings on every OS, like the reference (newline="\n").
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output) {
			throw std::runtime_error("cannot write " + temporary.filename().string());
		}
		output << document.dump(1, '\t') << '\n';
		if (!output) {
			throw std::runtime_error("cannot write " + temporary.filename().string());
		}
	}
	fs::rename(temporary, path);
}

application::TimePoint SystemClock::now() { return std::chrono::system_clock::now(); }

Settings SettingsRepository::load() const {
	const std::optional<json> document = read_json(path_);
	if (!document.has_value()) {
		return Settings{};
	}
	application::check_schema(*document, "settings.json");
	const auto locale = document->value("locale", std::string{});
	if (!is_supported_locale(locale)) {
		return Settings{};
	}
	return Settings{locale};
}

void SettingsRepository::save(const Settings& settings) const {
	ordered_json document{{"schemaVersion", application::schema_version}};
	if (settings.locale.has_value()) {
		document["locale"] = *settings.locale;
	}
	write_json_atomic(path_, document);
}

std::optional<application::SaveGame> FileSaveRepository::load() {
	const std::optional<json> document = read_json(path_);
	if (!document.has_value()) {
		return std::nullopt;
	}
	return application::save_game_from_json(*document);
}

void FileSaveRepository::save(const application::SaveGame& save) {
	write_json_atomic(path_, application::to_json(save));
}

void FileSaveRepository::remove() {
	std::error_code error;
	fs::remove(path_, error);
	if (error && fs::exists(path_)) {
		throw std::runtime_error("delete save.json: " + error.message());
	}
}

void FileHistoryRepository::add(const application::RunRecord& record) {
	write_json_atomic(dir_ / (record.run_id + ".json"), application::to_json(record));
}

std::vector<application::RunRecord> FileHistoryRepository::list() {
	std::vector<application::RunRecord> records;
	if (!fs::exists(dir_)) {
		return records;
	}
	std::vector<fs::path> files;
	for (const auto& entry : fs::directory_iterator(dir_)) {
		if (entry.is_regular_file() && entry.path().extension() == ".json") {
			files.push_back(entry.path());
		}
	}
	// directory_iterator order is unspecified; sort by file name like the reference.
	std::ranges::sort(files, {}, [](const fs::path& path) { return path.filename().string(); });
	for (const fs::path& file : files) {
		if (const std::optional<json> document = read_json(file)) {
			records.push_back(application::run_record_from_json(*document));
		}
	}
	return records;
}

application::Profile FileProfileRepository::load() {
	const std::optional<json> document = read_json(path_);
	if (!document.has_value()) {
		return application::Profile{};
	}
	return application::profile_from_json(*document);
}

void FileProfileRepository::save(const application::Profile& profile) {
	write_json_atomic(path_, application::to_json(profile));
}

application::Repositories file_repositories(const fs::path& data_dir) {
	return application::Repositories{
	    std::make_shared<FileSaveRepository>(data_dir),
	    std::make_shared<FileHistoryRepository>(data_dir),
	    std::make_shared<FileProfileRepository>(data_dir),
	};
}

} // namespace rpg::infrastructure
