"""JSON file repositories under the player's data directory (docs/persistence.md)."""

import json
import os
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path

from rpg.application.profile import Profile
from rpg.application.save_game import SCHEMA_VERSION, RunRecord, SaveGame, check_schema
from rpg.domain.json_types import JsonObject, JsonValue, json_bool, json_int, json_obj, json_str
from rpg.infrastructure.i18n import SUPPORTED_LOCALES
from rpg.infrastructure.migrations import migrate_history, migrate_profile, migrate_save, migrate_settings

BATTLE_SPEEDS = (1, 2)


def write_json_atomic(path: Path, document: JsonObject) -> None:
	"""Write to a temp file then rename, so a crash never leaves a half-written save."""
	path.parent.mkdir(parents=True, exist_ok=True)
	temporary = path.with_name(path.name + ".tmp")
	temporary.write_text(json.dumps(document, indent="\t", ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
	os.replace(temporary, path)


def read_json(path: Path) -> JsonValue:
	result: JsonValue = json.loads(path.read_text(encoding="utf-8"))
	return result


class SystemClock:
	def now(self) -> datetime:
		return datetime.now(UTC)


@dataclass(frozen=True, slots=True)
class Settings:
	locale: str | None = None
	auto_equip: bool = False
	battle_speed: int = 1


class SettingsRepository:
	def __init__(self, data_dir: Path) -> None:
		self._path = data_dir / "settings.json"

	def load(self) -> Settings:
		if not self._path.exists():
			return Settings()
		data = json_obj(read_json(self._path))
		check_schema(data, "settings.json")
		data = migrate_settings(data)
		locale = json_str(data["locale"]) if "locale" in data else None
		speed = json_int(data["battleSpeed"])
		return Settings(
			locale=locale if locale in SUPPORTED_LOCALES else None,
			auto_equip=json_bool(data["autoEquip"]),
			battle_speed=speed if speed in BATTLE_SPEEDS else BATTLE_SPEEDS[0],
		)

	def save(self, settings: Settings) -> None:
		document: JsonObject = {"schemaVersion": SCHEMA_VERSION}
		if settings.locale is not None:
			document["locale"] = settings.locale
		document["autoEquip"] = settings.auto_equip
		document["battleSpeed"] = settings.battle_speed
		write_json_atomic(self._path, document)


class FileSaveRepository:
	def __init__(self, data_dir: Path) -> None:
		self._path = data_dir / "save.json"

	def load(self) -> SaveGame | None:
		if not self._path.exists():
			return None
		document = json_obj(read_json(self._path))
		check_schema(document, "save.json")
		return SaveGame.from_dict(migrate_save(document))

	def save(self, save: SaveGame) -> None:
		write_json_atomic(self._path, save.to_dict())

	def delete(self) -> None:
		self._path.unlink(missing_ok=True)


class FileHistoryRepository:
	def __init__(self, data_dir: Path) -> None:
		self._dir = data_dir / "history"

	def add(self, record: RunRecord) -> None:
		write_json_atomic(self._dir / f"{record.run_id}.json", record.to_dict())

	def list(self) -> list[RunRecord]:
		if not self._dir.exists():
			return []
		records = []
		for path in sorted(self._dir.glob("*.json")):
			document = json_obj(read_json(path))
			check_schema(document, "history record")
			records.append(RunRecord.from_dict(migrate_history(document)))
		return records


class FileProfileRepository:
	def __init__(self, data_dir: Path) -> None:
		self._path = data_dir / "profile.json"

	def load(self) -> Profile:
		if not self._path.exists():
			return Profile()
		data = json_obj(read_json(self._path))
		check_schema(data, "profile.json")
		return Profile.from_dict(migrate_profile(data))

	def save(self, profile: Profile) -> None:
		write_json_atomic(self._path, profile.to_dict())
