#include "presentation/tui/terminal.h"

#include <stdlib.h>
#include <string.h>

#define ENTER_SCREEN "\x1b[?1049h\x1b[?25l"
#define LEAVE_SCREEN "\x1b[0m\x1b[?25h\x1b[?1049l"

static bool started;

static void set_key(char key[KEY_SIZE], const char *name) { str_copy(key, KEY_SIZE, name); }

#ifdef _WIN32
// ── Windows: console API ────────────────────────────────────────────────────

#include <windows.h>

static HANDLE input;
static HANDLE output;
static DWORD saved_input_mode;
static DWORD saved_output_mode;
static UINT saved_code_page;

void terminal_write(const char *text, size_t length) {
	DWORD written;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, (DWORD)length, &written, NULL);
}

bool terminal_start(void) {
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

void terminal_stop(void) {
	if (!started) {
		return;
	}
	started = false;
	terminal_write(LEAVE_SCREEN, strlen(LEAVE_SCREEN));
	SetConsoleOutputCP(saved_code_page);
	SetConsoleMode(output, saved_output_mode);
	SetConsoleMode(input, saved_input_mode);
}

void terminal_size(int *columns, int *rows) {
	CONSOLE_SCREEN_BUFFER_INFO info;
	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
		*columns = info.srWindow.Right - info.srWindow.Left + 1;
		*rows = info.srWindow.Bottom - info.srWindow.Top + 1;
	} else {
		*columns = 100;
		*rows = 30;
	}
}

int64_t terminal_now_ms(void) { return (int64_t)GetTickCount64(); }

// Returns false for events that are not a key the game understands.
static bool decode_key(const KEY_EVENT_RECORD *event, char key[KEY_SIZE]) {
	switch (event->wVirtualKeyCode) {
	case VK_RETURN:
		set_key(key, "enter");
		return true;
	case VK_ESCAPE:
		set_key(key, "escape");
		return true;
	case VK_BACK:
		set_key(key, "backspace");
		return true;
	case VK_UP:
		set_key(key, "up");
		return true;
	case VK_DOWN:
		set_key(key, "down");
		return true;
	case VK_LEFT:
		set_key(key, "left");
		return true;
	case VK_RIGHT:
		set_key(key, "right");
		return true;
	default:
		break;
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

KeyResult terminal_read_key(int timeout_ms, char key[KEY_SIZE]) {
	int64_t deadline = terminal_now_ms() + timeout_ms;
	for (;;) {
		DWORD wait = INFINITE;
		if (timeout_ms >= 0) {
			int64_t remaining = deadline - terminal_now_ms();
			wait = remaining > 0 ? (DWORD)remaining : 0;
		}
		if (WaitForSingleObject(input, wait) != WAIT_OBJECT_0) {
			return KEY_TIMEOUT;
		}
		INPUT_RECORD record;
		DWORD count;
		if (!ReadConsoleInputW(input, &record, 1, &count) || count == 0) {
			return KEY_END;
		}
		if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
			// Not a key, but the caller redraws after every return: report it as a timeout.
			return KEY_TIMEOUT;
		}
		if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown &&
		    decode_key(&record.Event.KeyEvent, key)) {
			return KEY_READ;
		}
	}
}

#else
// ── POSIX: termios + poll ───────────────────────────────────────────────────

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// How long a lone ESC waits for the rest of an escape sequence before it counts as the Escape key.
#define ESCAPE_TIMEOUT_MS 30

static struct termios saved_mode;

void terminal_write(const char *text, size_t length) {
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

bool terminal_start(void) {
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

void terminal_stop(void) {
	if (!started) {
		return;
	}
	started = false;
	terminal_write(LEAVE_SCREEN, strlen(LEAVE_SCREEN));
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_mode);
}

void terminal_size(int *columns, int *rows) {
	struct winsize size;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0) {
		*columns = size.ws_col;
		*rows = size.ws_row;
	} else {
		*columns = 100;
		*rows = 30;
	}
}

int64_t terminal_now_ms(void) {
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

KeyResult terminal_read_key(int timeout_ms, char key[KEY_SIZE]) {
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
			return KEY_END;
		}
		if (status == 0) {
			return KEY_TIMEOUT;
		}
		if (byte == 0x1b) {
			if (read_escape_sequence(key)) {
				return KEY_READ;
			}
		} else if (byte == '\r' || byte == '\n') {
			set_key(key, "enter");
			return KEY_READ;
		} else if (byte == 0x7f || byte == 0x08) {
			set_key(key, "backspace");
			return KEY_READ;
		} else if (byte == 0x03) {
			set_key(key, "ctrl+c");
			return KEY_READ;
		} else if (byte >= 0x20) {
			// A UTF-8 lead byte says how many continuation bytes follow.
			int extra = byte >= 0xF0 ? 3 : byte >= 0xE0 ? 2 : byte >= 0xC0 ? 1 : 0;
			key[0] = (char)byte;
			int length = 1;
			while (extra-- > 0 && read_byte(ESCAPE_TIMEOUT_MS, &byte) == 1) {
				key[length++] = (char)byte;
			}
			key[length] = '\0';
			return KEY_READ;
		}
	}
}

#endif
