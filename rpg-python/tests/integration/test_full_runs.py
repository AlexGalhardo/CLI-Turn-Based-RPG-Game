"""Whole runs driven by the bot: the engine must always terminate, be deterministic and survive save/restore."""

import json

import pytest

from rpg.application.bot import GreedyBot
from rpg.application.engine import GameEngine
from rpg.application.events import Event
from rpg.application.run_state import RunConfig, RunState
from rpg.domain.definitions import GameData
from rpg.domain.enums import Phase

MAX_STEPS = 50_000


def play_to_death(engine: GameEngine, bot: GreedyBot) -> list[list[Event]]:
	log: list[list[Event]] = []
	for _ in range(MAX_STEPS):
		if engine.state.phase is Phase.GAME_OVER:
			return log
		events = engine.step(bot.choose(engine.state))
		assert all(e["type"] != "error" for e in events), events
		log.append(events)
	raise AssertionError("run did not finish")


@pytest.mark.parametrize("vocation", ["warrior", "archer", "mage"])
@pytest.mark.parametrize("difficulty", ["easy", "normal", "hard"])
def test_bot_plays_until_death(data: GameData, vocation: str, difficulty: str) -> None:
	engine, _ = GameEngine.new_run(data, RunConfig("Bot", vocation, difficulty), 1234)
	log = play_to_death(engine, GreedyBot(data))
	state = engine.state
	assert state.phase is Phase.GAME_OVER
	assert state.round >= 1
	assert state.death_cause
	assert log[-1][-1]["type"] == "player_died"
	assert sum(state.stats.kills.values()) == state.round - 1
	assert state.stats.damage_dealt > 0


def test_same_seed_same_events(data: GameData) -> None:
	logs = []
	for _ in range(2):
		engine, first = GameEngine.new_run(data, RunConfig("Bot", "archer", "normal"), 777)
		logs.append([first, *play_to_death(engine, GreedyBot(data))])
	assert logs[0] == logs[1]


def test_different_seeds_diverge(data: GameData) -> None:
	finals = set()
	for seed in (1, 2, 3):
		engine, _ = GameEngine.new_run(data, RunConfig("Bot", "warrior", "normal"), seed)
		play_to_death(engine, GreedyBot(data))
		finals.add(json.dumps(engine.state.stats.to_dict(), sort_keys=True))
	assert len(finals) > 1


def test_restore_mid_run_continues_identically(data: GameData) -> None:
	bot = GreedyBot(data)
	reference, _ = GameEngine.new_run(data, RunConfig("Bot", "mage", "hard"), 99)
	reference_log = play_to_death(reference, bot)

	engine, _ = GameEngine.new_run(data, RunConfig("Bot", "mage", "hard"), 99)
	log: list[list[Event]] = []
	while engine.state.phase is not Phase.GAME_OVER:
		if engine.state.phase is Phase.MERCHANT and engine.state.round % 3 == 0:
			snapshot = json.loads(json.dumps(engine.state.to_dict()))
			engine = GameEngine.restore(data, RunState.from_dict(snapshot), engine.rng_state)
		log.append(engine.step(bot.choose(engine.state)))
	assert log == reference_log


def test_new_run_rejects_invalid_config(data: GameData) -> None:
	with pytest.raises(ValueError, match="invalid run config"):
		GameEngine.new_run(data, RunConfig("X", "knight", "normal"), 1)
	with pytest.raises(ValueError, match="invalid run config"):
		GameEngine.new_run(data, RunConfig("X", "mage", "nightmare"), 1)
