import json
from datetime import UTC, datetime, timedelta
from pathlib import Path

import pytest

from rpg.application.bot import GreedyBot
from rpg.application.commands import Attack, BuyPotion, NextFight
from rpg.application.game_session import GameSession, Repositories
from rpg.application.profile import HallOfFameEntry, Profile, ProfileService
from rpg.application.run_state import RunConfig
from rpg.application.save_game import NewerSchemaError, make_run_id, parse_timestamp
from rpg.domain.definitions import GameData
from rpg.domain.enums import Phase
from rpg.infrastructure.repositories import (
	FileHistoryRepository,
	FileProfileRepository,
	FileSaveRepository,
	Settings,
	SettingsRepository,
	SystemClock,
)


class FakeClock:
	def __init__(self) -> None:
		self.current = datetime(2026, 9, 27, 12, 0, 0, tzinfo=UTC)

	def now(self) -> datetime:
		self.current += timedelta(seconds=10)
		return self.current


def repositories(directory: Path) -> Repositories:
	return Repositories(
		FileSaveRepository(directory), FileHistoryRepository(directory), FileProfileRepository(directory)
	)


def test_new_session_autosaves_at_merchant(data: GameData, tmp_path: Path) -> None:
	session, events = GameSession.start(
		data,
		RunConfig("Alex", "archer", "normal"),
		5,
		repositories=repositories(tmp_path),
		clock=FakeClock(),
		game_version="9.9.9",
	)
	assert events[0]["type"] == "run_started"
	save = json.loads((tmp_path / "save.json").read_text(encoding="utf-8"))
	assert save["schemaVersion"] == 2
	assert save["run"]["config"]["autoEquip"] is False
	assert save["run"]["won"] is False
	assert save["implementation"] == "python"
	assert save["gameVersion"] == "9.9.9"
	assert save["session"]["runId"] == session.info.run_id
	assert save["run"]["phase"] == "merchant"
	session.step(BuyPotion("health_potion", 1))
	save = json.loads((tmp_path / "save.json").read_text(encoding="utf-8"))
	assert save["run"]["player"]["potions"]["health_potion"] == 6


def test_quit_mid_battle_resumes_from_last_merchant(data: GameData, tmp_path: Path) -> None:
	repos = repositories(tmp_path)
	clock = FakeClock()
	session, _ = GameSession.start(
		data, RunConfig("Alex", "warrior", "normal"), 5, repositories=repos, clock=clock, game_version="1"
	)
	session.step(NextFight())
	monster = session.state.monster
	assert monster is not None
	monster.hp = monster.max_hp = 1_000_000
	session.step(Attack())
	assert session.state.phase is Phase.BATTLE
	session.save_and_quit()

	resumed = GameSession.resume(data, repos, clock, "1")
	assert resumed is not None
	assert resumed.state.phase is Phase.MERCHANT
	assert resumed.state.round == 0
	assert resumed.info.sessions == 2
	assert resumed.info.play_time_seconds > 0
	assert resumed.info.run_id == session.info.run_id


def test_resume_without_save(data: GameData, tmp_path: Path) -> None:
	assert GameSession.resume(data, repositories(tmp_path), FakeClock(), "1") is None


def test_death_writes_history_profile_and_deletes_save(data: GameData, tmp_path: Path) -> None:
	repos = repositories(tmp_path)
	session, _ = GameSession.start(
		data, RunConfig("Bot", "mage", "hard"), 3, repositories=repos, clock=FakeClock(), game_version="1"
	)
	bot = GreedyBot(data)
	unlocked: list[str] = []
	while session.state.phase is not Phase.GAME_OVER:
		unlocked.extend(a.id for a in session.step(bot.choose(session.state)).achievements)
	assert not (tmp_path / "save.json").exists()
	records = repos.history.list()
	assert len(records) == 1
	record = records[0]
	assert record.round == session.state.round
	assert record.death_cause == session.state.death_cause
	assert parse_timestamp(record.ended_at) > parse_timestamp(record.started_at)
	profile = repos.profile.load()
	assert profile.hall_of_fame[0].run_id == record.run_id
	assert "first_blood" in unlocked
	assert "first_blood" in profile.achievements
	assert sum(entry.kills for entry in profile.bestiary.values()) == session.state.round - 1
	session.save_and_quit()
	assert not (tmp_path / "save.json").exists()


def test_newer_schema_is_refused(data: GameData, tmp_path: Path) -> None:
	(tmp_path / "save.json").write_text(json.dumps({"schemaVersion": 99}), encoding="utf-8")
	with pytest.raises(NewerSchemaError):
		FileSaveRepository(tmp_path).load()
	(tmp_path / "profile.json").write_text(json.dumps({"schemaVersion": 99}), encoding="utf-8")
	with pytest.raises(NewerSchemaError):
		FileProfileRepository(tmp_path).load()


def test_settings_round_trip(tmp_path: Path) -> None:
	repo = SettingsRepository(tmp_path)
	assert repo.load() == Settings()
	repo.save(Settings("pt-BR", auto_equip=True, battle_speed=2))
	assert repo.load() == Settings("pt-BR", auto_equip=True, battle_speed=2)
	saved = json.loads((tmp_path / "settings.json").read_text(encoding="utf-8"))
	assert saved == {"schemaVersion": 2, "locale": "pt-BR", "autoEquip": True, "battleSpeed": 2}
	repo.save(Settings())
	assert repo.load() == Settings()
	(tmp_path / "settings.json").write_text(json.dumps({"schemaVersion": 1, "locale": "fr"}), encoding="utf-8")
	assert repo.load() == Settings()
	(tmp_path / "settings.json").write_text(json.dumps({"schemaVersion": 1, "locale": "en"}), encoding="utf-8")
	assert repo.load() == Settings("en", auto_equip=False, battle_speed=1)
	document = {"schemaVersion": 2, "autoEquip": True, "battleSpeed": 7}
	(tmp_path / "settings.json").write_text(json.dumps(document), encoding="utf-8")
	assert repo.load() == Settings(None, auto_equip=True, battle_speed=1)


def test_profile_round_trip_and_hall_of_fame_order(data: GameData, tmp_path: Path) -> None:
	service = ProfileService(data, Profile())
	for index in range(12):
		service.record_finished_run(
			HallOfFameEntry(
				f"run{index}", "A", "mage", "normal", index % 5, index, f"2026-01-{index + 1:02d}T00:00:00Z"
			)
		)
	hall = service.profile.hall_of_fame
	assert len(hall) == 10
	assert [e.round for e in hall[:3]] == [4, 4, 3]
	assert hall[0].level > hall[1].level
	service.record_finished_run(HallOfFameEntry("winner", "W", "mage", "easy", 1, 1, "2026-02-01T00:00:00Z", True))
	assert service.profile.hall_of_fame[0].run_id == "winner"
	repo = FileProfileRepository(tmp_path)
	repo.save(service.profile)
	assert repo.load() == service.profile
	assert service.revealed("rat") is False


def test_run_id_and_clock() -> None:
	moment = datetime(2026, 1, 2, 3, 4, 5, tzinfo=UTC)
	assert make_run_id(moment, 42) == "20260102T030405Z-42"
	assert SystemClock().now().tzinfo is UTC
