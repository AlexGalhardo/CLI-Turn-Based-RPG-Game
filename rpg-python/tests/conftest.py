from collections.abc import Callable
from dataclasses import replace
from pathlib import Path

import pytest

from rpg.application.engine import GameEngine
from rpg.application.run_state import RunConfig
from rpg.domain.definitions import GameData, ItemDef
from rpg.domain.enums import Slot, Stat
from rpg.infrastructure.data_loader import load_game_data
from rpg.infrastructure.paths import find_shared_dir

SHARED_DIR = find_shared_dir(Path(__file__))
_GAME_DATA = load_game_data(SHARED_DIR)


@pytest.fixture(autouse=True)
def _isolated_data_dir(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
	"""Tests must never touch the real save directory."""
	monkeypatch.setenv("RPG_DATA_DIR", str(tmp_path / "rpg-data"))
	monkeypatch.setenv("RPG_NO_ANIM", "1")


@pytest.fixture
def shared_dir() -> Path:
	return SHARED_DIR


@pytest.fixture
def data() -> GameData:
	return _GAME_DATA


type EngineFactory = Callable[..., GameEngine]


@pytest.fixture
def new_engine(data: GameData) -> EngineFactory:
	def factory(
		vocation: str = "warrior", difficulty: str = "normal", seed: int = 42, game_data: GameData | None = None
	) -> GameEngine:
		engine, _ = GameEngine.new_run(game_data or data, RunConfig("Tester", vocation, difficulty), seed)
		return engine

	return factory


def with_test_items(data: GameData) -> GameData:
	"""Adds deterministic test items (one per slot with every stat) without touching the shared files."""
	extra = (
		ItemDef("test_helmet", "Test Helmet", Slot.HELMET, "helmet", 0, None, {Stat.ARMOR: 10, Stat.MAX_HP: 50}, 100),
		ItemDef("test_ring", "Test Ring", Slot.RING, "ring", 0, None, {Stat.CRIT_CHANCE: 80, Stat.DODGE: 90}, 100),
		ItemDef("test_axe", "Test Axe", Slot.WEAPON, "axe", 0, None, {Stat.ATTACK: 20}, 100),
		ItemDef("test_rod", "Test Rod", Slot.WEAPON, "rod", 0, None, {Stat.ATTACK: 1}, 100),
	)
	return replace(data, items=data.items + extra)
