// Hand-written ANSI renderer of the UI controller (layout specified in docs/tui.md).
//
// Everything here is pure: app_handle_key(), app_tick() and app_auto_battle_tick() change the App, and app_render()
// turns it into one string. The impure terminal loop is run_tui() at the end of app.c, on top of tui/terminal.c, so
// the screen is testable without a terminal (app_render with `ansi` off drops the colours).
#ifndef RPG_PRESENTATION_TUI_APP_H
#define RPG_PRESENTATION_TUI_APP_H

#include "presentation/cli.h"
#include "presentation/controller.h"

#define ANIMATION_MS 500
#define MAX_CUES 16

typedef struct {
	Controller *controller; // borrowed
	// With animation off (--no-anim, tests) only the first art frame is shown and an auto-battle plays instantly.
	bool animate;
	int64_t tick;
	// One-shot monster animations ("hurt", "attack") still to be shown, one per animation tick.
	const char *cues[MAX_CUES];
	size_t cue_count;
	int width;
	int height;
} App;

void app_init(App *app, Controller *controller, bool animate);
// Forwards a key to the controller. Returns false when the player asked to leave.
bool app_handle_key(App *app, const char *key);
// Animation timer (500 ms): advances the idle loop and consumes one one-shot cue.
void app_tick(App *app);
// Auto-battle timer: plays one turn. Returns whether the fight goes on.
bool app_auto_battle_tick(App *app);
// Appends the whole screen to `out`, rows separated by "\n"; with `ansi` the text carries colour escapes.
void app_render(App *app, StrBuf *out, bool ansi);

// Runs the interactive game until the player quits. Returns the process exit code.
int run_tui(const GameData *data, const char *data_dir, const CliOptions *options, bool animate, StrBuf *err);

#endif
