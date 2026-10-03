// End-to-end tests: drive the real FTXUI component with key events and read the rendered 100 × 30 screen.
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include "application/bot.hpp"
#include "application/loot.hpp"
#include "application/merchant.hpp"
#include "assets/shared_files.hpp"
#include "infrastructure/art.hpp"
#include "presentation/render.hpp"
#include "presentation/tui/app.hpp"
#include "support/controller_fixture.hpp"

using namespace rpg;
namespace fs = std::filesystem;

namespace {

// Owns the controller, the art library and the FTXUI component, like the real program does.
class Harness {
public:
	Harness(const fs::path& directory, std::string_view locale) :
	    controller_(rpg::testing::services_for(directory), 42, locale), art_(assets::embedded_shared()),
	    app_(controller_, art_, false), component_(app_.component()) {
		app_.on_quit = [this] { quit_ = true; };
	}

	presentation::Controller& controller() { return controller_; }
	presentation::tui::App& app() { return app_; }

	// Sends keys; true when the program asked to quit.
	bool press(const std::vector<std::string>& keys) {
		quit_ = false;
		for (const std::string& key : keys) {
			ftxui::Event event = ftxui::Event::Character(key);
			if (key == "enter") {
				event = ftxui::Event::Return;
			} else if (key == "escape") {
				event = ftxui::Event::Escape;
			} else if (key == "backspace") {
				event = ftxui::Event::Backspace;
			} else if (key == "ctrl+c") {
				event = ftxui::Event::CtrlC;
			}
			component_->OnEvent(event);
		}
		return quit_;
	}

	void type_text(std::string_view text) {
		for (const char character : text) {
			press({std::string(1, character)});
		}
	}

	// The screen as plain text, one line per row (no colours).
	std::string screen() {
		auto screen = ftxui::Screen::Create(
		    ftxui::Dimension::Fixed(presentation::min_columns), ftxui::Dimension::Fixed(presentation::min_rows));
		ftxui::Render(screen, component_->Render());
		std::string result;
		for (int y = 0; y < screen.dimy(); ++y) {
			for (int x = 0; x < screen.dimx(); ++x) {
				// An untouched cell holds an empty string: it is drawn as a space.
				const std::string& character = screen.CellAt(x, y).character;
				result += character.empty() ? std::string(" ") : character;
			}
			result += '\n';
		}
		return result;
	}

private:
	presentation::Controller controller_;
	infrastructure::ArtLibrary art_;
	presentation::tui::App app_;
	ftxui::Component component_;
	bool quit_ = false;
};

bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

template <typename T> std::size_t index_of(const std::vector<T>& items, const T& item) {
	return static_cast<std::size_t>(std::ranges::find(items, item) - items.begin());
}

// The keys a human would press to issue a bot command through the menus.
std::vector<std::string> keys_for(const application::Command& command, presentation::Controller& controller) {
	const domain::GameData& data = *controller.services.data;
	const application::RunState& state = controller.session->state();
	return std::visit(
	    application::Overloaded{
	        [](const application::Attack&) { return std::vector<std::string>{"1"}; },
	        [](const application::Defend&) { return std::vector<std::string>{"4"}; },
	        [&](const application::Cast& cast) {
		        const auto& spells = data.vocation(state.player.vocation_id).spells;
		        return std::vector<std::string>{"2", presentation::list_key(index_of(spells, cast.spell_id))};
	        },
	        [&](const application::UsePotion& use) {
		        std::vector<std::string> owned;
		        for (const auto& potion : data.potions) {
			        if (state.player.potion_count(potion.id) > 0) {
				        owned.push_back(potion.id);
			        }
		        }
		        return std::vector<std::string>{"3", presentation::list_key(index_of(owned, use.potion_id))};
	        },
	        [](const application::NextFight&) { return std::vector<std::string>{"0"}; },
	        [&](const application::BuyPotion& buy) {
		        const auto available = application::available_potions(state, data);
		        std::vector<std::string> keys{"1", presentation::list_key(index_of(available, buy.potion_id))};
		        for (const char digit : std::to_string(buy.quantity)) {
			        keys.emplace_back(1, digit);
		        }
		        keys.emplace_back("enter");
		        keys.emplace_back("0");
		        return keys;
	        },
	        [&](const application::SellItem& sell) {
		        const auto found = std::ranges::find(state.player.bag, sell.uid, &domain::ItemInstance::uid);
		        return std::vector<std::string>{
		            "2", presentation::list_key(static_cast<std::size_t>(found - state.player.bag.begin())), "0"};
	        },
	        [&](const application::Equip& equip) {
		        const auto& vocation = data.vocation(state.player.vocation_id);
		        std::vector<std::int64_t> usable;
		        for (const auto& item : state.player.bag) {
			        if (application::can_use(data.item(item.item_id), vocation)) {
				        usable.push_back(item.uid);
			        }
		        }
		        return std::vector<std::string>{"3", presentation::list_key(index_of(usable, equip.uid)), "0"};
	        },
	        [](const application::Unequip&) { return std::vector<std::string>{"3", "0"}; },
	        [](const application::BuyStockItem& buy) {
		        return std::vector<std::string>{"4", presentation::list_key(static_cast<std::size_t>(buy.index)), "0"};
	        },
	    },
	    command);
}

} // namespace

