// The `rpg_host` Lua module: what standard Lua cannot do, and nothing else.
//
// The whole game is written in Lua. Standard Lua, however, reads the keyboard one line at a time and cannot create
// or list a directory, so this file gives the game three small groups of functions:
//   - asset(name): a file of shared/ compiled into the binary;
//   - is_dir, mkdir, list_dir, replace: the directory calls of infrastructure/filesystem.lua;
//   - terminal_*: raw keyboard input, terminal size and output for presentation/tui/terminal.lua.
// This is the only platform-specific code of the port: termios + poll on POSIX, the console API on Windows.
#include "rpg_host.h"

#include "embedded.h"
#include "lauxlib.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define KEY_SIZE 16
#define ENTER_SCREEN "\x1b[?1049h\x1b[?25l"
#define LEAVE_SCREEN "\x1b[0m\x1b[?25h\x1b[?1049l"

typedef enum { READ_KEY, READ_TIMEOUT, READ_END } KeyResult;

static bool started;

static void set_key(char key[KEY_SIZE], const char *name) {
	strncpy(key, name, KEY_SIZE - 1);
	key[KEY_SIZE - 1] = '\0';
}

#ifdef _WIN32
// ── Windows: console API ────────────────────────────────────────────────────
#include <windows.h>

static HANDLE input;
static HANDLE output;
static DWORD saved_input_mode;
static DWORD saved_output_mode;
static UINT saved_code_page;

static bool path_is_dir(const char *path) {
	DWORD attributes = GetFileAttributesA(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool make_dir(const char *path) { return CreateDirectoryA(path, NULL) != 0 || path_is_dir(path); }

static bool replace_file(const char *source, const char *target) {
	return MoveFileExA(source, target, MOVEFILE_REPLACE_EXISTING) != 0;
}

// Pushes a list with the names inside `path` (empty when it is not a directory).
static void push_dir_names(lua_State *L, const char *path) {
	lua_newtable(L);
	lua_pushfstring(L, "%s\\*", path);
	WIN32_FIND_DATAA entry;
	HANDLE search = FindFirstFileA(lua_tostring(L, -1), &entry);
	lua_pop(L, 1);
	if (search == INVALID_HANDLE_VALUE) {
		return;
	}
	lua_Integer count = 0;
	do {
		if (strcmp(entry.cFileName, ".") != 0 && strcmp(entry.cFileName, "..") != 0) {
			lua_pushstring(L, entry.cFileName);
			lua_rawseti(L, -2, ++count);
		}
	} while (FindNextFileA(search, &entry));
	FindClose(search);
}

static void terminal_write(const char *text, size_t length) {
	DWORD written;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, (DWORD)length, &written, NULL);
}

static void terminal_stop(void) {
	if (!started) {
		return;
	}
	started = false;
	terminal_write(LEAVE_SCREEN, strlen(LEAVE_SCREEN));
	SetConsoleOutputCP(saved_code_page);
	SetConsoleMode(output, saved_output_mode);
	SetConsoleMode(input, saved_input_mode);
}

static bool terminal_start(void) {
	input = GetStdHandle(STD_INPUT_HANDLE);
	output = GetStdHandle(STD_OUTPUT_HANDLE);
	// Git Bash's own window (mintty) gives pipes, not a console: GetConsoleMode fails there.
	if (!GetConsoleMode(input, &saved_input_mode) || !GetConsoleMode(output, &saved_output_mode)) {
		return false;
	}
	// No line buffering, no echo, Ctrl+C delivered as a key; window events tell us about resizes.
	SetConsoleMode(input, ENABLE_WINDOW_INPUT);
	// Let the console interpret the ANSI escape sequences the renderer writes.
	SetConsoleMode(output, saved_output_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	saved_code_page = GetConsoleOutputCP();
	SetConsoleOutputCP(CP_UTF8);
	started = true;
	atexit(terminal_stop);
	terminal_write(ENTER_SCREEN, strlen(ENTER_SCREEN));
	return true;
}

static void terminal_size(int *columns, int *rows) {
	CONSOLE_SCREEN_BUFFER_INFO info;
	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
		*columns = info.srWindow.Right - info.srWindow.Left + 1;
		*rows = info.srWindow.Bottom - info.srWindow.Top + 1;
	} else {
		*columns = 100;
		*rows = 30;
	}
}

static int64_t terminal_now_ms(void) { return (int64_t)GetTickCount64(); }

// Returns false for events that are not a key the game understands.
static bool decode_key(const KEY_EVENT_RECORD *event, char key[KEY_SIZE]) {
	static const struct {
		WORD code;
		const char *name;
	} named[] = {
	    {VK_RETURN, "enter"},
	    {VK_ESCAPE, "escape"},
	    {VK_BACK, "backspace"},
	    {VK_UP, "up"},
	    {VK_DOWN, "down"},
	    {VK_LEFT, "left"},
	    {VK_RIGHT, "right"},
	};
	for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
		if (event->wVirtualKeyCode == named[i].code) {
			set_key(key, named[i].name);
			return true;
		}
	}
	WCHAR character = event->uChar.UnicodeChar;
	if (character == 3) {
		set_key(key, "ctrl+c");
		return true;
	}
	if (character < 0x20) {
		return false;
	}
	int length = WideCharToMultiByte(CP_UTF8, 0, &character, 1, key, KEY_SIZE - 1, NULL, NULL);
	if (length <= 0) {
		return false;
	}
	key[length] = '\0';
	return true;
}

