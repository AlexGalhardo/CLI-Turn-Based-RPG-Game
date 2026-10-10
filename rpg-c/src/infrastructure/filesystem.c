#include "infrastructure/filesystem.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define make_directory(path) _mkdir(path)
#define remove_directory(path) _rmdir(path)
#else
#include <unistd.h>
#define make_directory(path) mkdir(path, 0755)
#define remove_directory(path) rmdir(path)
#endif

void path_join(char *out, const char *directory, const char *name) {
	int written = snprintf(out, PATH_SIZE, "%s/%s", directory, name);
	if (written < 0 || written >= PATH_SIZE) {
		fatal("path too long: %s/%s", directory, name);
	}
}

bool fs_exists(const char *path) {
	struct stat info;
	return stat(path, &info) == 0;
}

bool fs_is_directory(const char *path) {
	struct stat info;
	return stat(path, &info) == 0 && (info.st_mode & S_IFMT) == S_IFDIR;
}

bool fs_make_directories(const char *path) {
	char partial[PATH_SIZE];
	str_copy(partial, sizeof(partial), path);
	size_t length = strlen(partial);
	for (size_t i = 1; i <= length; i++) {
		if (partial[i] != '/' && partial[i] != '\\' && partial[i] != '\0') {
			continue;
		}
		char saved = partial[i];
		partial[i] = '\0';
		// "C:" is a drive, not a directory to create.
		bool is_drive = i == 2 && partial[1] == ':';
		if (!is_drive && !fs_is_directory(partial) && make_directory(partial) != 0 && errno != EEXIST) {
			return false;
		}
		partial[i] = saved;
	}
	return fs_is_directory(path);
}

char *fs_read_text(const char *path) {
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		return NULL;
	}
	StrBuf content = {0};
	char chunk[4096];
	size_t read;
	while ((read = fread(chunk, 1, sizeof(chunk), file)) > 0) {
		sb_append_n(&content, chunk, read);
	}
	bool failed = ferror(file) != 0;
	fclose(file);
	if (failed) {
		sb_free(&content);
		return NULL;
	}
	return sb_take(&content);
}

static void parent_directory(const char *path, char *out) {
	str_copy(out, PATH_SIZE, path);
	char *slash = strrchr(out, '/');
	char *backslash = strrchr(out, '\\');
	if (backslash != NULL && (slash == NULL || backslash > slash)) {
		slash = backslash;
	}
	if (slash == NULL) {
		out[0] = '\0';
	} else {
		*slash = '\0';
	}
}

bool fs_write_text_atomic(const char *path, const char *text) {
	char parent[PATH_SIZE];
	parent_directory(path, parent);
	if (parent[0] != '\0' && !fs_make_directories(parent)) {
		return false;
	}
	char temporary[PATH_SIZE];
	int written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	if (written < 0 || written >= (int)sizeof(temporary)) {
		return false;
	}
	// Binary mode: the files use LF on every platform, like the other implementations.
	FILE *file = fopen(temporary, "wb");
	if (file == NULL) {
		return false;
	}
	size_t length = strlen(text);
	bool ok = fwrite(text, 1, length, file) == length;
	ok = fclose(file) == 0 && ok;
	if (!ok) {
		remove(temporary);
		return false;
	}
#ifdef _WIN32
	// rename() on Windows fails when the target exists; MoveFileEx replaces it in one step.
	return MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
	return rename(temporary, path) == 0;
#endif
}

bool fs_remove_file(const char *path) { return remove(path) == 0 || !fs_exists(path); }

static int compare_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

char **fs_list(const char *directory, const char *suffix, size_t *count) {
	char **names = NULL;
	size_t capacity = 0;
	*count = 0;
	DIR *handle = opendir(directory);
	if (handle == NULL) {
		return NULL;
	}
	size_t suffix_length = strlen(suffix);
	struct dirent *entry;
	while ((entry = readdir(handle)) != NULL) {
		size_t length = strlen(entry->d_name);
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || length < suffix_length ||
		    strcmp(entry->d_name + length - suffix_length, suffix) != 0) {
			continue;
		}
		char *name = xstrdup(entry->d_name);
		VEC_PUSH(names, *count, capacity, name);
	}
	closedir(handle);
	if (*count > 0) {
		qsort(names, *count, sizeof(char *), compare_names);
	}
	return names;
}

void fs_free_names(char **names, size_t count) {
	for (size_t i = 0; i < count; i++) {
		free(names[i]);
	}
	free(names);
}

bool fs_remove_tree(const char *path) {
	if (!fs_is_directory(path)) {
		return fs_remove_file(path);
	}
	size_t count;
	char **names = fs_list(path, "", &count);
	bool ok = true;
	for (size_t i = 0; i < count; i++) {
		char child[PATH_SIZE];
		path_join(child, path, names[i]);
		ok = fs_remove_tree(child) && ok;
	}
	fs_free_names(names, count);
	return remove_directory(path) == 0 && ok;
}