TEST_CASE("first launch: language, then a new run reaches the merchant", "[e2e]") {
	const rpg::testing::TempDir directory;
	Harness harness(directory.path(), "");
	REQUIRE(harness.controller().view == presentation::View::language);
	harness.press({"2"});
	REQUIRE(contains(harness.screen(), "Nova jornada"));
	harness.press({"2", "3"});
	harness.type_text("Ana");
	harness.press({"enter", "3"});
	const std::string screen = harness.screen();
	REQUIRE(contains(screen, "Ana"));
	REQUIRE(contains(screen, "Mago"));
	REQUIRE(fs::exists(directory.path() / "save.json"));
}

TEST_CASE("battle, merchant, save & quit, then continue", "[e2e]") {
	const rpg::testing::TempDir directory;
	Harness harness(directory.path(), "en");
	REQUIRE(contains(harness.screen(), "C++"));
	harness.press({"2", "2"});
	harness.type_text("Bo");
	harness.press({"enter", "1", "1", "1", "1", "enter"});
	REQUIRE(harness.controller().session->state().player.potion_count("health_potion") == 6);
	harness.press({"0", "5"});
	REQUIRE(contains(harness.screen(), "Equipment"));
	harness.press({"0", "0"});
	const std::string battle = harness.screen();
	INFO(battle);
	REQUIRE(contains(battle, "Round 1"));
	REQUIRE(contains(battle, "HP"));
	REQUIRE(contains(battle, "[1] Attack"));
	REQUIRE(contains(battle, "╭"));
	harness.press({"1", "2", presentation::list_key(0), "3", "escape", "q"});
	REQUIRE(contains(harness.screen(), "Continue"));
	harness.press({"1"});
	REQUIRE(harness.controller().view == presentation::View::merchant);
	REQUIRE(harness.controller().session->info.sessions == 2);
}

TEST_CASE("a whole run by keys until game over", "[e2e]") {
	const rpg::testing::TempDir directory;
	Harness harness(directory.path(), "en");
	harness.press({"2", "3"});
	harness.type_text("Hero");
	harness.press({"enter", "1"});
	auto& controller = harness.controller();
	const application::GreedyBot bot(*controller.services.data);
	for (int step = 0; step < 5000 && controller.session->state().phase != domain::Phase::game_over; ++step) {
		harness.press(keys_for(bot.choose(controller.session->state()), controller));
	}
	REQUIRE(contains(harness.screen(), "GAME OVER"));
	harness.press({"2", "3"});
	REQUIRE(contains(harness.screen(), "Hero"));
	harness.press({"0", "5"});
	REQUIRE(contains(harness.screen(), "[x] First Blood"));
	harness.press({"0", "4", "n", "p", "0"});
	REQUIRE(harness.press({"0"}));
	std::size_t records = 0;
	for ([[maybe_unused]] const auto& entry : fs::directory_iterator(directory.path() / "history")) {
		records += 1;
	}
	REQUIRE(records == 1);
}

TEST_CASE("small terminals get a resize message; Ctrl+C quits", "[e2e]") {
	const rpg::testing::TempDir directory;
	Harness harness(directory.path(), "en");
	harness.app().resize(60, 20);
	REQUIRE(contains(harness.screen(), "resize"));
	REQUIRE(harness.press({"ctrl+c"}));
}

TEST_CASE("animation cues play once per tick", "[e2e]") {
	const rpg::testing::TempDir directory;
	presentation::Controller controller(rpg::testing::services_for(directory.path()), 42, "en");
	infrastructure::ArtLibrary art(assets::embedded_shared());
	presentation::tui::App app(controller, art, true);
	auto component = app.component();
	const auto render = [&] {
		auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(30));
		ftxui::Render(screen, component->Render());
		return screen.ToString();
	};
	// Title → new run → normal → name "Z" → warrior → next fight → attack.
	for (const ftxui::Event& event :
	    {ftxui::Event::Character("2"), ftxui::Event::Character("2"), ftxui::Event::Character("Z"), ftxui::Event::Return,
	        ftxui::Event::Character("1"), ftxui::Event::Character("0"), ftxui::Event::Character("1")}) {
		component->OnEvent(event);
	}
	REQUIRE(controller.view == presentation::View::battle);
	// The renderer took the cues over from the controller; they are drawn one per tick.
	REQUIRE(controller.animation_cues.empty());
	REQUIRE_FALSE(render().empty());
	REQUIRE(component->OnEvent(ftxui::Event::Custom));
	REQUIRE(component->OnEvent(ftxui::Event::Custom));
	REQUIRE_FALSE(render().empty());
	REQUIRE_FALSE(component->OnEvent(ftxui::Event::ArrowUp));
}

TEST_CASE("key names match the other implementations", "[e2e]") {
	REQUIRE(presentation::tui::key_name(ftxui::Event::Return) == "enter");
	REQUIRE(presentation::tui::key_name(ftxui::Event::Escape) == "escape");
	REQUIRE(presentation::tui::key_name(ftxui::Event::Backspace) == "backspace");
	REQUIRE(presentation::tui::key_name(ftxui::Event::Delete) == "backspace");
	REQUIRE(presentation::tui::key_name(ftxui::Event::Character("a")) == "a");
	REQUIRE(presentation::tui::key_name(ftxui::Event::ArrowUp).empty());
}
