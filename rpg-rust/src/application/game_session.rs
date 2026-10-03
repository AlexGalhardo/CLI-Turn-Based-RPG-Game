//! Use case that wraps the pure engine with time, persistence and the profile.
//!
//! The engine stays deterministic; everything that depends on the clock or the filesystem happens here.

use std::fmt;
use std::rc::Rc;
use std::time::SystemTime;

use crate::application::commands::Command;
use crate::application::engine::{GameEngine, InvalidRunConfig};
use crate::application::events::Event;
use crate::application::ports::{Clock, HistoryRepository, ProfileRepository, SaveRepository};
use crate::application::profile::{HallOfFameEntry, ProfileService};
use crate::application::run_state::{RunConfig, RunState};
use crate::application::save_game::{
	IMPLEMENTATION, PersistenceError, RunRecord, SaveGame, SessionInfo, format_timestamp, make_run_id,
};
use crate::domain::definitions::{AchievementDef, GameData};
use crate::domain::enums::Phase;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StepResult {
	pub events: Vec<Event>,
	pub achievements: Vec<AchievementDef>,
}

/// The persistence ports. `Rc<dyn Trait>` is the Rust spelling of "any object implementing the interface",
/// shared by the session and the UI controller.
#[derive(Clone)]
pub struct Repositories {
	pub saves: Rc<dyn SaveRepository>,
	pub history: Rc<dyn HistoryRepository>,
	pub profile: Rc<dyn ProfileRepository>,
}

/// Why a session could not start.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SessionError {
	InvalidConfig(InvalidRunConfig),
	Persistence(PersistenceError),
}

impl fmt::Display for SessionError {
	fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
		match self {
			SessionError::InvalidConfig(error) => error.fmt(formatter),
			SessionError::Persistence(error) => error.fmt(formatter),
		}
	}
}

impl std::error::Error for SessionError {}

impl From<InvalidRunConfig> for SessionError {
	fn from(error: InvalidRunConfig) -> SessionError {
		SessionError::InvalidConfig(error)
	}
}

impl From<PersistenceError> for SessionError {
	fn from(error: PersistenceError) -> SessionError {
		SessionError::Persistence(error)
	}
}

pub struct GameSession {
	pub data: Rc<GameData>,
	pub engine: GameEngine,
	pub info: SessionInfo,
	pub profile: ProfileService,
	pub finished_record: Option<RunRecord>,
	repositories: Repositories,
	clock: Rc<dyn Clock>,
	game_version: String,
	segment_started: SystemTime,
	merchant_snapshot: Option<(RunState, u32)>,
}

impl GameSession {
	fn new(
		data: Rc<GameData>,
		engine: GameEngine,
		info: SessionInfo,
		repositories: Repositories,
		clock: Rc<dyn Clock>,
		game_version: &str,
	) -> Result<GameSession, PersistenceError> {
		let profile = ProfileService::new(Rc::clone(&data), repositories.profile.load()?);
		let segment_started = clock.now();
		Ok(GameSession {
			data,
			engine,
			info,
			profile,
			finished_record: None,
			repositories,
			clock,
			game_version: game_version.to_owned(),
			segment_started,
			merchant_snapshot: None,
		})
	}

	pub fn state(&self) -> &RunState {
		self.engine.state()
	}

	/// Mutable state for tests that set up scenarios.
	pub fn state_mut(&mut self) -> &mut RunState {
		self.engine.state_mut()
	}

	// ── creation ──────────────────────────────────────────────────────────────

	pub fn start(
		data: Rc<GameData>,
		config: RunConfig,
		seed: u64,
		repositories: Repositories,
		clock: Rc<dyn Clock>,
		game_version: &str,
	) -> Result<(GameSession, Vec<Event>), SessionError> {
		let (engine, events) = GameEngine::new_run(Rc::clone(&data), config, seed)?;
		let now = clock.now();
		let info = SessionInfo::new(make_run_id(now, seed), format_timestamp(now));
		let mut session = GameSession::new(data, engine, info, repositories, clock, game_version)?;
		session.after_step(&events)?;
		Ok((session, events))
	}

