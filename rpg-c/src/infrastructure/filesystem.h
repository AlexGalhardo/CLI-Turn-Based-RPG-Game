// The few file operations the game needs. ISO C has no directories, so this is one of the two places with
// platform-specific code (the other is presentation/tui/terminal.c): POSIX calls, with the Windows spellings where
// MinGW differs.
#ifndef RPG_INFRASTRUCTURE_FILESYSTEM_H
#define RPG_INFRASTRUCTURE_FILESYSTEM_H

#include "domain/base.h"

#define PATH_SIZE 1024

// Joins two path parts with '/' (Windows accepts it too) into a PATH_SIZE buffer.
void path_join(char *out, const char *directory, const char *name);
bool fs_exists(const char *path);
bool fs_is_directory(const char *path);
// Creates the directory and its missing parents.
bool fs_make_directories(const char *path);
// Whole file as a heap string (caller frees), or NULL when it cannot be read.
char *fs_read_text(const char *path);
// Writes to "<path>.tmp" then renames, so a crash never leaves a half-written file. Creates parent directories.
bool fs_write_text_atomic(const char *path, const char *text);
// Removing a file that does not exist is not an error.
bool fs_remove_file(const char *path);
bool fs_remove_tree(const char *path);
// Names (not paths) of the entries of `directory` ending in `suffix`, sorted. Free with fs_free_names().
char **fs_list(const char *directory, const char *suffix, size_t *count);
void fs_free_names(char **names, size_t count);

#endif
