// The impure edge of the TUI: raw keyboard input, terminal size and output.
// This file and infrastructure/filesystem.c hold all the platform-specific code of the port: termios + poll on
// POSIX, the Win32 console API on Windows. Everything above it works with key names and plain strings.
#ifndef RPG_PRESENTATION_TUI_TERMINAL_H
#define RPG_PRESENTATION_TUI_TERMINAL_H

#include "domain/base.h"

// A key name: a printable character (UTF-8) or "enter", "escape", "backspace", "up", "down", "left", "right",
// "ctrl+c".
#define KEY_SIZE 16

typedef enum { KEY_READ, KEY_TIMEOUT, KEY_END } KeyResult;

// Switches to raw mode, the alternate screen and a hidden cursor. Returns false when stdin or stdout is not an
// interactive terminal (a pipe, a file): the caller then falls back to line input.
bool terminal_start(void);
// Restores the terminal. Safe to call when terminal_start() failed or was never called.
void terminal_stop(void);
void terminal_size(int *columns, int *rows);
// Waits up to `timeout_ms` (negative = forever) for one key and writes its name to `key`.
KeyResult terminal_read_key(int timeout_ms, char key[KEY_SIZE]);
void terminal_write(const char *text, size_t length);
// Milliseconds from a monotonic clock (only differences are meaningful).
int64_t terminal_now_ms(void);

#endif
