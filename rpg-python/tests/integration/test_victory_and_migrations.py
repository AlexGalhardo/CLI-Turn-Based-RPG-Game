"""Victory phase through the session (save, resume, history, Hall of Fame) and schema 1 → 2 migrations."""

import copy
import json
from pathlib import Path

from rpg.application.commands import Attack, ContinueRun, EndRun, NextFight
from rpg.application.game_session import GameSession, Repositories
from rpg.application.run_state import RunConfig
from rpg.domain.definitions import GameData
from rpg.domain.enums import Phase
from rpg.domain.json_types import JsonObject, json_obj
from rpg.infrastructure.repositories import FileHistoryRepository, FileProfileRepository, FileSaveRepository
from tests.integration.test_persistence import FakeClock, repositories

MAX_SWINGS = 200


def _win_final_fight(data: GameData, tmp_path: Path) -> tuple[GameSession, Repositories, FakeClock]:
	repos = repositories(tmp_path)
	clock = FakeClock()
	session, _ = GameSession.start(
		data, RunConfig("Vic", "warrior", "easy", True), 8, repositories=repos, clock=clock, game_version="1"
	)
	session.state.round = data.balance.final_round - 1
	session.step(NextFight())
	monster = session.state.monster
	assert monster is not None
	assert monster.creature_id == "ferumbras"
	assert monster.enemy_class == "boss"
	for _ in range(MAX_SWINGS):
		if session.state.phase is not Phase.BATTLE:
			break
		session.state.player.hp = 1_000_000
		monster.hp = 1
		session.step(Attack())
	assert session.state.phase is Phase.VICTORY
	return session, repos, clock


def test_victory_is_saved_resumed_and_ended_as_won(data: GameData, tmp_path: Path) -> None:
	_, repos, clock = _win_final_fight(data, tmp_path)
	save = json.loads((tmp_path / "save.json").read_text(encoding="utf-8"))
	assert save["run"]["phase"] == "victory"
	assert save["run"]["won"] is True

	resumed = GameSession.resume(data, repos, clock, "1")
	assert resumed is not None
	assert resumed.state.phase is Phase.VICTORY
	result = resumed.step(EndRun())
	assert result.events == [{"type": "run_ended", "won": True}]
	assert "conqueror" in repos.profile.load().achievements
	assert not (tmp_path / "save.json").exists()
	record = repos.history.list()[0]
	assert record.won is True
	assert record.death_cause == ""
	assert repos.profile.load().hall_of_fame[0].won is True


def test_continue_after_victory_keeps_the_run_won(data: GameData, tmp_path: Path) -> None:
	session, _, _ = _win_final_fight(data, tmp_path)
	events = session.step(ContinueRun()).events
	assert events == [{"type": "merchant_entered", "round": data.balance.final_round}]
	assert session.state.phase is Phase.MERCHANT
	assert session.state.won is True
	session.step(NextFight())
	assert session.state.round == data.balance.final_round + 1


def _v1(document: JsonObject) -> JsonObject:
	"""Turns a current save into what version 1 wrote: no M8 fields and the old `epic` rarity."""
	old = copy.deepcopy(document)
	old["schemaVersion"] = 1
	run = json_obj(old["run"])
	del json_obj(run["config"])["autoEquip"]
	del run["won"]
	player = json_obj(run["player"])
	weapon = json_obj(json_obj(player["equipment"])["weapon"])
	weapon["rarity"] = "epic"
	stats = json_obj(run["stats"])
	for key in ("itemsAutoEquipped", "elitesKilled", "potionsDropped"):
		del stats[key]
	stats["itemsDropped"] = {"epic": 2, "legendary": 1}
	stats["droppedItems"] = [{"itemId": "sword", "rarity": "epic", "round": 3}]
	return old


def test_v1_save_is_migrated(data: GameData, tmp_path: Path) -> None:
	GameSession.start(
		data,
		RunConfig("Old", "warrior", "normal"),
		4,
		repositories=repositories(tmp_path),
		clock=FakeClock(),
		game_version="1",
	)
	current = json_obj(json.loads((tmp_path / "save.json").read_text(encoding="utf-8")))
	(tmp_path / "save.json").write_text(json.dumps(_v1(current)), encoding="utf-8")

	loaded = FileSaveRepository(tmp_path).load()
	assert loaded is not None
	run = loaded.run
	assert run.config.auto_equip is False
	assert run.won is False
	assert run.player.equipment[next(iter(run.player.equipment))].rarity == "legendary"
	assert run.stats.items_dropped == {"legendary": 3}
	assert run.stats.dropped_items[0].rarity == "legendary"
	assert run.stats.elites_killed == 0


def test_v1_monster_gets_its_class_from_is_boss(data: GameData, tmp_path: Path) -> None:
	session, _ = GameSession.start(
		data,
		RunConfig("Old", "mage", "normal"),
		4,
		repositories=repositories(tmp_path),
		clock=FakeClock(),
		game_version="1",
	)
	session.state.round = 9
	session.step(NextFight())
	document = json_obj(json.loads((tmp_path / "save.json").read_text(encoding="utf-8")))
	run = json_obj(document["run"])
	run["monster"] = session.state.to_dict()["monster"]
	old = _v1(document)
	del json_obj(json_obj(old["run"])["monster"])["enemyClass"]
	(tmp_path / "save.json").write_text(json.dumps(old), encoding="utf-8")
	loaded = FileSaveRepository(tmp_path).load()
	assert loaded is not None
	assert loaded.run.monster is not None
	assert loaded.run.monster.enemy_class == "boss"


def test_v1_history_and_profile_are_migrated(data: GameData, tmp_path: Path) -> None:
	session, _, _ = _win_final_fight(data, tmp_path / "won")
	session.step(EndRun())
	record_path = next((tmp_path / "won" / "history").glob("*.json"))
	record = json_obj(json.loads(record_path.read_text(encoding="utf-8")))
	record["schemaVersion"] = 1
	del record["won"]
	stats = json_obj(record["stats"])
	for key in ("itemsAutoEquipped", "elitesKilled", "potionsDropped"):
		del stats[key]
	history_dir = tmp_path / "old" / "history"
	history_dir.mkdir(parents=True)
	(history_dir / record_path.name).write_text(json.dumps(record), encoding="utf-8")
	assert FileHistoryRepository(tmp_path / "old").list()[0].won is False

	profile = json_obj(json.loads((tmp_path / "won" / "profile.json").read_text(encoding="utf-8")))
	profile["schemaVersion"] = 1
	for entry in profile["hallOfFame"] if isinstance(profile["hallOfFame"], list) else []:
		del json_obj(entry)["won"]
	(tmp_path / "old" / "profile.json").write_text(json.dumps(profile), encoding="utf-8")
	assert FileProfileRepository(tmp_path / "old").load().hall_of_fame[0].won is False
