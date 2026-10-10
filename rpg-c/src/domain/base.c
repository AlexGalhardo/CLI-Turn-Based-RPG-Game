#include "domain/base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fatal(const char *format, ...) {
	va_list args;
	va_start(args, format);
	fputs("rpg-c: fatal: ", stderr);
	vfprintf(stderr, format, args);
	fputc('\n', stderr);
	va_end(args);
	exit(70);
}

void *xmalloc(size_t size) {
	void *pointer = malloc(size == 0 ? 1 : size);
	if (pointer == NULL) {
		fatal("out of memory");
	}
	return pointer;
}

void *xcalloc(size_t count, size_t size) {
	void *pointer = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
	if (pointer == NULL) {
		fatal("out of memory");
	}
	return pointer;
}

void *xrealloc(void *pointer, size_t size) {
	void *result = realloc(pointer, size == 0 ? 1 : size);
	if (result == NULL) {
		fatal("out of memory");
	}
	return result;
}

char *xstrdup(const char *text) {
	size_t size = strlen(text) + 1;
	char *copy = xmalloc(size);
	memcpy(copy, text, size);
	return copy;
}

bool str_eq(const char *a, const char *b) { return strcmp(a, b) == 0; }

bool str_starts_with(const char *text, const char *prefix) { return strncmp(text, prefix, strlen(prefix)) == 0; }

void str_copy(char *destination, size_t size, const char *source) {
	size_t length = strlen(source);
	if (length >= size) {
		fatal("text too long for a %zu-byte buffer: %s", size, source);
	}
	memcpy(destination, source, length + 1);
}

void id_set(char *destination, const char *source) { str_copy(destination, ID_SIZE, source); }

size_t utf8_length(const char *text) {
	size_t count = 0;
	for (; *text != '\0'; text++) {
		// Continuation bytes look like 10xxxxxx; every other byte starts a code point.
		if (((unsigned char)*text & 0xC0) != 0x80) {
			count++;
		}
	}
	return count;
}

static void sb_reserve(StrBuf *buffer, size_t extra) {
	size_t needed = buffer->length + extra + 1;
	if (needed <= buffer->capacity) {
		return;
	}
	size_t capacity = buffer->capacity == 0 ? 64 : buffer->capacity;
	while (capacity < needed) {
		capacity *= 2;
	}
	buffer->data = xrealloc(buffer->data, capacity);
	buffer->capacity = capacity;
}

void sb_append_n(StrBuf *buffer, const char *text, size_t length) {
	sb_reserve(buffer, length);
	memcpy(buffer->data + buffer->length, text, length);
	buffer->length += length;
	buffer->data[buffer->length] = '\0';
}

void sb_append(StrBuf *buffer, const char *text) { sb_append_n(buffer, text, strlen(text)); }

void sb_append_char(StrBuf *buffer, char character) { sb_append_n(buffer, &character, 1); }

void sb_appendf(StrBuf *buffer, const char *format, ...) {
	va_list args;
	va_list copy;
	va_start(args, format);
	va_copy(copy, args);
	int length = vsnprintf(NULL, 0, format, copy);
	va_end(copy);
	if (length < 0) {
		fatal("invalid format string: %s", format);
	}
	sb_reserve(buffer, (size_t)length);
	vsnprintf(buffer->data + buffer->length, (size_t)length + 1, format, args);
	buffer->length += (size_t)length;
	va_end(args);
}

void sb_clear(StrBuf *buffer) {
	buffer->length = 0;
	if (buffer->data != NULL) {
		buffer->data[0] = '\0';
	}
}

void sb_free(StrBuf *buffer) {
	free(buffer->data);
	buffer->data = NULL;
	buffer->length = 0;
	buffer->capacity = 0;
}

char *sb_take(StrBuf *buffer) {
	char *text = buffer->data == NULL ? xstrdup("") : buffer->data;
	buffer->data = NULL;
	buffer->length = 0;
	buffer->capacity = 0;
	return text;
}
