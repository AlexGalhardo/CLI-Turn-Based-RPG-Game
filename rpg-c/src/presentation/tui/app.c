#include "presentation/tui/app.h"

#include "infrastructure/art.h"
#include "presentation/render.h"
#include "presentation/tui/terminal.h"
#include "version.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_LINES 5
#define TWO_COLUMN_THRESHOLD 4
#define COLUMN_WIDTH 44
#define ART_WIDTH 32
#define TOP_HEIGHT 8
#define PLAYER_HEIGHT 5
#define LOG_HEIGHT 7
#define MIN_MENU_HEIGHT 10
#define ACCENT "#ffa62b"
#define KEY_COLOR "#5fd7ff"

// ── styled text ─────────────────────────────────────────────────────────────
// A screen is a list of lines, a line is a list of spans, a span is a text with one style.

enum { BOLD = 1, ITALIC = 2, UNDERLINE = 4 };

typedef struct {
	unsigned style;
	const char *color; // "#rrggbb" literal, or NULL for the terminal's default
	char *text;        // owned
} Span;

typedef struct {
	Span *spans;
	size_t count;
	size_t capacity;
} Line;

typedef struct {
	Line *lines;
	size_t count;
	size_t capacity;
} Screen;

static void line_add_n(Line *line, unsigned style, const char *color, const char *text, size_t length) {
	Span span = {.style = style, .color = color, .text = xmalloc(length + 1)};
	memcpy(span.text, text, length);
	span.text[length] = '\0';
	VEC_PUSH(line->spans, line->count, line->capacity, span);
}

static void line_add(Line *line, unsigned style, const char *color, const char *text) {
	line_add_n(line, style, color, text, strlen(text));
}

static void line_free(Line *line) {
	for (size_t i = 0; i < line->count; i++) {
		free(line->spans[i].text);
	}
	free(line->spans);
	memset(line, 0, sizeof(*line));
}

static Line *screen_add(Screen *screen) {
	Line empty = {0};
	VEC_PUSH(screen->lines, screen->count, screen->capacity, empty);
	return &screen->lines[screen->count - 1];
}

static void screen_free(Screen *screen) {
	for (size_t i = 0; i < screen->count; i++) {
		line_free(&screen->lines[i]);
	}
	free(screen->lines);
	memset(screen, 0, sizeof(*screen));
}

// Bytes taken by the first `count` code points of `text` (or by all of it when it is shorter).
static size_t utf8_prefix(const char *text, size_t count) {
	size_t bytes = 0;
	while (text[bytes] != '\0') {
		if (((unsigned char)text[bytes] & 0xC0) != 0x80) {
			if (count == 0) {
				break;
			}
			count--;
		}
		bytes++;
	}
	return bytes;
}

static void add_repeated(Line *line, unsigned style, const char *color, const char *piece, size_t times) {
	StrBuf text = {0};
	for (size_t i = 0; i < times; i++) {
		sb_append(&text, piece);
	}
	line_add(line, style, color, text.data == NULL ? "" : text.data);
	sb_free(&text);
}

// Appends `source` to `target`, truncated to `width` visible characters and padded with spaces.
static void fit(Line *target, const Line *source, size_t width) {
	size_t used = 0;
	if (source != NULL) {
		for (size_t i = 0; i < source->count && used < width; i++) {
			const Span *span = &source->spans[i];
			size_t bytes = utf8_prefix(span->text, width - used);
			line_add_n(target, span->style, span->color, span->text, bytes);
			used += utf8_length(target->spans[target->count - 1].text);
		}
	}
	add_repeated(target, 0, NULL, " ", width - used);
}

