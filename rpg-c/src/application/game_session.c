#include "application/game_session.h"

#include <string.h>

static bool session_init(GameSession *session, const GameData *data, Repositories repositories, Clock clock,
    const char *game_version, char *error, size_t error_size) {
	session->data = data;
	session->repositories = repositories;
	session->clock = clock;
	str_copy(session->game_version, VERSION_SIZE, game_version);
	if (repositories.load_profile(repositories.context, &session->profile, error, error_size) != LOAD_OK) {
		return false;
	}
	session->segment_started = clock.now(clock.context);
	return true;
}

static int64_t accumulate_play_time(GameSession *session) {
	int64_t now = session->clock.now(session->clock.context);
	int64_t elapsed = now - session->segment_started;
	session->info.play_time_seconds += elapsed > 0 ? elapsed : 0;
	session->segment_started = now;
	return now;
}

static void write_save(GameSession *session) {
	if (!session->has_snapshot) {
		return;
	}
	int64_t now = accumulate_play_time(session);
	// A shallow view of the snapshot: the repository only reads it, so nothing here is freed.
	SaveGame save = {.rng_state = session->snapshot_rng_state, .session = session->info, .run = session->snapshot};
	str_copy(save.game_version, VERSION_SIZE, session->game_version);
	str_copy(save.implementation, VERSION_SIZE, IMPLEMENTATION);
	format_timestamp(now, save.saved_at);
	session->repositories.write_save(session->repositories.context, &save);
}

static void finish(GameSession *session) {
	const RunState *state = &session->engine.state;
	RunRecord *record = &session->finished_record;
	memset(record, 0, sizeof(*record));
	format_timestamp(accumulate_play_time(session), record->ended_at);
	str_copy(record->run_id, RUN_ID_SIZE, session->info.run_id);
	str_copy(record->name, NAME_SIZE, state->config.name);
	id_set(record->vocation, state->config.vocation_id);
	id_set(record->difficulty, state->config.difficulty_id);
	record->seed = state->seed;
	str_copy(record->implementation, VERSION_SIZE, IMPLEMENTATION);
	str_copy(record->game_version, VERSION_SIZE, session->game_version);
	str_copy(record->started_at, TIMESTAMP_SIZE, session->info.started_at);
	record->play_time_seconds = session->info.play_time_seconds;
	record->sessions = session->info.sessions;
	record->round = state->round;
	record->level = state->player.level;
	record->magic_level = state->player.magic_level;
	id_set(record->death_cause, state->has_death_cause ? state->death_cause : "");
	record->won = state->won;
	record->stats = statistics_clone(&state->stats);
	session->has_finished_record = true;

	session->repositories.add_history(session->repositories.context, record);
	session->repositories.delete_save(session->repositories.context);

	HallOfFameEntry entry;
	memset(&entry, 0, sizeof(entry));
	str_copy(entry.run_id, RUN_ID_SIZE, record->run_id);
	str_copy(entry.name, NAME_SIZE, record->name);
	id_set(entry.vocation, record->vocation);
	id_set(entry.difficulty, record->difficulty);
	entry.round = record->round;
	entry.level = record->level;
	str_copy(entry.ended_at, TIMESTAMP_SIZE, record->ended_at);
	entry.won = record->won;
	profile_record_finished_run(&session->profile, &entry);
}

static void after_step(GameSession *session, const EventList *events, size_t first, AchievementList *unlocked) {
	char now[TIMESTAMP_SIZE];
	format_timestamp(session->clock.now(session->clock.context), now);
	const RunState *state = &session->engine.state;
	EventList produced = {.items = events->items + first, .count = events->count - first};
	AchievementList local = {0};
	AchievementList *target = unlocked != NULL ? unlocked : &local;
	size_t unlocked_before = target->count;
	profile_observe(&session->profile, session->data, &produced, state, now, session->info.run_id, target);
	bool profile_changed = target->count > unlocked_before;
	for (size_t i = 0; i < produced.count; i++) {
		profile_changed = profile_changed || event_is(&produced.items[i], "monster_killed");
	}
	achievement_list_free(&local);

	if (state->phase == PHASE_MERCHANT || state->phase == PHASE_VICTORY) {
		if (session->has_snapshot) {
			run_state_free(&session->snapshot);
		}
		session->snapshot = run_state_clone(state);
		session->snapshot_rng_state = session->engine.rng.state;
		session->has_snapshot = true;
		write_save(session);
	} else if (state->phase == PHASE_GAME_OVER && !session->has_finished_record) {
		finish(session);
		profile_changed = true;
	}
	if (profile_changed) {
		session->repositories.save_profile(session->repositories.context, &session->profile);
	}
}

bool session_start(GameSession *session, const GameData *data, const RunConfig *config, int64_t seed,
    Repositories repositories, Clock clock, const char *game_version, EventList *events, char *error,
    size_t error_size) {
	memset(session, 0, sizeof(*session));
	size_t first = events->count;
	if (!engine_new_run(&session->engine, data, config, seed, events, error, error_size)) {
		return false;
	}
	int64_t now = clock.now(clock.context);
	make_run_id(now, seed, session->info.run_id);
	format_timestamp(now, session->info.started_at);
	session->info.sessions = 1;
	if (!session_init(session, data, repositories, clock, game_version, error, error_size)) {
		engine_free(&session->engine);
		return false;
	}
	after_step(session, events, first, NULL);
	return true;
}

LoadResult session_resume(GameSession *session, const GameData *data, Repositories repositories, Clock clock,
    const char *game_version, char *error, size_t error_size) {
	memset(session, 0, sizeof(*session));
	SaveGame save;
	LoadResult result = repositories.load_save(repositories.context, &save, error, error_size);
	if (result != LOAD_OK) {
		return result;
	}
	session->info = save.session;
	session->info.sessions++;
	if (!session_init(session, data, repositories, clock, game_version, error, error_size)) {
		save_game_free(&save);
		return LOAD_FAILED;
	}
	session->snapshot = run_state_clone(&save.run);
	session->snapshot_rng_state = save.rng_state;
	session->has_snapshot = true;
	// The engine takes over the loaded run, so `save` must not be freed.
	engine_restore(&session->engine, data, save.run, save.rng_state);
	return LOAD_OK;
}

void session_step(GameSession *session, const Command *command, EventList *events, AchievementList *unlocked) {
	size_t first = events->count;
	engine_step(&session->engine, command, events);
	after_step(session, events, first, unlocked);
}

void session_save_and_quit(GameSession *session) {
	if (session->engine.state.phase != PHASE_GAME_OVER) {
		write_save(session);
	}
}

void session_free(GameSession *session) {
	engine_free(&session->engine);
	profile_free(&session->profile);
	if (session->has_snapshot) {
		run_state_free(&session->snapshot);
	}
	if (session->has_finished_record) {
		run_record_free(&session->finished_record);
	}
	memset(session, 0, sizeof(*session));
}
