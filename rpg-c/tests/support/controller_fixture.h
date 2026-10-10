// Shared by the controller tests: a controller over real file repositories in a temp directory, and shortcuts for
// the queries the assertions read most.
#ifndef RPG_TESTS_SUPPORT_CONTROLLER_FIXTURE_H
#define RPG_TESTS_SUPPORT_CONTROLLER_FIXTURE_H

#include "presentation/controller.h"
#include "support/helpers.h"

// Seed 7, the fake clock and file repositories in `directory` (from temp_dir_create), like the reference
// make_controller(). `lang` NULL opens the first-launch language screen. `data` and `directory` must outlive the
// controller; release it with controller_free().
void make_controller(Controller *controller, const GameData *data, const char *directory, const char *lang);
// The usual test setup: a new temp directory (written to `directory`) and an English controller over it.
void open_controller(Controller *controller, char directory[TEST_PATH_SIZE], const GameData *data);
// Releases the controller and deletes its directory.
void close_controller(Controller *controller, const char *directory);
// Title → new run → normal difficulty → name → vocation → auto-equip ("2" = off).
void start_run(Controller *controller, const char *name, const char *vocation_key, const char *auto_equip_key);
// The reference defaults: "Zed", the first vocation, auto-equip off.
void start_default_run(Controller *controller);
// Presses every character of `text` as its own key.
void type_text(Controller *controller, const char *text);

// Translation without params (see controller_t for how long it lives).
const char *tr(Controller *controller, const char *key);
size_t body_count(Controller *controller);
// Line `index` of the body; a negative index counts from the end (-1 is the last line). "" when out of range.
const char *body_line(Controller *controller, int index);
// Colour of that line (NULL for the default colour or out of range).
const char *body_color(Controller *controller, int index);
// The whole body joined by "\n". The caller frees it.
char *body_text(Controller *controller);
size_t option_count(Controller *controller);
// Option `index`; a negative index counts from the end.
const MenuOption *option_at(Controller *controller, int index);
// The option keys joined by spaces: "1 2 3 0". The caller frees it.
char *option_keys(Controller *controller);
// The last line of the combat log ("" when it is empty).
const char *last_log_line(const Controller *controller);

#endif