// A bordered box of `height` rows: `rows` are truncated or padded to fill it. `title_row` (optional) replaces the
// top border.
static void panel(
    Screen *screen, size_t width, size_t height, const Line *rows, size_t row_count, const char *title_row) {
	Line *top = screen_add(screen);
	if (title_row != NULL) {
		line_add(top, 0, ACCENT, title_row);
	} else {
		line_add(top, 0, ACCENT, "╭");
		add_repeated(top, 0, ACCENT, "─", width - 2);
		line_add(top, 0, ACCENT, "╮");
	}
	for (size_t i = 0; i < height - 2; i++) {
		Line *row = screen_add(screen);
		line_add(row, 0, ACCENT, "│ ");
		fit(row, i < row_count ? &rows[i] : NULL, width - 4);
		line_add(row, 0, ACCENT, " │");
	}
	Line *bottom = screen_add(screen);
	line_add(bottom, 0, ACCENT, "╰");
	add_repeated(bottom, 0, ACCENT, "─", width - 2);
	line_add(bottom, 0, ACCENT, "╯");
}

// ── colours ─────────────────────────────────────────────────────────────────

// The controller and render.h speak in colour names (elements, rarities, semantic styles, plain colours); this is
// the one place that knows their RGB values.
static const char *color_hex(const char *name) {
	static const char *const table[][2] = {
	    {"physical", "#ffffff"},
	    {"fire", "#ff5f5f"},
	    {"ice", "#5fd7ff"},
	    {"energy", "#d75fff"},
	    {"earth", "#5fd75f"},
	    {"holy", "#ffd75f"},
	    {"death", "#8a8a8a"},
	    {"common", "#ffffff"},
	    {"rare", "#1e90ff"},
	    {"legendary", "#ffaf00"},
	    {"mythic", "#af87ff"},
	    {STYLE_WARNING, "#ffd75f"},
	    {STYLE_GAIN, "#5fd75f"},
	    {STYLE_LOSS, "#ff5f5f"},
	    {STYLE_DIM, "#808080"},
	    {"green", "#5fd75f"},
	    {"yellow", "#ffd75f"},
	    {"red", "#ff5f5f"},
	    {"blue", "#5f87ff"},
	};
	if (name == NULL) {
		return NULL;
	}
	for (size_t i = 0; i < ARRAY_LEN(table); i++) {
		if (strcmp(table[i][0], name) == 0) {
			return table[i][1];
		}
	}
	return NULL;
}

// ── update ──────────────────────────────────────────────────────────────────

void app_init(App *app, Controller *controller, bool animate) {
	memset(app, 0, sizeof(*app));
	app->controller = controller;
	app->animate = animate;
	app->width = MIN_COLUMNS;
	app->height = MIN_ROWS;
}

static void take_cues(App *app) {
	Controller *controller = app->controller;
	app->cue_count = 0;
	if (app->animate) {
		for (size_t i = 0; i < controller->animation_cue_count && app->cue_count < MAX_CUES; i++) {
			app->cues[app->cue_count++] = controller->animation_cues[i];
		}
	}
	controller->animation_cue_count = 0;
}

bool app_handle_key(App *app, const char *key) {
	Controller *controller = app->controller;
	if (strcmp(key, "ctrl+c") == 0) {
		return false;
	}
	controller_press(controller, key);
	if (controller->exit_requested) {
		return false;
	}
	// Without animation an auto-battle plays instantly; otherwise the terminal loop paces it.
	if (controller->auto_battle_active && !app->animate) {
		controller_run_auto_battle(controller);
	}
	take_cues(app);
	return true;
}

void app_tick(App *app) {
	app->tick++;
	if (app->cue_count > 0) {
		memmove(&app->cues[0], &app->cues[1], (app->cue_count - 1) * sizeof(app->cues[0]));
		app->cue_count--;
	}
}

bool app_auto_battle_tick(App *app) {
	bool running = controller_auto_battle_step(app->controller);
	take_cues(app);
	return running;
}

// ── rendering ───────────────────────────────────────────────────────────────

static void add_bar(Line *line, const char *color, int64_t current, int64_t maximum) {
	StrBuf cells = {0};
	bar(&cells, current, maximum, BAR_WIDTH);
	line_add(line, 0, color, cells.data);
	sb_free(&cells);
}

