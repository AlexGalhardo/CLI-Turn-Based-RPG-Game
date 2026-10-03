"""Use case that wraps the pure engine with time, persistence and the profile.

The engine stays deterministic; everything that depends on the clock or the filesystem happens here.
"""

import copy
from dataclasses import dataclass
from datetime import datetime

from rpg.application.commands import Command
from rpg.application.engine import GameEngine
from rpg.application.events import Event
from rpg.application.ports import Clock, HistoryRepository, ProfileRepository, SaveRepository
from rpg.application.profile import HallOfFameEntry, ProfileService
from rpg.application.run_state import RunConfig, RunState
from rpg.application.save_game import (
	IMPLEMENTATION,
	RunRecord,
	SaveGame,
	SessionInfo,
	format_timestamp,
	make_run_id,
)
from rpg.domain.definitions import AchievementDef, GameData
from rpg.domain.enums import Phase


@dataclass(frozen=True, slots=True)
class StepResult:
	events: list[Event]
	achievements: list[AchievementDef]


@dataclass(frozen=True, slots=True)
class Repositories:
	saves: SaveRepository
	history: HistoryRepository
	profile: ProfileRepository


class GameSession:
	def __init__(
		self,
		data: GameData,
		engine: GameEngine,
		*,
		info: SessionInfo,
		repositories: Repositories,
		clock: Clock,
		game_version: str,
	) -> None:
		self.data = data
		self.engine = engine
		self.info = info
		self._repositories = repositories
		self._clock = clock
		self._game_version = game_version
		self.profile = ProfileService(data, repositories.profile.load())
		self._segment_started = clock.now()
		self._merchant_snapshot: tuple[RunState, int] | None = None
		self.finished_record: RunRecord | None = None

	@property
	def state(self) -> RunState:
		return self.engine.state

	# ── creation ──────────────────────────────────────────────────────────────

	@classmethod
	def start(
		cls,
		data: GameData,
		config: RunConfig,
		seed: int,
		*,
		repositories: Repositories,
		clock: Clock,
		game_version: str,
	) -> tuple[GameSession, list[Event]]:
		engine, events = GameEngine.new_run(data, config, seed)
		now = clock.now()
		info = SessionInfo(run_id=make_run_id(now, seed), started_at=format_timestamp(now))
		session = cls(data, engine, info=info, repositories=repositories, clock=clock, game_version=game_version)
		session._after_step(events)
		return session, events

	@classmethod
	def resume(cls, data: GameData, repositories: Repositories, clock: Clock, game_version: str) -> GameSession | None:
		save = repositories.saves.load()
		if save is None:
			return None
		engine = GameEngine.restore(data, save.run, save.rng_state)
		save.session.sessions += 1
		session = cls(
			data, engine, info=save.session, repositories=repositories, clock=clock, game_version=game_version
		)
		session._merchant_snapshot = (copy.deepcopy(save.run), save.rng_state)
		return session

	# ── play ──────────────────────────────────────────────────────────────────

	def step(self, command: Command) -> StepResult:
		events = self.engine.step(command)
		return StepResult(events, self._after_step(events))

	def _after_step(self, events: list[Event]) -> list[AchievementDef]:
		now = format_timestamp(self._clock.now())
		unlocked = self.profile.observe(events, self.state, now, self.info.run_id)
		profile_changed = bool(unlocked) or any(e["type"] == "monster_killed" for e in events)
		if self.state.phase in {Phase.MERCHANT, Phase.VICTORY}:
			self._merchant_snapshot = (copy.deepcopy(self.state), self.engine.rng_state)
			self._write_save()
		elif self.state.phase is Phase.GAME_OVER and self.finished_record is None:
			self._finish()
			profile_changed = True
		if profile_changed:
			self._repositories.profile.save(self.profile.profile)
		return unlocked

	def save_and_quit(self) -> None:
		"""Persists play time. Mid-battle quits resume from the last merchant (or victory) snapshot."""
		if self.state.phase is not Phase.GAME_OVER:
			self._write_save()

	# ── persistence ───────────────────────────────────────────────────────────

	def _accumulate_play_time(self) -> datetime:
		now = self._clock.now()
		self.info.play_time_seconds += max(0, int((now - self._segment_started).total_seconds()))
		self._segment_started = now
		return now

	def _write_save(self) -> None:
		if self._merchant_snapshot is None:
			return
		now = self._accumulate_play_time()
		run, rng_state = self._merchant_snapshot
		self._repositories.saves.save(
			SaveGame(
				game_version=self._game_version,
				implementation=IMPLEMENTATION,
				saved_at=format_timestamp(now),
				rng_state=rng_state,
				session=self.info,
				run=run,
			)
		)

	def _finish(self) -> None:
		now = format_timestamp(self._accumulate_play_time())
		state = self.state
		record = RunRecord(
			run_id=self.info.run_id,
			name=state.config.name,
			vocation=state.config.vocation_id,
			difficulty=state.config.difficulty_id,
			seed=state.seed,
			implementation=IMPLEMENTATION,
			game_version=self._game_version,
			started_at=self.info.started_at,
			ended_at=now,
			play_time_seconds=self.info.play_time_seconds,
			sessions=self.info.sessions,
			round=state.round,
			level=state.player.level,
			magic_level=state.player.magic_level,
			death_cause=state.death_cause or "",
			won=state.won,
			stats=state.stats,
		)
		self._repositories.history.add(record)
		self._repositories.saves.delete()
		self.profile.record_finished_run(
			HallOfFameEntry(
				run_id=record.run_id,
				name=record.name,
				vocation=record.vocation,
				difficulty=record.difficulty,
				round=record.round,
				level=record.level,
				ended_at=record.ended_at,
				won=record.won,
			)
		)
		self.finished_record = record
