"""JSON file repositories under the player's data directory (docs/persistence.md)."""

import json
import os
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path

from rpg.application.profile import Profile
from rpg.application.save_game import RunRecord, SaveGame, check_schema
from rpg.domain.json_types import JsonObject, JsonValue, json_obj, json_str
from rpg.infrastructure.i18n import SUPPORTED_LOCALES


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


class SettingsRepository:
	def __init__(self, data_dir: Path) -> None:
		self._path = data_dir / "settings.json"

	def load(self) -> Settings:
		if not self._path.exists():
			return Settings()
		data = json_obj(read_json(self._path))
		check_schema(data, "settings.json")
		locale = json_str(data["locale"]) if "locale" in data else None
		return Settings(locale if locale in SUPPORTED_LOCALES else None)

	def save(self, settings: Settings) -> None:
		document: JsonObject = {"schemaVersion": 1}
		if settings.locale is not None:
			document["locale"] = settings.locale
		write_json_atomic(self._path, document)


class FileSaveRepository:
	def __init__(self, data_dir: Path) -> None:
		self._path = data_dir / "save.json"

	def load(self) -> SaveGame | None:
		if not self._path.exists():
			return None
		return SaveGame.from_dict(read_json(self._path))

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
		return [RunRecord.from_dict(read_json(path)) for path in sorted(self._dir.glob("*.json"))]


class FileProfileRepository:
	def __init__(self, data_dir: Path) -> None:
		self._path = data_dir / "profile.json"

	def load(self) -> Profile:
		if not self._path.exists():
			return Profile()
		data = json_obj(read_json(self._path))
		check_schema(data, "profile.json")
		return Profile.from_dict(data)

	def save(self, profile: Profile) -> None:
		write_json_atomic(self._path, profile.to_dict())