RPG_PRINTF(4, 5)
static void add_formatted(Line *line, unsigned style, const char *color, const char *format, ...) {
	char text[256];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	line_add(line, style, color, text);
}

// The top box: monster art on the left, monster (or game title) information on the right.
static void render_top(App *app, Screen *screen) {
	Controller *controller = app->controller;
	Line info[TOP_HEIGHT - 2] = {0};
	const Frame *frame;
	unsigned art_style = 0;
	const char *art_color;

	const MonsterView *monster = controller_monster_view(controller);
	if (monster == NULL) {
		frame = frame_for(art_load_file("families", "dragon"), "idle", app->tick);
		art_style = BOLD;
		art_color = "#5fd75f";
		line_add(&info[0], BOLD, NULL, controller_t(controller, "app.title", NULL, 0));
		line_add(&info[1], ITALIC, NULL, controller_t(controller, "app.subtitle", NULL, 0));
		line_add(&info[3], 0, NULL, "v" RPG_VERSION " · C");
	} else {
		const MonsterDef *creature = data_creature(controller->services.data, monster->creature_id);
		const char *animation = app->cue_count > 0 ? app->cues[0] : "idle";
		frame = frame_for(art_for_creature(creature), animation, app->tick);
		const char *element_hex = color_hex(monster->element);
		element_hex = element_hex != NULL ? element_hex : "#ffffff";
		bool hurt = strcmp(animation, "hurt") == 0;
		art_style = hurt ? BOLD : 0;
		art_color = hurt ? "#ff5f5f" : element_hex;

		if (monster->is_boss) {
			line_add(&info[0], BOLD, "#d75fff", controller_t(controller, "hud.boss", NULL, 0));
			line_add(&info[0], 0, NULL, " ");
		} else if (strcmp(monster->enemy_class, "elite") == 0) {
			line_add(&info[0], BOLD, "#ffd75f", controller_t(controller, "hud.elite", NULL, 0));
			line_add(&info[0], 0, NULL, " ");
		}
		char name[NAME_SIZE];
		str_copy(name, sizeof(name), monster->name);
		for (char *c = name; *c != '\0'; c++) {
			*c = (char)toupper((unsigned char)*c);
		}
		line_add(&info[0], BOLD, NULL, name);
		line_add(&info[1], BOLD, NULL, "HP ");
		add_bar(&info[1], color_hex(hp_color(monster->hp, monster->max_hp)), monster->hp, monster->max_hp);
		add_formatted(&info[1], 0, NULL, "  %lld/%lld", (long long)monster->hp, (long long)monster->max_hp);
		line_add(&info[2], 0, element_hex, monster->details);
	}

	Line rows[TOP_HEIGHT - 2] = {0};
	for (size_t i = 0; i < TOP_HEIGHT - 2; i++) {
		Line art = {0};
		if (frame != NULL && i < frame->line_count) {
			line_add(&art, art_style, art_color, frame->lines[i]);
		}
		fit(&rows[i], &art, ART_WIDTH);
		for (size_t s = 0; s < info[i].count; s++) {
			line_add(&rows[i], info[i].spans[s].style, info[i].spans[s].color, info[i].spans[s].text);
		}
		line_free(&art);
		line_free(&info[i]);
	}

	// The header sits inside the top border: "╭─ Round 7 · Tier 1 ───╮".
	const char *header = controller_header(controller);
	StrBuf title = {0};
	sb_appendf(&title, "╭─ %s ", header);
	for (int i = (int)utf8_length(header) + 5; i < app->width; i++) {
		sb_append(&title, "─");
	}
	sb_append(&title, "╮");
	panel(screen, (size_t)app->width, TOP_HEIGHT, rows, TOP_HEIGHT - 2, title.data);
	sb_free(&title);
	for (size_t i = 0; i < TOP_HEIGHT - 2; i++) {
		line_free(&rows[i]);
	}
}

