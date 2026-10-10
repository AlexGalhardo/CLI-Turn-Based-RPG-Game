// Small building blocks every layer uses: content ids, fatal errors, checked allocation and a growable string.
// C has no exceptions: a bug (an unknown id, an impossible state) stops the program with `fatal()`, while expected
// failures (a broken save, a usage error) are returned as values by the functions that can meet them.
#ifndef RPG_DOMAIN_BASE_H
#define RPG_DOMAIN_BASE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Content ids ("health_potion", "fire") live in fixed buffers, so structs holding them copy with plain assignment.
#define ID_SIZE 48
typedef char Id[ID_SIZE];

// Player names: 16 characters, each up to 4 UTF-8 bytes.
#define NAME_SIZE 96

#define ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

#if defined(__GNUC__)
#define RPG_PRINTF(format_index, first_arg) __attribute__((format(printf, format_index, first_arg)))
#define RPG_NORETURN __attribute__((noreturn))
#else
#define RPG_PRINTF(format_index, first_arg)
#define RPG_NORETURN
#endif

RPG_NORETURN void fatal(const char *format, ...) RPG_PRINTF(1, 2);

void *xmalloc(size_t size);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *pointer, size_t size);
char *xstrdup(const char *text);

bool str_eq(const char *a, const char *b);
bool str_starts_with(const char *text, const char *prefix);
// Copies `source` into a buffer of `size` bytes; a text that does not fit is a bug.
void str_copy(char *destination, size_t size, const char *source);
void id_set(char *destination, const char *source);
// Number of code points in a UTF-8 text (what a terminal shows as columns for the characters the game uses).
size_t utf8_length(const char *text);

// Growable string. `data` is always NUL-terminated once something was appended; release it with sb_free().
typedef struct {
	char *data;
	size_t length;
	size_t capacity;
} StrBuf;

void sb_append(StrBuf *buffer, const char *text);
void sb_append_n(StrBuf *buffer, const char *text, size_t length);
void sb_append_char(StrBuf *buffer, char character);
void sb_appendf(StrBuf *buffer, const char *format, ...) RPG_PRINTF(2, 3);
void sb_clear(StrBuf *buffer);
void sb_free(StrBuf *buffer);
// Hands the heap string over to the caller (never NULL) and resets the buffer.
char *sb_take(StrBuf *buffer);

// Appends one element to a heap array described by (items, count, capacity), growing it when full.
#define VEC_PUSH(items, count, capacity, value)                                                                        \
	do {                                                                                                               \
		if ((count) == (capacity)) {                                                                                   \
			(capacity) = (capacity) == 0 ? 8 : (capacity) * 2;                                                         \
			(items) = xrealloc((items), (size_t)(capacity) * sizeof(*(items)));                                        \
		}                                                                                                              \
		(items)[(count)++] = (value);                                                                                  \
	} while (0)

#endif
