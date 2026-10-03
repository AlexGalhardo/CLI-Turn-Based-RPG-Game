"""Save file and finished-run record formats, shared by the six implementations (docs/persistence.md)."""

from dataclasses import dataclass, field
from datetime import UTC, datetime

from rpg.application.run_state import RunState
from rpg.application.statistics import RunStatistics
from rpg.domain.json_types import JsonObject, JsonValue, json_bool, json_int, json_obj, json_str

SCHEMA_VERSION = 2
IMPLEMENTATION = "python"


def format_timestamp(moment: datetime) -> str:
	return moment.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_timestamp(text: str) -> datetime:
	return datetime.strptime(text, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=UTC)


def make_run_id(started_at: datetime, seed: int) -> str:
	return f"{started_at.astimezone(UTC).strftime('%Y%m%dT%H%M%SZ')}-{seed}"


class NewerSchemaError(RuntimeError):
	"""The file was written by a newer game version; it is never overwritten."""


def check_schema(data: JsonObject, what: str) -> None:
	version = json_int(data["schemaVersion"])
	if version > SCHEMA_VERSION:
		raise NewerSchemaError(f"{what} uses schema {version}; update the game (supports {SCHEMA_VERSION})")


@dataclass(slots=True)
class SessionInfo:
	run_id: str
	started_at: str
	play_time_seconds: int = 0
	sessions: int = 1

	def to_dict(self) -> JsonObject:
		return {
			"runId": self.run_id,
			"startedAt": self.started_at,
			"playTimeSeconds": self.play_time_seconds,
			"sessions": self.sessions,
		}

	@staticmethod
	def from_dict(raw: JsonValue) -> SessionInfo:
		data = json_obj(raw)
		return SessionInfo(
			run_id=json_str(data["runId"]),
			started_at=json_str(data["startedAt"]),
			play_time_seconds=json_int(data["playTimeSeconds"]),
			sessions=json_int(data["sessions"]),
		)


@dataclass(slots=True)
class SaveGame:
	game_version: str
	implementation: str
	saved_at: str
	rng_state: int
	session: SessionInfo
	run: RunState

	def to_dict(self) -> JsonObject:
		return {
			"schemaVersion": SCHEMA_VERSION,
			"gameVersion": self.game_version,
			"implementation": self.implementation,
			"savedAt": self.saved_at,
			"rngState": self.rng_state,
			"session": self.session.to_dict(),
			"run": self.run.to_dict(),
		}

	@staticmethod
	def from_dict(raw: JsonValue) -> SaveGame:
		data = json_obj(raw)
		check_schema(data, "save.json")
		return SaveGame(
			game_version=json_str(data["gameVersion"]),
			implementation=json_str(data["implementation"]),
			saved_at=json_str(data["savedAt"]),
			rng_state=json_int(data["rngState"]),
			session=SessionInfo.from_dict(data["session"]),
			run=RunState.from_dict(data["run"]),
		)


@dataclass(slots=True)
class RunRecord:
	"""A finished run, written to history/<runId>.json."""

	run_id: str
	name: str
	vocation: str
	difficulty: str
	seed: int
	implementation: str
	game_version: str
	started_at: str
	ended_at: str
	play_time_seconds: int
	sessions: int
	round: int
	level: int
	magic_level: int
	death_cause: str
	won: bool = False
	stats: RunStatistics = field(default_factory=RunStatistics)

	def to_dict(self) -> JsonObject:
		return {
			"schemaVersion": SCHEMA_VERSION,
			"runId": self.run_id,
			"name": self.name,
			"vocation": self.vocation,
			"difficulty": self.difficulty,
			"seed": self.seed,
			"implementation": self.implementation,
			"gameVersion": self.game_version,
			"startedAt": self.started_at,
			"endedAt": self.ended_at,
			"playTimeSeconds": self.play_time_seconds,
			"sessions": self.sessions,
			"round": self.round,
			"level": self.level,
			"magicLevel": self.magic_level,
			"deathCause": self.death_cause,
			"won": self.won,
			"stats": self.stats.to_dict(),
		}

	@staticmethod
	def from_dict(raw: JsonValue) -> RunRecord:
		data = json_obj(raw)
		check_schema(data, "history record")
		return RunRecord(
			run_id=json_str(data["runId"]),
			name=json_str(data["name"]),
			vocation=json_str(data["vocation"]),
			difficulty=json_str(data["difficulty"]),
			seed=json_int(data["seed"]),
			implementation=json_str(data["implementation"]),
			game_version=json_str(data["gameVersion"]),
			started_at=json_str(data["startedAt"]),
			ended_at=json_str(data["endedAt"]),
			play_time_seconds=json_int(data["playTimeSeconds"]),
			sessions=json_int(data["sessions"]),
			round=json_int(data["round"]),
			level=json_int(data["level"]),
			magic_level=json_int(data["magicLevel"]),
			death_cause=json_str(data["deathCause"]),
			won=json_bool(data["won"]),
			stats=RunStatistics.from_dict(data["stats"]),
		)
