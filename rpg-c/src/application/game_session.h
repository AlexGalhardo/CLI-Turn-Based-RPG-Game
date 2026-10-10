// Use case that wraps the pure engine with time, persistence and the profile.
// The engine stays deterministic; everything that depends on the clock or the filesystem happens here.
#ifndef RPG_APPLICATION_GAME_SESSION_H
#define RPG_APPLICATION_GAME_SESSION_H

#include "application/engine.h"
#include "application/ports.h"

typedef struct {
	const GameData *data;
	GameEngine engine;
	SessionInfo info;
	Repositories repositories;
	Clock clock;
	char game_version[VERSION_SIZE];
	Profile profile;
	int64_t segment_started;
	// The state saved to disk: the last merchant (or victory) moment, so a mid-battle quit resumes before the fight.
	bool has_snapshot;
	RunState snapshot;
	uint32_t snapshot_rng_state;
	bool has_finished_record;
	RunRecord finished_record;
} GameSession;

// Starts a run (appending its first events) and writes the first save. Returns false and fills `error` for an
// invalid config or an unreadable profile; the session is then unusable and needs no cleanup.
bool session_start(GameSession *session, const GameData *data, const RunConfig *config, int64_t seed,
    Repositories repositories, Clock clock, const char *game_version, EventList *events, char *error,
    size_t error_size);
// Continues the saved run. LOAD_MISSING when there is no save; LOAD_FAILED fills `error`.
LoadResult session_resume(GameSession *session, const GameData *data, Repositories repositories, Clock clock,
    const char *game_version, char *error, size_t error_size);
// Plays one command: appends its events and the achievements it unlocked (`unlocked` may be NULL).
void session_step(GameSession *session, const Command *command, EventList *events, AchievementList *unlocked);
// Persists play time. Mid-battle quits resume from the last merchant (or victory) snapshot.
void session_save_and_quit(GameSession *session);
void session_free(GameSession *session);

#endif
