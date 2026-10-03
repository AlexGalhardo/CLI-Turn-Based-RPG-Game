#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "infrastructure/art.hpp"
#include "presentation/controller.hpp"

namespace rpg::presentation::tui {

// Maps FTXUI events to the controller's key names (same names as the other implementations); "" when unmapped.
std::string key_name(const ftxui::Event& event);

// The event posted by the auto-battle timer: one auto-battle turn.
ftxui::Event auto_battle_event();

// The FTXUI renderer of the controller: it only draws the controller's queries and forwards key names to press().
// The same layout as the Textual, Ink and Bubble Tea renderers (docs/tui.md).
class App {
public:
	App(Controller& controller, infrastructure::ArtLibrary& art, bool animate);

	// Called when the player quits (title [0] or Ctrl+C). The real program passes the loop's exit closure.
	std::function<void()> on_quit;
	// When set, the screen size is read from the terminal on every frame (the real program); tests call resize().
	bool track_terminal = false;
	// Starts (interval in ms) or stops (0) the auto-battle timer that posts auto_battle_event(). The real program
	// passes a closure driving its timer thread; tests call auto_battle_tick() themselves.
	std::function<void(std::int64_t)> on_auto_battle_timer;

	[[nodiscard]] Controller& controller() { return *controller_; }

	// The interactive component: renders the screen and handles key events.
	ftxui::Component component();

	// Handles one event; true when consumed.
	bool handle(const ftxui::Event& event);

	// One animation step (500 ms): advances the idle loop and consumes the oldest one-shot cue.
	void tick();

	// One auto-battle turn (paced by the battle speed setting); stops the timer when the fight ends.
	void auto_battle_tick();

	// Whether the paced auto-battle timer is running.
	[[nodiscard]] bool auto_battle_timer_running() const { return auto_timer_; }

	void resize(int width, int height);

	// The whole screen as an FTXUI element.
	ftxui::Element render();

private:
	Controller* controller_;
	infrastructure::ArtLibrary* art_;
	bool animate_;
	std::int64_t tick_ = 0;
	std::vector<std::string> cues_;
	bool auto_timer_ = false;
	int width_;
	int height_;

	void take_cues();
	// Paced by the battle speed setting; instant without animation (--no-anim, tests).
	void start_auto_battle();

	ftxui::Element top();
	ftxui::Element player_panel();
	ftxui::Element menu_panel();
};

// Runs the full-screen program until the player quits.
void run_tui(Controller& controller, infrastructure::ArtLibrary& art, bool animate);

} // namespace rpg::presentation::tui
