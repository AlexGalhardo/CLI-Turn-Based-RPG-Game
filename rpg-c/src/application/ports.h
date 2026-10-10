// Ports implemented by the infrastructure layer (Dependency Inversion: the application owns the interfaces).
// In C an interface is a struct of function pointers plus a `context` pointer handed back to every call.
#ifndef RPG_APPLICATION_PORTS_H
#define RPG_APPLICATION_PORTS_H

#include "application/profile.h"
#include "application/save_game.h"

// Seconds since 1970-01-01T00:00:00Z.
typedef struct {
	int64_t (*now)(void *context);
	void *context;
} Clock;

typedef enum {
	LOAD_OK,
	LOAD_MISSING, // there is no such file (not an error)
	LOAD_FAILED,  // unreadable, malformed or written by a newer game version; `error` says why
} LoadResult;

// Writing is expected to work: an implementation that cannot write stops the program with a clear message.
typedef struct {
	void *context;
	LoadResult (*load_save)(void *context, SaveGame *out, char *error, size_t error_size);
	void (*write_save)(void *context, const SaveGame *save);
	void (*delete_save)(void *context);
	void (*add_history)(void *context, const RunRecord *record);
	// A missing profile loads as an empty one (LOAD_OK).
	LoadResult (*load_profile)(void *context, Profile *out, char *error, size_t error_size);
	void (*save_profile)(void *context, const Profile *profile);
} Repositories;

#endif