static void render_player(App *app, Screen *screen) {
	Line rows[3] = {0};
	const PlayerView *player = controller_player_view(app->controller);
	if (player != NULL) {
		line_add(&rows[0], 0, NULL, player->summary);
		add_formatted(&rows[0], 0, "#ffd75f", "   %s", player->gold);
		if (player->statuses[0] != '\0') {
			add_formatted(&rows[0], 0, "#ff5f5f", "   %s", player->statuses);
		}
		line_add(&rows[1], BOLD, NULL, "HP ");
		add_bar(&rows[1], color_hex(hp_color(player->hp, player->max_hp)), player->hp, player->max_hp);
		add_formatted(&rows[1], BOLD, NULL, "  %lld/%lld", (long long)player->hp, (long long)player->max_hp);
		line_add(&rows[2], BOLD, NULL, "MP ");
		add_bar(&rows[2], color_hex("blue"), player->mp, player->max_mp);
		add_formatted(
		    &rows[2], 0, NULL, "  %lld/%lld   %s", (long long)player->mp, (long long)player->max_mp, player->xp);
	}
	panel(screen, (size_t)app->width, PLAYER_HEIGHT, rows, player != NULL ? 3 : 0, NULL);
	for (size_t i = 0; i < 3; i++) {
		line_free(&rows[i]);
	}
}

static void render_log(App *app, Screen *screen) {
	const Controller *controller = app->controller;
	Line rows[LOG_LINES] = {0};
	size_t shown = controller->log_count < LOG_LINES ? controller->log_count : LOG_LINES;
	for (size_t i = 0; i < shown; i++) {
		line_add(&rows[i], 0, NULL, controller->log[controller->log_count - shown + i]);
	}
	panel(screen, (size_t)app->width, LOG_HEIGHT, rows, shown, NULL);
	for (size_t i = 0; i < shown; i++) {
		line_free(&rows[i]);
	}
}

// One option: "[1] label  detail". In two columns the label is cut and the cell padded to COLUMN_WIDTH.
static void add_option(Line *row, const MenuOption *option, bool two_columns, bool last_in_row) {
	char key[8];
	snprintf(key, sizeof(key), "[%c] ", toupper((unsigned char)option->key[0]));
	line_add(row, BOLD, KEY_COLOR, key);
	StrBuf detail = {0};
	if (option->detail[0] != '\0') {
		sb_appendf(&detail, "  %s", option->detail);
	}
	const char *detail_text = detail.data != NULL ? detail.data : "";
	size_t detail_width = utf8_length(detail_text);
	size_t label_bytes = strlen(option->label);
	if (two_columns) {
		size_t room = COLUMN_WIDTH - 1 > detail_width ? COLUMN_WIDTH - 1 - detail_width : 0;
		label_bytes = utf8_prefix(option->label, room);
	}
	line_add_n(row, 0, color_hex(option->color), option->label, label_bytes);
	size_t label_width = utf8_length(row->spans[row->count - 1].text);
	line_add(row, 0, color_hex(option->detail_color), detail_text);
	if (two_columns && !last_in_row && label_width + detail_width < COLUMN_WIDTH) {
		add_repeated(row, 0, NULL, " ", COLUMN_WIDTH - label_width - detail_width);
	}
	sb_free(&detail);
}

