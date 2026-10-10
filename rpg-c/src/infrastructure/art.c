#include "infrastructure/art.h"

#include "assets/shared_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy_range(const char *start, size_t length) {
	char *text = xmalloc(length + 1);
	memcpy(text, start, length);
	text[length] = '\0';
	return text;
}

static void add_frame(Animation *animation) {
	animation->frames = xrealloc(animation->frames, (animation->frame_count + 1) * sizeof(Frame));
	animation->frames[animation->frame_count++] = (Frame){0};
}

bool parse_art(const char *text, Animations *out, char *error, size_t error_size) {
	out->items = NULL;
	out->count = 0;
	// Trailing newlines do not make an empty last line.
	size_t length = strlen(text);
	while (length > 0 && text[length - 1] == '\n') {
		length--;
	}
	Animation *current = NULL;
	size_t start = 0;
	while (start <= length) {
		size_t end = start;
		while (end < length && text[end] != '\n') {
			end++;
		}
		const char *line = text + start;
		size_t line_length = end - start;
		if (line_length > 0 && line[0] == '@') {
			// The animation name is the rest of the line, without surrounding spaces.
			const char *name = line + 1;
			size_t name_length = line_length - 1;
			while (name_length > 0 && (*name == ' ' || *name == '\t')) {
				name++;
				name_length--;
			}
			while (name_length > 0 && (name[name_length - 1] == ' ' || name[name_length - 1] == '\t')) {
				name_length--;
			}
			out->items = xrealloc(out->items, (out->count + 1) * sizeof(Animation));
			current = &out->items[out->count++];
			memset(current, 0, sizeof(*current));
			if (name_length >= ID_SIZE) {
				name_length = ID_SIZE - 1;
			}
			memcpy(current->name, name, name_length);
			add_frame(current);
		} else if (line_length == 2 && line[0] == '%' && line[1] == '%') {
			if (current == NULL) {
				snprintf(error, error_size, "frame separator before any @animation");
				animations_free(out);
				return false;
			}
			add_frame(current);
		} else if (current == NULL) {
			snprintf(error, error_size, "art content before the first @animation");
			animations_free(out);
			return false;
		} else {
			Frame *frame = &current->frames[current->frame_count - 1];
			frame->lines = xrealloc(frame->lines, (frame->line_count + 1) * sizeof(char *));
			frame->lines[frame->line_count++] = copy_range(line, line_length);
		}
		start = end + 1;
	}
	return true;
}

void animations_free(Animations *animations) {
	for (size_t a = 0; a < animations->count; a++) {
		Animation *animation = &animations->items[a];
		for (size_t f = 0; f < animation->frame_count; f++) {
			for (size_t l = 0; l < animation->frames[f].line_count; l++) {
				free(animation->frames[f].lines[l]);
			}
			free(animation->frames[f].lines);
		}
		free(animation->frames);
	}
	free(animations->items);
	animations->items = NULL;
	animations->count = 0;
}

const Animation *find_animation(const Animations *animations, const char *name) {
	// A name defined twice keeps its last definition, like assigning a dictionary key twice.
	const Animation *found = NULL;
	for (size_t i = 0; i < animations->count; i++) {
		if (strcmp(animations->items[i].name, name) == 0) {
			found = &animations->items[i];
		}
	}
	return found;
}

const Frame *frame_for(const Animations *animations, const char *animation, int64_t tick) {
	if (animations == NULL) {
		return NULL;
	}
	const Animation *found = find_animation(animations, animation);
	if (found == NULL) {
		found = find_animation(animations, "idle");
	}
	if (found == NULL || found->frame_count == 0) {
		return NULL;
	}
	return &found->frames[(size_t)tick % found->frame_count];
}

// ── library (parsed once per file) ──────────────────────────────────────────

typedef struct {
	char path[96];
	bool exists;
	Animations animations;
} CachedArt;

// Entries are allocated one by one: the pointers handed out stay valid when the list grows.
static CachedArt **cache;
static size_t cache_count;
static size_t cache_capacity;

const Animations *art_load_file(const char *folder, const char *name) {
	char path[96];
	snprintf(path, sizeof(path), "art/%s/%s.txt", folder, name);
	for (size_t i = 0; i < cache_count; i++) {
		if (strcmp(cache[i]->path, path) == 0) {
			return cache[i]->exists ? &cache[i]->animations : NULL;
		}
	}
	CachedArt *entry = xcalloc(1, sizeof(CachedArt));
	str_copy(entry->path, sizeof(entry->path), path);
	const char *text = shared_file(path);
	char error[120];
	entry->exists = text != NULL && parse_art(text, &entry->animations, error, sizeof(error));
	VEC_PUSH(cache, cache_count, cache_capacity, entry);
	return entry->exists ? &entry->animations : NULL;
}

const Animations *art_for_creature(const MonsterDef *creature) {
	return creature->is_boss ? art_load_file("bosses", creature->id) : art_load_file("families", creature->family);
}
