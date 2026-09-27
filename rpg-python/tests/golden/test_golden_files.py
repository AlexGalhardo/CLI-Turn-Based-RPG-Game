"""Replays every shared/golden scenario; the TypeScript and Go suites run the same files."""

import json
from pathlib import Path

import pytest

from rpg.application.commands import command_from_dict
from rpg.application.engine import GameEngine
from rpg.application.run_state import RunConfig, RunState
from rpg.domain.definitions import GameData
from rpg.domain.json_types import JsonObject, json_int, json_list, json_obj
from rpg.infrastructure.paths import find_shared_dir
from rpg.tools.golden import final_state, record, scenarios

GOLDEN_DIR = find_shared_dir(Path(__file__)) / "golden"
SCENARIO_FILES = sorted(p for p in GOLDEN_DIR.glob("*.json") if p.name != "prng.json")


def _load(path: Path) -> JsonObject:
	return json_obj(json.loads(path.read_text(encoding="utf-8")))


def test_golden_files_exist() -> None:
	assert len(SCENARIO_FILES) >= 11


@pytest.mark.parametrize("path", SCENARIO_FILES, ids=lambda p: p.stem)
def test_replay_matches_golden(data: GameData, path: Path) -> None:
	golden = _load(path)
	engine, first = GameEngine.new_run(data, RunConfig.from_dict(golden["config"]), json_int(golden["seed"]))
	expected_events = json_list(golden["events"])
	assert first == expected_events[0]
	for index, raw_command in enumerate(json_list(golden["commands"]), start=1):
		assert engine.step(command_from_dict(raw_command)) == expected_events[index], f"command #{index}"
	assert final_state(engine) == golden["finalState"]
	assert json.loads(json.dumps(engine.state.to_dict())) == golden["finalRun"]
	assert RunState.from_dict(golden["finalRun"]).to_dict() == golden["finalRun"]


def test_golden_files_are_up_to_date(data: GameData) -> None:
	"""Fails when rules changed without regenerating the golden files (`uv run rpg-golden`)."""
	for scenario in scenarios(data):
		recorded = json.loads(json.dumps(record(data, scenario)))
		assert recorded == _load(GOLDEN_DIR / f"{scenario.name}.json"), scenario.name
