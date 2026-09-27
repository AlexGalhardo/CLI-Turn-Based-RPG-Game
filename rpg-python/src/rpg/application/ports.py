"""Ports implemented by the infrastructure layer (Dependency Inversion: the application owns the interfaces)."""

from datetime import datetime
from typing import Protocol

from rpg.application.profile import Profile
from rpg.application.save_game import RunRecord, SaveGame


class Clock(Protocol):
	def now(self) -> datetime: ...


class SaveRepository(Protocol):
	def load(self) -> SaveGame | None: ...

	def save(self, save: SaveGame) -> None: ...

	def delete(self) -> None: ...


class HistoryRepository(Protocol):
	def add(self, record: RunRecord) -> None: ...

	def list(self) -> list[RunRecord]: ...


class ProfileRepository(Protocol):
	def load(self) -> Profile: ...

	def save(self, profile: Profile) -> None: ...
