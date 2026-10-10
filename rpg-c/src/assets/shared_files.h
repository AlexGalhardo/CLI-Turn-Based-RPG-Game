// The shared/ tree (data, i18n, art) compiled into the binary: C has no go:embed, so cmake/embed_shared.cmake turns
// every file into a byte array in a generated shared_files.c. The executable needs no files next to it.
#ifndef RPG_ASSETS_SHARED_FILES_H
#define RPG_ASSETS_SHARED_FILES_H

#include <stddef.h>

typedef struct {
	const char *path;    // relative to shared/, e.g. "data/monsters.json" or "art/families/orc.txt"
	const char *content; // NUL-terminated UTF-8 with LF line endings
	size_t size;
} SharedFile;

extern const SharedFile SHARED_FILES[];
extern const size_t SHARED_FILE_COUNT;

// Content of an embedded file, or NULL when there is no such file.
const char *shared_file(const char *path);

#endif
