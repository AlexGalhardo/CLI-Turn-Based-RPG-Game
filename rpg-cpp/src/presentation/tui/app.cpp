#include "presentation/tui/app.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <thread>

#include <ftxui/component/app.hpp>
#include <ftxui/screen/terminal.hpp>

#include "presentation/render.hpp"
#include "version.hpp"

namespace rpg::presentation::tui {

using namespace ftxui;

namespace {

// Layout constants (same as the Textual, Ink and Bubble Tea renderers). Heights include the borders.
constexpr auto animation_interval = std::chrono::milliseconds(500);
constexpr auto timer_resolution = std::chrono::milliseconds(10);
constexpr std::size_t log_lines = 5;
constexpr std::size_t two_column_threshold = 4;
constexpr int column_label_width = 43;
constexpr int art_width = 32;
constexpr int top_height = 8;
constexpr int player_height = 5;
constexpr int log_height = 7;
constexpr int min_menu_height = 10;
constexpr std::string_view accent = "#ffa62b";

Color hex(std::string_view value) {
	if (value.size() != 7 || value.front() != '#') {
		return Color::Default;
	}
	const auto channel = [&](std::size_t offset) {
		return static_cast<std::uint8_t>(std::stoi(std::string(value.substr(offset, 2)), nullptr, 16));
	};
	return Color::RGB(channel(1), channel(3), channel(5));
}

Element styled(const std::string& content, std::string_view color_hex) { return text(content) | color(hex(color_hex)); }

// A rounded accent border with one column of horizontal padding and a fixed total height.
Element panel(Element content, int height) {
	return hbox({text(" "), content | color(Color::Default) | flex, text(" ")}) | borderStyled(ROUNDED, hex(accent)) |
	       size(HEIGHT, EQUAL, height);
}

std::string_view option_color(std::string_view color) {
	if (const std::string_view rarity = rarity_color(color); !rarity.empty()) {
		return rarity;
	}
	if (const std::string_view style = style_color(color); !style.empty()) {
		return style;
	}
	if (color == "heal") {
		return "#5fd75f";
	}
	return element_color(color);
}

std::string upper(std::string text) {
	std::ranges::transform(text, text.begin(),
	    [](char character) { return static_cast<char>(std::toupper(static_cast<unsigned char>(character))); });
	return text;
}

} // namespace

std::string key_name(const Event& event) {
	if (event == Event::Return) {
		return "enter";
	}
	if (event == Event::Escape) {
		return "escape";
	}
	if (event == Event::Backspace || event == Event::Delete) {
		return "backspace";
	}
	if (event.is_character()) {
		return event.character();
	}
	return {};
}

Event auto_battle_event() { return Event::Special("rpg:auto_battle"); }

App::App(Controller& controller, infrastructure::ArtLibrary& art, bool animate) :
    controller_(&controller), art_(&art), animate_(animate), width_(min_columns), height_(min_rows) {}

void App::resize(int width, int height) {
	width_ = width;
	height_ = height;
}

void App::tick() {
	tick_ += 1;
	if (!cues_.empty()) {
		cues_.erase(cues_.begin());
	}
}

bool App::handle(const Event& event) {
	if (event == Event::CtrlC) {
		if (on_quit) {
			on_quit();
		}
		return true;
	}
	if (event == Event::Custom) {
		tick();
		return true;
	}
	if (event == auto_battle_event()) {
		auto_battle_tick();
		return true;
	}
	const std::string key = key_name(event);
	if (key.empty()) {
		return false;
	}
	controller_->press(key);
	if (controller_->exit_requested) {
		if (on_quit) {
			on_quit();
		}
		return true;
	}
	if (controller_->auto_battle_active() && !auto_timer_) {
		start_auto_battle();
	}
	take_cues();
	return true;
}

void App::take_cues() {
	cues_ = animate_ ? controller_->animation_cues : std::vector<std::string>{};
	controller_->animation_cues.clear();
}

void App::start_auto_battle() {
	if (!animate_) {
		controller_->run_auto_battle();
		return;
	}
	auto_timer_ = true;
	if (on_auto_battle_timer) {
		on_auto_battle_timer(controller_->auto_battle_interval_ms());
	}
}

void App::auto_battle_tick() {
	const bool running = controller_->auto_battle_step();
	if (!running && auto_timer_) {
		auto_timer_ = false;
		if (on_auto_battle_timer) {
			on_auto_battle_timer(0);
		}
	}
	take_cues();
}

Component App::component() {
	return CatchEvent(Renderer([this] {
		if (track_terminal) {
			const Dimensions dimensions = Terminal::Size();
			resize(dimensions.dimx, dimensions.dimy);
		}
		return render();
	}),
	    [this](const Event& event) { return handle(event); });
}

Element App::render() {
	if (width_ < min_columns || height_ < min_rows) {
		return text(controller_->t("app.resize", {{"columns", min_columns}, {"rows", min_rows}})) | bold |
		       color(hex("#ffd75f"));
	}
	Elements sections{top(), panel(player_panel(), player_height)};
	int used = top_height + player_height;
	if (!is_paged(controller_->view)) {
		const auto& log = controller_->log;
		Elements lines;
		for (std::size_t i = log.size() > log_lines ? log.size() - log_lines : 0; i < log.size(); ++i) {
			lines.push_back(text(log[i]));
		}
		sections.push_back(panel(vbox(std::move(lines)), log_height));
		used += log_height;
	}
	sections.push_back(panel(menu_panel(), std::max(min_menu_height, height_ - used)));
	return vbox(std::move(sections)) | size(WIDTH, EQUAL, width_);
}

Element App::top() {
	Controller& controller = *controller_;
	const std::optional<MonsterView> monster = controller.monster_view();
	const std::string animation = cues_.empty() ? std::string("idle") : cues_.front();

	infrastructure::Frame frame;
	std::string art_color;
	bool art_bold = false;
	Elements info;
	if (!monster.has_value()) {
		frame = infrastructure::frame_for(art_->load_file("families", "dragon"), "idle", tick_);
		art_color = "#5fd75f";
		art_bold = true;
		info = {
		    text(controller.t("app.title")) | bold,
		    text(controller.t("app.subtitle")) | italic,
		    text(""),
		    text("v" + std::string(version) + " · C++"),
		};
	} else {
		const domain::MonsterDef& creature = controller.services.data->creature(monster->creature_id);
		frame = infrastructure::frame_for(art_->for_creature(creature), animation, tick_);
		art_color = std::string(element_color(monster->element));
		if (animation == "hurt") {
			art_color = "#ff5f5f";
			art_bold = true;
		}
		Element name = text(upper(monster->name)) | bold;
		if (monster->is_boss) {
			name = hbox({styled(controller.t("hud.boss") + " ", "#d75fff") | bold, name});
		} else if (monster->enemy_class == domain::enemy_class::elite) {
			name = hbox({styled(controller.t("hud.elite") + " ", "#ffd75f") | bold, name});
		}
		info = {
		    name,
		    hbox({text("HP ") | bold,
		        styled(bar(monster->hp, monster->max_hp, bar_width), hp_color(monster->hp, monster->max_hp)),
		        text("  " + std::to_string(monster->hp) + "/" + std::to_string(monster->max_hp))}),
		    styled(monster->details, element_color(monster->element)),
		};
	}

	Elements art_lines;
	for (const std::string& line : frame) {
		art_lines.push_back(text(line));
	}
	Element art = vbox(std::move(art_lines)) | color(hex(art_color)) | size(WIDTH, EQUAL, art_width);
	if (art_bold) {
		art = art | bold;
	}
	const Element title = text("─ " + controller.header() + " ");
	const Element content = hbox({text(" "), art, vbox(std::move(info)) | flex, text(" ")}) | color(Color::Default);
	return window(title, content, ROUNDED) | color(hex(accent)) | size(HEIGHT, EQUAL, top_height);
}

Element App::player_panel() {
	const std::optional<PlayerView> player = controller_->player_view();
	if (!player.has_value()) {
		return text("");
	}
	Elements summary{text(player->summary), styled("   " + player->gold, "#ffd75f")};
	if (!player->statuses.empty()) {
		summary.push_back(styled("   " + player->statuses, "#ff5f5f"));
	}
	return vbox({
	    hbox(std::move(summary)),
	    hbox({text("HP ") | bold,
	        styled(bar(player->hp, player->max_hp, bar_width), hp_color(player->hp, player->max_hp)),
	        text("  " + std::to_string(player->hp) + "/" + std::to_string(player->max_hp)) | bold}),
	    hbox({text("MP ") | bold, styled(bar(player->mp, player->max_mp, bar_width), "#5f87ff"),
	        text("  " + std::to_string(player->mp) + "/" + std::to_string(player->max_mp) + "   " + player->xp)}),
	});
}

Element App::menu_panel() {
	Controller& controller = *controller_;
	Elements lines{text(controller.title()) | bold | underlined};
	const std::vector<std::string> body = controller.body_lines();
	const std::vector<std::string> body_colors = controller.body_colors();
	for (std::size_t i = 0; i < body.size(); ++i) {
		Element line = text(body[i]);
		if (const std::string_view line_color = option_color(i < body_colors.size() ? body_colors[i] : "");
		    !line_color.empty()) {
			line = line | color(hex(line_color));
		}
		lines.push_back(line);
	}
	const std::vector<MenuOption> options = controller.options();
	if (!options.empty()) {
		lines.push_back(text(""));
	}
	const std::size_t columns = options.size() > two_column_threshold ? 2 : 1;
	for (std::size_t start = 0; start < options.size(); start += columns) {
		Elements row;
		for (std::size_t i = start; i < std::min(options.size(), start + columns); ++i) {
			const MenuOption& option = options[i];
			row.push_back(styled("[" + upper(option.key) + "] ", "#5fd7ff") | bold);
			const std::string detail = option.detail.empty() ? std::string{} : "  " + option.detail;
			std::string label_text = option.label;
			if (columns > 1) {
				// Keep the detail visible: the label is cut to the room left in the column.
				const std::size_t room = static_cast<std::size_t>(column_label_width) - utf8_length(detail);
				label_text = utf8_prefix(label_text, room);
			}
			Element label = text(label_text);
			if (const std::string_view label_color = option_color(option.color); !label_color.empty()) {
				label = label | color(hex(label_color));
			}
			if (!detail.empty()) {
				Element detail_element = text(detail);
				if (const std::string_view detail_color = option_color(option.detail_color); !detail_color.empty()) {
					detail_element = detail_element | color(hex(detail_color));
				}
				label = hbox({label, detail_element});
			}
			if (columns > 1) {
				label = hbox({label | size(WIDTH, EQUAL, column_label_width), text(" ")});
			}
			row.push_back(label);
		}
		lines.push_back(hbox(std::move(row)));
	}
	if (const std::string prompt = controller.input_prompt(); !prompt.empty()) {
		lines.push_back(text(""));
		lines.push_back(text(prompt) | bold);
	}
	if (!controller.message.empty()) {
		lines.push_back(text(""));
		lines.push_back(styled(controller.message, "#ff5f5f") | bold);
	}
	if (controller.error.has_value()) {
		lines.push_back(text(""));
		lines.push_back(styled(*controller.error, "#ff5f5f"));
	}
	return vbox(std::move(lines));
}

void run_tui(Controller& controller, infrastructure::ArtLibrary& art, bool animate) {
	auto screen = ftxui::App::Fullscreen();
	App app(controller, art, animate);
	app.track_terminal = true;
	app.on_quit = screen.ExitLoopClosure();

	// The animation and auto-battle clocks live outside the UI thread and only post events; the UI state is touched by
	// the loop. The auto-battle interval (0 = stopped) is the only value shared with the UI thread.
	std::atomic<bool> running{animate};
	std::atomic<std::int64_t> auto_interval_ms{0};
	app.on_auto_battle_timer = [&auto_interval_ms](std::int64_t interval) { auto_interval_ms.store(interval); };
	std::thread ticker;
	if (animate) {
		ticker = std::thread([&] {
			using clock = std::chrono::steady_clock;
			auto next_animation = clock::now() + animation_interval;
			auto next_auto_battle = clock::time_point::max();
			while (running.load()) {
				std::this_thread::sleep_for(timer_resolution);
				const auto now = clock::now();
				if (now >= next_animation) {
					next_animation = now + animation_interval;
					screen.PostEvent(Event::Custom);
				}
				const std::int64_t interval = auto_interval_ms.load();
				if (interval <= 0) {
					next_auto_battle = clock::time_point::max();
				} else if (next_auto_battle == clock::time_point::max()) {
					next_auto_battle = now + std::chrono::milliseconds(interval);
				} else if (now >= next_auto_battle) {
					next_auto_battle = now + std::chrono::milliseconds(interval);
					screen.PostEvent(auto_battle_event());
				}
			}
		});
	}
	screen.Loop(app.component());
	running.store(false);
	if (ticker.joinable()) {
		ticker.join();
	}
}

} // namespace rpg::presentation::tui