static KeyResult terminal_read_key(int timeout_ms, char key[KEY_SIZE]) {
	int64_t deadline = terminal_now_ms() + timeout_ms;
	for (;;) {
		DWORD wait = INFINITE;
		if (timeout_ms >= 0) {
			int64_t remaining = deadline - terminal_now_ms();
			wait = remaining > 0 ? (DWORD)remaining : 0;
		}
		if (WaitForSingleObject(input, wait) != WAIT_OBJECT_0) {
			return READ_TIMEOUT;
		}
		INPUT_RECORD record;
		DWORD count;
		if (!ReadConsoleInputW(input, &record, 1, &count) || count == 0) {
			return READ_END;
		}
		if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
			// Not a key, but the caller redraws after every return: report it as a timeout.
			return READ_TIMEOUT;
		}
		if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown &&
		    decode_key(&record.Event.KeyEvent, key)) {
			return READ_KEY;
		}
	}
}

#else
// ── POSIX: termios + poll ───────────────────────────────────────────────────
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// How long a lone ESC waits for the rest of an escape sequence before it counts as the Escape key.
#define ESCAPE_TIMEOUT_MS 30

static struct termios saved_mode;

static bool path_is_dir(const char *path) {
	struct stat info;
	return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool make_dir(const char *path) { return mkdir(path, 0777) == 0 || path_is_dir(path); }

static bool replace_file(const char *source, const char *target) { return rename(source, target) == 0; }

// Pushes a list with the names inside `path` (empty when it is not a directory).
static void push_dir_names(lua_State *L, const char *path) {
	lua_newtable(L);
	DIR *directory = opendir(path);
	if (directory == NULL) {
		return;
	}
	lua_Integer count = 0;
	for (struct dirent *entry = readdir(directory); entry != NULL; entry = readdir(directory)) {
		if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
			lua_pushstring(L, entry->d_name);
			lua_rawseti(L, -2, ++count);
		}
	}
	closedir(directory);
}

static void terminal_write(const char *text, size_t length) {
	while (length > 0) {
		ssize_t written = write(STDOUT_FILENO, text, length);
		if (written <= 0) {
			if (written < 0 && errno == EINTR) {
				continue;
			}
			return;
		}
		text += written;
		length -= (size_t)written;
	}
}

static void on_resize(int signal_number) { (void)signal_number; }

static void terminal_stop(void) {
	if (!started) {
		return;
	}
	started = false;
	terminal_write(LEAVE_SCREEN, strlen(LEAVE_SCREEN));
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_mode);
}

static bool terminal_start(void) {
	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || tcgetattr(STDIN_FILENO, &saved_mode) != 0) {
		return false;
	}
	struct termios raw = saved_mode;
	// No line buffering, no echo, Ctrl+C delivered as a byte, Enter delivered as \r.
	raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
	raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	// An empty handler is enough: the signal interrupts poll(), and the caller redraws at the new size.
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_handler = on_resize;
	sigaction(SIGWINCH, &action, NULL);
	started = true;
	atexit(terminal_stop);
	terminal_write(ENTER_SCREEN, strlen(ENTER_SCREEN));
	return true;
}

static void terminal_size(int *columns, int *rows) {
	struct winsize size;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0) {
		*columns = size.ws_col;
		*rows = size.ws_row;
	} else {
		*columns = 100;
		*rows = 30;
	}
}

