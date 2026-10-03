#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "application/game_session.hpp"
#include "assets/shared_files.hpp"
#include "domain/definitions.hpp"
#include "infrastructure/i18n.hpp"
#include "infrastructure/repositories.hpp"
#include "presentation/event_text.hpp"

namespace rpg::presentation {

// Controller limits (same values as the reference).
inline constexpr std::size_t max_log_lines = 50;
inline constexpr std::size_t max_name_length = 16;
inline constexpr std::size_t max_quantity_digits = 2;
inline constexpr std::size_t page_size = 10;

// UI screens (docs/tui.md).
enum class View {
	language,
	title,
	difficulty,
	name,
	vocation,
	merchant,
	buy_potions,
	quantity,
	sell,
	equipment,
	stock,
	character,
	battle,
	spells,
	potions,
	game_over,
	hall_of_fame,
	bestiary,
	achievements,
};

// Informative screens page their lines and hide the combat log.
bool is_paged(View view);

// One selectable option. `color` is a rarity, an element, "heal" or empty.
struct MenuOption {
	std::string key;
	std::string label;
	std::string color;
};

// The collaborators of the controller. GameData and the shared tree are borrowed and outlive the controller.
struct Services {
	const domain::GameData* data = nullptr;
	const assets::SharedFs* shared = nullptr;
	std::shared_ptr<infrastructure::SettingsRepository> settings;
	application::Repositories repositories;
	std::shared_ptr<application::Clock> clock;
	std::string version;
};

// What the renderer shows about the monster.
struct MonsterView {
	std::string name;
	std::string creature_id;
	std::int64_t hp = 0;
	std::int64_t max_hp = 0;
	bool is_boss = false;
	std::string element;
	std::string details;
};

// What the renderer shows about the player.
struct PlayerView {
	std::string summary;
	std::string gold;
	std::int64_t hp = 0;
	std::int64_t max_hp = 0;
	std::int64_t mp = 0;
	std::int64_t max_mp = 0;
	std::string xp;
	std::string statuses;
};

// A seed from the OS random source (presentation-level randomness only; the engine never sees it).
std::uint64_t random_seed();

// The framework-independent UI state machine (port of the reference controller). The TUI renders its queries and
// forwards key names ("1", "a", "enter", "escape", "backspace", ...) to press().
class Controller {
public:
	View view = View::language;
	std::optional<application::GameSession> session;
	std::vector<std::string> log;
	std::string message;
	std::string input_buffer;
	bool exit_requested = false;
	std::vector<std::string> animation_cues;
	std::size_t page = 0;
	std::string locale;
	Services services;
	// The last persistence error, shown by the renderer instead of crashing.
	std::optional<std::string> error;

	// seed and locale_override may be empty; seed_source defaults to random_seed. Throws if settings can't be read.
	Controller(Services services, std::optional<std::uint64_t> seed, std::string_view locale_override,
	    std::function<std::uint64_t()> seed_source = {});

	// Translates a key.
	[[nodiscard]] std::string t(std::string_view key, const infrastructure::Params& params = {}) const;

	// ── queries used by renderers ──
	[[nodiscard]] std::string title() const;
	[[nodiscard]] std::vector<MenuOption> options();
	[[nodiscard]] std::vector<std::string> body_lines();
	[[nodiscard]] std::string input_prompt() const;
	[[nodiscard]] std::string header() const;
	[[nodiscard]] std::optional<MonsterView> monster_view() const;
	[[nodiscard]] std::optional<PlayerView> player_view() const;

	// ── input ──
	void press(std::string_view key);

private:
	struct MenuEntry {
		MenuOption option;
		std::function<void()> action;
	};

	// A unique_ptr gives the translator a stable address while the controller itself may move.
	std::unique_ptr<infrastructure::Translator> translator_;
	View language_return_ = View::title;
	std::string difficulty_ = "normal";
	std::string name_;
	std::string potion_id_;
	std::optional<std::uint64_t> seed_;
	std::function<std::uint64_t()> seed_source_;

	void set_locale(std::string_view new_locale);
	[[nodiscard]] EventFormatter formatter() const { return EventFormatter(*services.data, *translator_); }
	[[nodiscard]] application::SessionContext session_context() const;
	[[nodiscard]] std::vector<std::string> weak_elements(const domain::MonsterDef& creature, bool weak) const;
	[[nodiscard]] std::string status_text(const std::vector<domain::ActiveStatus>& statuses) const;

	void text_input(std::string_view key);
	void submit_text();

	std::vector<MenuEntry> menu();
	MenuEntry back(View target);
	std::function<void()> go_to(View target);
	std::function<void()> command(application::Command command);
	std::vector<MenuEntry> title_menu();
	[[nodiscard]] std::string item_label(
	    std::string_view key, const domain::ItemInstance& item, infrastructure::Params params) const;
	std::vector<MenuEntry> potion_shop();
	std::vector<MenuEntry> sell_menu();
	std::vector<MenuEntry> equipment_menu();
	std::vector<MenuEntry> stock_menu();
	std::vector<MenuEntry> spell_menu();
	std::vector<MenuEntry> battle_potions();

	void choose_language(const std::string& new_locale);
	void open_language();
	void new_run();
	void choose_difficulty(const std::string& difficulty_id);
	void choose_vocation(const std::string& vocation_id);
	void continue_run();
	void ask_quantity(const std::string& potion_id);
	void save_and_quit();
	void step(const application::Command& command);
	void push_log(std::string line);
	void record(const application::StepResult& result);

	// Informative screen bodies (controller_body.cpp).
	std::vector<std::string> body();
	application::ProfileService profile_service();
	std::vector<std::string> character_sheet() const;
	std::vector<std::string> game_over_summary() const;
	std::vector<std::string> hall_of_fame();
	std::vector<std::string> bestiary();
	std::vector<std::string> achievements();
};

} // namespace rpg::presentation
