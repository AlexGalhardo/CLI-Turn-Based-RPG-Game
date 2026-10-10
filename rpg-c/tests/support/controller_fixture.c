#include "support/controller_fixture.h"

#include "infrastructure/repositories.h"
#include "version.h"

static FakeClock clock_state;

void make_controller(Controller *controller, const GameData *data, const char *directory, const char *lang) {
	Services services = {
	    .data = data,
	    .data_dir = directory,
	    .repositories = file_repositories(directory),
	    .clock = fake_clock(&clock_state),
	    .version = RPG_VERSION,
	    .has_seed = true,
	    .seed = 7,
	    .locale_override = lang,
	};
	controller_init(controller, &services);
}

void open_controller(Controller *controller, char directory[TEST_PATH_SIZE], const GameData *data) {
	temp_dir_create(directory);
	make_controller(controller, data, directory, "en");
}

void close_controller(Controller *controller, const char *directory) {
	controller_free(controller);
	temp_dir_remove(directory);
}

void type_text(Controller *controller, const char *text) {
	for (const char *at = text; *at != '\0'; at++) {
		char key[2] = {*at, '\0'};
		controller_press(controller, key);
	}
}

void start_run(Controller *controller, const char *name, const char *vocation_key, const char *auto_equip_key) {
	controller_press(controller, "2");
	controller_press(controller, "2");
	type_text(controller, name);
	controller_press(controller, "enter");
	controller_press(controller, vocation_key);
	controller_press(controller, auto_equip_key);
}

void start_default_run(Controller *controller) { start_run(controller, "Zed", "1", "2"); }

const char *tr(Controller *controller, const char *key) { return controller_t(controller, key, NULL, 0); }

size_t body_count(Controller *controller) {
	size_t count;
	controller_body_lines(controller, &count);
	return count;
}

// Turns a Python-style index into a position, or returns false when it is out of range.
static bool resolve(int index, size_t count, size_t *position) {
	int resolved = index < 0 ? (int)count + index : index;
	if (resolved < 0 || (size_t)resolved >= count) {
		return false;
	}
	*position = (size_t)resolved;
	return true;
}

const char *body_line(Controller *controller, int index) {
	size_t count;
	size_t position;
	const char *const *lines = controller_body_lines(controller, &count);
	return resolve(index, count, &position) ? lines[position] : "";
}

const char *body_color(Controller *controller, int index) {
	size_t count;
	size_t position;
	const char *const *colors = controller_body_colors(controller, &count);
	return resolve(index, count, &position) ? colors[position] : NULL;
}

char *body_text(Controller *controller) {
	size_t count;
	const char *const *lines = controller_body_lines(controller, &count);
	StrBuf text = {0};
	for (size_t i = 0; i < count; i++) {
		sb_append(&text, i > 0 ? "\n" : "");
		sb_append(&text, lines[i]);
	}
	sb_append(&text, "");
	return sb_take(&text);
}

size_t option_count(Controller *controller) {
	size_t count;
	controller_options(controller, &count);
	return count;
}

const MenuOption *option_at(Controller *controller, int index) {
	static const MenuOption missing = {.key = "", .label = "", .detail = ""};
	size_t count;
	size_t position;
	const MenuOption *options = controller_options(controller, &count);
	return resolve(index, count, &position) ? &options[position] : &missing;
}

char *option_keys(Controller *controller) {
	size_t count;
	const MenuOption *options = controller_options(controller, &count);
	StrBuf keys = {0};
	for (size_t i = 0; i < count; i++) {
		sb_append(&keys, i > 0 ? " " : "");
		sb_append(&keys, options[i].key);
	}
	sb_append(&keys, "");
	return sb_take(&keys);
}

const char *last_log_line(const Controller *controller) {
	return controller->log_count == 0 ? "" : controller->log[controller->log_count - 1];
}
