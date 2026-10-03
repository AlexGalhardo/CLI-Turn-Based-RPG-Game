#include "application/game_session.hpp"

#include <algorithm>

namespace rpg::application {

GameSession::GameSession(GameEngine run, SessionInfo session_info, Profile loaded, SessionContext context) :
    engine(std::move(run)), info(std::move(session_info)), profile(engine.data(), std::move(loaded)),
    context_(std::move(context)), segment_started_(context_.clock->now()) {}

std::expected<std::pair<GameSession, std::vector<Event>>, std::string> GameSession::start(
    const domain::GameData& data, const RunConfig& config, std::uint64_t seed, const SessionContext& context) {
	auto created = GameEngine::new_run(data, config, seed);
	if (!created.has_value()) {
		return std::unexpected(created.error());
	}
	auto& [engine, events] = *created;
	const TimePoint now = context.clock->now();
	SessionInfo session_info{
	    .run_id = make_run_id(now, seed), .started_at = format_timestamp(now), .play_time_seconds = 0, .sessions = 1};
	GameSession session(std::move(engine), std::move(session_info), context.repositories.profile->load(), context);
	session.after_step(events);
	return std::pair{std::move(session), std::move(events)};
}

std::optional<GameSession> GameSession::resume(const domain::GameData& data, const SessionContext& context) {
	std::optional<SaveGame> save = context.repositories.saves->load();
	if (!save.has_value()) {
		return std::nullopt;
	}
	save->session.sessions += 1;
	GameEngine engine = GameEngine::restore(data, save->run, save->rng_state);
	GameSession session(std::move(engine), save->session, context.repositories.profile->load(), context);
	session.snapshot_ = std::move(save->run);
	session.snapshot_rng_ = save->rng_state;
	return session;
}

StepResult GameSession::step(const Command& command) {
	std::vector<Event> events = engine.step(command);
	std::vector<domain::AchievementDef> achievements = after_step(events);
	return StepResult{std::move(events), std::move(achievements)};
}

std::vector<domain::AchievementDef> GameSession::after_step(const std::vector<Event>& events) {
	const std::string now = format_timestamp(context_.clock->now());
	std::vector<domain::AchievementDef> unlocked = profile.observe(events, state(), now, info.run_id);
	bool profile_changed = !unlocked.empty() || std::ranges::any_of(events,
	                                                [](const Event& event) { return event.type == "monster_killed"; });

	if (state().phase == domain::Phase::merchant || state().phase == domain::Phase::victory) {
		snapshot_ = state();
		snapshot_rng_ = engine.rng_state();
		write_save();
	} else if (state().phase == domain::Phase::game_over && !finished_record.has_value()) {
		finish();
		profile_changed = true;
	}
	if (profile_changed) {
		context_.repositories.profile->save(profile.profile);
	}
	return unlocked;
}

void GameSession::save_and_quit() {
	if (state().phase == domain::Phase::game_over) {
		return;
	}
	write_save();
}

TimePoint GameSession::accumulate_play_time() {
	const TimePoint now = context_.clock->now();
	const auto elapsed = std::chrono::floor<std::chrono::seconds>(now - segment_started_).count();
	info.play_time_seconds += std::max<std::int64_t>(0, static_cast<std::int64_t>(elapsed));
	segment_started_ = now;
	return now;
}

void GameSession::write_save() {
	if (!snapshot_.has_value()) {
		return;
	}
	const TimePoint now = accumulate_play_time();
	context_.repositories.saves->save(SaveGame{
	    .schema = schema_version,
	    .game_version = context_.game_version,
	    .implementation_name = std::string(implementation),
	    .saved_at = format_timestamp(now),
	    .rng_state = snapshot_rng_,
	    .session = info,
	    .run = *snapshot_,
	});
}

void GameSession::finish() {
	const std::string now = format_timestamp(accumulate_play_time());
	const RunState& run = state();
	RunRecord record{
	    .schema = schema_version,
	    .run_id = info.run_id,
	    .name = run.config.name,
	    .vocation = run.config.vocation_id,
	    .difficulty = run.config.difficulty_id,
	    .seed = run.seed,
	    .implementation_name = std::string(implementation),
	    .game_version = context_.game_version,
	    .started_at = info.started_at,
	    .ended_at = now,
	    .play_time_seconds = info.play_time_seconds,
	    .sessions = info.sessions,
	    .round = run.round,
	    .level = run.player.level,
	    .magic_level = run.player.magic_level,
	    .death_cause = run.death_cause.value_or(""),
	    .won = run.won,
	    .stats = run.stats,
	};
	context_.repositories.history->add(record);
	context_.repositories.saves->remove();
	profile.record_finished_run(HallOfFameEntry{record.run_id, record.name, record.vocation, record.difficulty,
	    record.round, record.level, record.ended_at, record.won});
	finished_record = std::move(record);
}

} // namespace rpg::application