// The bottom box: title, body lines, options, then the input prompt and the message of the last key press.
static void render_menu(App *app, Screen *screen, size_t height) {
	Controller *controller = app->controller;
	size_t room = height - 2;
	size_t option_count;
	const MenuOption *options = controller_options(controller, &option_count);
	size_t body_count;
	size_t color_count;
	const char *const *body = controller_body_lines(controller, &body_count);
	const char *const *colors = controller_body_colors(controller, &color_count);
	const char *prompt = controller_input_prompt(controller);
	bool has_message = controller->message != NULL && controller->message[0] != '\0';

	size_t columns = option_count > TWO_COLUMN_THRESHOLD ? 2 : 1;
	size_t option_rows = (option_count + columns - 1) / columns;
	size_t tail = (option_count > 0 ? 1 + option_rows : 0) + (prompt != NULL ? 2 : 0) + (has_message ? 2 : 0);

	Line *rows = xcalloc(1 + body_count + tail + 1, sizeof(Line));
	size_t count = 0;
	line_add(&rows[count++], BOLD | UNDERLINE, NULL, controller_title(controller));

	// Long bodies (the equipment screen on a small terminal) are cut so the options always stay visible.
	size_t body_room = room > 1 + tail ? room - 1 - tail : 0;
	size_t shown = body_count;
	if (body_count > body_room) {
		shown = body_room > 0 ? body_room - 1 : 0;
	}
	for (size_t i = 0; i < shown; i++) {
		line_add(&rows[count++], 0, i < color_count ? color_hex(colors[i]) : NULL, body[i]);
	}
	if (shown < body_count) {
		line_add(&rows[count++], 0, NULL, "…");
	}

	if (option_count > 0) {
		count++; // blank line
		for (size_t i = 0; i < option_count; i++) {
			bool last_in_row = columns == 1 || i % columns == columns - 1 || i == option_count - 1;
			add_option(&rows[count], &options[i], columns == 2, last_in_row);
			if (last_in_row) {
				count++;
			}
		}
	}
	if (prompt != NULL) {
		count++;
		line_add(&rows[count++], BOLD, NULL, prompt);
	}
	if (has_message) {
		count++;
		line_add(&rows[count++], BOLD, "#ff5f5f", controller->message);
	}
	panel(screen, (size_t)app->width, height, rows, count, NULL);
	for (size_t i = 0; i < count; i++) {
		line_free(&rows[i]);
	}
	free(rows);
}

static void build_screen(App *app, Screen *screen) {
	Controller *controller = app->controller;
	if (app->width < MIN_COLUMNS || app->height < MIN_ROWS) {
		const char *message =
		    CONTROLLER_T(controller, "app.resize", P_INT("columns", MIN_COLUMNS), P_INT("rows", MIN_ROWS));
		line_add(screen_add(screen), BOLD, "#ffd75f", message);
		return;
	}
	int used = TOP_HEIGHT + PLAYER_HEIGHT;
	render_top(app, screen);
	render_player(app, screen);
	if (!view_is_paged(controller->view)) {
		render_log(app, screen);
		used += LOG_HEIGHT;
	}
	int menu_height = app->height - used > MIN_MENU_HEIGHT ? app->height - used : MIN_MENU_HEIGHT;
	render_menu(app, screen, (size_t)menu_height);
}

static void append_span(StrBuf *out, const Span *span, bool ansi) {
	bool styled = ansi && span->text[0] != '\0' && (span->style != 0 || span->color != NULL);
	if (styled) {
		// SGR: 1 bold, 3 italic, 4 underline, 38;2;r;g;b a 24-bit foreground colour.
		sb_append(out, "\x1b[0");
		if (span->style & BOLD) {
			sb_append(out, ";1");
		}
		if (span->style & ITALIC) {
			sb_append(out, ";3");
		}
		if (span->style & UNDERLINE) {
			sb_append(out, ";4");
		}
		if (span->color != NULL) {
			unsigned red = 0;
			unsigned green = 0;
			unsigned blue = 0;
			sscanf(span->color, "#%2x%2x%2x", &red, &green, &blue);
			sb_appendf(out, ";38;2;%u;%u;%u", red, green, blue);
		}
		sb_append(out, "m");
	}
	sb_append(out, span->text);
	if (styled) {
		sb_append(out, "\x1b[0m");
	}
}

void app_render(App *app, StrBuf *out, bool ansi) {
	Screen screen = {0};
	build_screen(app, &screen);
	for (size_t i = 0; i < screen.count; i++) {
		if (i > 0) {
			sb_append_char(out, '\n');
		}
		for (size_t s = 0; s < screen.lines[i].count; s++) {
			append_span(out, &screen.lines[i].spans[s], ansi);
		}
	}
	screen_free(&screen);
}

