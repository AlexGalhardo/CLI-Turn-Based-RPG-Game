"""Filesystem locations: the shared/ folder and the player's data directory (docs/persistence.md)."""

import os
from pathlib import Path

SHARED_DIR_ENV = "RPG_SHARED_DIR"
DATA_DIR_ENV = "RPG_DATA_DIR"
DEFAULT_DATA_DIR_NAME = ".cli-turn-based-rpg"


def find_shared_dir(start: Path | None = None) -> Path:
	override = os.environ.get(SHARED_DIR_ENV)
	if override:
		return Path(override)
	current = (start or Path(__file__)).resolve()
	for directory in (current, *current.parents):
		candidate = directory / "shared" / "data"
		if candidate.is_dir():
			return directory / "shared"
	raise FileNotFoundError(f"shared/ folder not found; set {SHARED_DIR_ENV}")


def resolve_data_dir(cli_value: str | None = None) -> Path:
	if cli_value:
		return Path(cli_value)
	override = os.environ.get(DATA_DIR_ENV)
	if override:
		return Path(override)
	return Path.home() / DEFAULT_DATA_DIR_NAME