static int64_t terminal_now_ms(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

// Reads one byte, waiting up to `timeout_ms` (negative = forever). 1 = read, 0 = timeout or interrupted, -1 = end.
static int read_byte(int timeout_ms, unsigned char *byte) {
	struct pollfd descriptor = {.fd = STDIN_FILENO, .events = POLLIN};
	int ready = poll(&descriptor, 1, timeout_ms);
	if (ready <= 0) {
		return ready < 0 && errno != EINTR ? -1 : 0;
	}
	return read(STDIN_FILENO, byte, 1) == 1 ? 1 : -1;
}

// After ESC: consumes a CSI/SS3 sequence ("[A", "OB", "[1;5C"…) and names the arrow keys.
static bool read_escape_sequence(char key[KEY_SIZE]) {
	unsigned char byte;
	if (read_byte(ESCAPE_TIMEOUT_MS, &byte) != 1) {
		set_key(key, "escape");
		return true;
	}
	if (byte != '[' && byte != 'O') {
		return false;
	}
	while (read_byte(ESCAPE_TIMEOUT_MS, &byte) == 1) {
		if (byte < 0x40 || byte > 0x7E) {
			continue; // parameter bytes
		}
		static const char *const arrows[] = {"up", "down", "right", "left"};
		if (byte >= 'A' && byte <= 'D') {
			set_key(key, arrows[byte - 'A']);
			return true;
		}
		return false;
	}
	return false;
}

static KeyResult terminal_read_key(int timeout_ms, char key[KEY_SIZE]) {
	int64_t deadline = terminal_now_ms() + timeout_ms;
	for (;;) {
		int wait = -1;
		if (timeout_ms >= 0) {
			int64_t remaining = deadline - terminal_now_ms();
			wait = remaining > 0 ? (int)remaining : 0;
		}
		unsigned char byte;
		int status = read_byte(wait, &byte);
		if (status < 0) {
			return READ_END;
		}
		if (status == 0) {
			return READ_TIMEOUT;
		}
		if (byte == 0x1b) {
			if (read_escape_sequence(key)) {
				return READ_KEY;
			}
		} else if (byte == '\r' || byte == '\n') {
			set_key(key, "enter");
			return READ_KEY;
		} else if (byte == 0x7f || byte == 0x08) {
			set_key(key, "backspace");
			return READ_KEY;
		} else if (byte == 0x03) {
			set_key(key, "ctrl+c");
			return READ_KEY;
		} else if (byte >= 0x20) {
			// A UTF-8 lead byte says how many continuation bytes follow.
			int extra = byte >= 0xF0 ? 3 : byte >= 0xE0 ? 2 : byte >= 0xC0 ? 1 : 0;
			key[0] = (char)byte;
			int length = 1;
			while (extra-- > 0 && read_byte(ESCAPE_TIMEOUT_MS, &byte) == 1) {
				key[length++] = (char)byte;
			}
			key[length] = '\0';
			return READ_KEY;
		}
	}
}
#endif

// ── the Lua functions ───────────────────────────────────────────────────────
// Each one takes its arguments from the Lua stack and returns how many results it pushed.

// asset(name) -> string, or nil when shared/ has no such file.
static int host_asset(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	for (size_t i = 0; i < EMBEDDED_ASSET_COUNT; i++) {
		if (strcmp(EMBEDDED_ASSETS[i].name, name) == 0) {
			lua_pushlstring(L, EMBEDDED_ASSETS[i].content, EMBEDDED_ASSETS[i].size);
			return 1;
		}
	}
	lua_pushnil(L);
	return 1;
}

static int host_is_dir(lua_State *L) {
	lua_pushboolean(L, path_is_dir(luaL_checkstring(L, 1)));
	return 1;
}

// mkdir(path) -> true when the directory exists afterwards (the parent must exist).
static int host_mkdir(lua_State *L) {
	lua_pushboolean(L, make_dir(luaL_checkstring(L, 1)));
	return 1;
}

static int host_list_dir(lua_State *L) {
	push_dir_names(L, luaL_checkstring(L, 1));
	return 1;
}

// replace(source, target) -> true when `source` was moved over `target`.
static int host_replace(lua_State *L) {
	lua_pushboolean(L, replace_file(luaL_checkstring(L, 1), luaL_checkstring(L, 2)));
	return 1;
}

static int host_terminal_start(lua_State *L) {
	lua_pushboolean(L, terminal_start());
	return 1;
}

static int host_terminal_stop(lua_State *L) {
	(void)L;
	terminal_stop();
	return 0;
}

// terminal_size() -> columns, rows
static int host_terminal_size(lua_State *L) {
	int columns;
	int rows;
	terminal_size(&columns, &rows);
	lua_pushinteger(L, columns);
	lua_pushinteger(L, rows);
	return 2;
}

// terminal_read_key(timeout_ms) -> the key, nil on a timeout, false when the input ended.
static int host_terminal_read_key(lua_State *L) {
	char key[KEY_SIZE];
	switch (terminal_read_key((int)luaL_checkinteger(L, 1), key)) {
	case READ_KEY:
		lua_pushstring(L, key);
		break;
	case READ_TIMEOUT:
		lua_pushnil(L);
		break;
	default:
		lua_pushboolean(L, 0);
		break;
	}
	return 1;
}

static int host_terminal_write(lua_State *L) {
	size_t length;
	const char *text = luaL_checklstring(L, 1, &length);
	terminal_write(text, length);
	return 0;
}

static int host_terminal_now_ms(lua_State *L) {
	lua_pushinteger(L, (lua_Integer)terminal_now_ms());
	return 1;
}

int luaopen_rpg_host(lua_State *L) {
	static const luaL_Reg functions[] = {
	    {"is_dir", host_is_dir},
	    {"mkdir", host_mkdir},
	    {"list_dir", host_list_dir},
	    {"replace", host_replace},
	    {"terminal_start", host_terminal_start},
	    {"terminal_stop", host_terminal_stop},
	    {"terminal_size", host_terminal_size},
	    {"terminal_read_key", host_terminal_read_key},
	    {"terminal_write", host_terminal_write},
	    {"terminal_now_ms", host_terminal_now_ms},
	    {NULL, NULL},
	};
	luaL_newlib(L, functions);
	// `asset` only exists when shared/ was compiled in: the game then never looks for the folder on disk.
	if (EMBEDDED_ASSET_COUNT > 0) {
		lua_pushcfunction(L, host_asset);
		lua_setfield(L, -2, "asset");
	}
	return 1;
}