// ── the terminal loop (the only impure part) ────────────────────────────────

static void draw(App *app) {
	terminal_size(&app->width, &app->height);
	StrBuf frame = {0};
	app_render(app, &frame, true);
	// Home the cursor, clear to the end of every row while writing, then clear whatever is below the frame.
	StrBuf output = {0};
	sb_append(&output, "\x1b[H");
	for (const char *c = frame.data; *c != '\0'; c++) {
		if (*c == '\n') {
			sb_append(&output, "\x1b[K\r\n");
		} else {
			sb_append_char(&output, *c);
		}
	}
	sb_append(&output, "\x1b[K\x1b[J");
	terminal_write(output.data, output.length);
	sb_free(&frame);
	sb_free(&output);
}

// One thread, two timers: wait for a key until the next timer is due, then run whatever is due and redraw.
static void interactive_loop(App *app) {
	int64_t next_animation = terminal_now_ms() + ANIMATION_MS;
	int64_t next_auto_battle = -1; // -1 = no auto-battle running
	draw(app);
	for (;;) {
		int64_t now = terminal_now_ms();
		int64_t wake = app->animate ? next_animation : -1;
		if (next_auto_battle >= 0 && (wake < 0 || next_auto_battle < wake)) {
			wake = next_auto_battle;
		}
		int timeout = wake < 0 ? -1 : (int)(wake > now ? wake - now : 0);

		char key[KEY_SIZE];
		KeyResult result = terminal_read_key(timeout, key);
		if (result == KEY_END || (result == KEY_READ && !app_handle_key(app, key))) {
			return;
		}
		now = terminal_now_ms();
		if (app->controller->auto_battle_active && next_auto_battle < 0) {
			next_auto_battle = now + controller_auto_battle_interval_ms(app->controller);
		}
		if (app->animate && now >= next_animation) {
			app_tick(app);
			next_animation = now + ANIMATION_MS;
		}
		if (next_auto_battle >= 0 && now >= next_auto_battle) {
			bool running = app_auto_battle_tick(app);
			next_auto_battle = running ? now + controller_auto_battle_interval_ms(app->controller) : -1;
		}
		draw(app);
	}
}

// Without an interactive terminal (a pipe, a file): print the screen as plain text and read whole lines, each one a
// sequence of keys followed by Enter.
static void line_loop(App *app) {
	app->animate = false;
	char line[256];
	for (;;) {
		StrBuf frame = {0};
		app_render(app, &frame, false);
		sb_append_char(&frame, '\n');
		fputs(frame.data, stdout);
		fflush(stdout);
		sb_free(&frame);
		if (fgets(line, sizeof(line), stdin) == NULL) {
			return;
		}
		size_t length = strcspn(line, "\r\n");
		line[length] = '\0';
		for (size_t at = 0; at < length;) {
			size_t bytes = utf8_prefix(line + at, 1);
			char key[KEY_SIZE] = {0};
			memcpy(key, line + at, bytes < KEY_SIZE ? bytes : KEY_SIZE - 1);
			at += bytes;
			if (!app_handle_key(app, key)) {
				return;
			}
		}
		if (!app_handle_key(app, "enter")) {
			return;
		}
	}
}

int run_tui(const GameData *data, const char *data_dir, const CliOptions *options, bool animate, StrBuf *err) {
	(void)err;
	Services services = {
	    .data = data,
	    .data_dir = data_dir,
	    .repositories = file_repositories(data_dir),
	    .clock = system_clock(),
	    .version = RPG_VERSION,
	    .has_seed = options->has_seed,
	    .seed = options->seed,
	    .locale_override = options->lang,
	};
	Controller controller;
	controller_init(&controller, &services);
	App app;
	app_init(&app, &controller, animate);
	if (terminal_start()) {
		interactive_loop(&app);
		terminal_stop();
	} else {
		line_loop(&app);
	}
	controller_save_session(&controller);
	controller_free(&controller);
	return 0;
}