	pub fn resume(
		data: Rc<GameData>,
		repositories: Repositories,
		clock: Rc<dyn Clock>,
		game_version: &str,
	) -> Result<Option<GameSession>, PersistenceError> {
		let Some(save) = repositories.saves.load()? else {
			return Ok(None);
		};
		let engine = GameEngine::restore(Rc::clone(&data), save.run.clone(), save.rng_state);
		let mut info = save.session;
		info.sessions += 1;
		let mut session = GameSession::new(data, engine, info, repositories, clock, game_version)?;
		session.merchant_snapshot = Some((save.run, save.rng_state));
		Ok(Some(session))
	}

	// ── play ──────────────────────────────────────────────────────────────────

	pub fn step(&mut self, command: &Command) -> Result<StepResult, PersistenceError> {
		let events = self.engine.step(command);
		let achievements = self.after_step(&events)?;
		Ok(StepResult { events, achievements })
	}

	fn after_step(&mut self, events: &[Event]) -> Result<Vec<AchievementDef>, PersistenceError> {
		let now = format_timestamp(self.clock.now());
		let unlocked = self.profile.observe(events, self.engine.state(), &now, &self.info.run_id);
		let mut profile_changed =
			!unlocked.is_empty() || events.iter().any(|event| matches!(event, Event::MonsterKilled { .. }));
		match self.state().phase {
			Phase::Merchant | Phase::Victory => {
				self.merchant_snapshot = Some((self.state().clone(), self.engine.rng_state()));
				self.write_save()?;
			}
			Phase::GameOver if self.finished_record.is_none() => {
				self.finish()?;
				profile_changed = true;
			}
			_ => {}
		}
		if profile_changed {
			self.repositories.profile.save(&self.profile.profile)?;
		}
		Ok(unlocked)
	}

	/// Persists play time. Mid-battle quits resume from the last merchant (or victory) snapshot.
	pub fn save_and_quit(&mut self) -> Result<(), PersistenceError> {
		if self.state().phase != Phase::GameOver {
			self.write_save()?;
		}
		Ok(())
	}

	// ── persistence ───────────────────────────────────────────────────────────

	fn accumulate_play_time(&mut self) -> SystemTime {
		let now = self.clock.now();
		let elapsed = now.duration_since(self.segment_started).map_or(0, |duration| duration.as_secs() as i64);
		self.info.play_time_seconds += elapsed;
		self.segment_started = now;
		now
	}

	fn write_save(&mut self) -> Result<(), PersistenceError> {
		if self.merchant_snapshot.is_none() {
			return Ok(());
		}
		let now = self.accumulate_play_time();
		let (run, rng_state) = self.merchant_snapshot.clone().expect("checked above");
		let save = SaveGame::new(&self.game_version, format_timestamp(now), rng_state, self.info.clone(), run);
		self.repositories.saves.save(&save)
	}

	fn finish(&mut self) -> Result<(), PersistenceError> {
		let now = format_timestamp(self.accumulate_play_time());
		let state = self.engine.state();
		let record = RunRecord {
			schema_version: crate::application::save_game::SCHEMA_VERSION,
			run_id: self.info.run_id.clone(),
			name: state.config.name.clone(),
			vocation: state.config.vocation_id.clone(),
			difficulty: state.config.difficulty_id.clone(),
			seed: state.seed,
			implementation: IMPLEMENTATION.to_owned(),
			game_version: self.game_version.clone(),
			started_at: self.info.started_at.clone(),
			ended_at: now,
			play_time_seconds: self.info.play_time_seconds,
			sessions: self.info.sessions,
			round: state.round,
			level: state.player.level,
			magic_level: state.player.magic_level,
			death_cause: state.death_cause.clone().unwrap_or_default(),
			won: state.won,
			stats: state.stats.clone(),
		};
		self.repositories.history.add(&record)?;
		self.repositories.saves.delete()?;
		self.profile.record_finished_run(HallOfFameEntry {
			run_id: record.run_id.clone(),
			name: record.name.clone(),
			vocation: record.vocation.clone(),
			difficulty: record.difficulty.clone(),
			round: record.round,
			level: record.level,
			ended_at: record.ended_at.clone(),
			won: record.won,
		});
		self.finished_record = Some(record);
		Ok(())
	}
}
