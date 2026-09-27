"""Generates shared/golden/*.json from the Python reference (docs/cross-language-parity.md §4).

Run only when a rule change is intended: `uv run rpg-golden`.
"""

import json
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from rpg.application.bot import GreedyBot
from rpg.application.commands import (
	Attack,
	BuyPotion,
	BuyStockItem,
	Cast,
	Command,
	Defend,
	Equip,
	NextFight,
	SellItem,
	Unequip,
	UsePotion,
	command_to_dict,
)
from rpg.application.engine import GameEngine
from rpg.application.events import Event
from rpg.application.run_state import RunConfig
from rpg.domain.definitions import GameData
from rpg.domain.enums import Phase, Slot
from rpg.domain.json_types import JsonObject, JsonValue
from rpg.infrastructure.data_loader import load_game_data
from rpg.infrastructure.paths import find_shared_dir

MAX_BOT_STEPS = 20_000

type CommandSource = Callable[[GameEngine, int], Command | None]


@dataclass(frozen=True, slots=True)
class Scenario:
	name: str
	seed: int
	config: RunConfig
	commands: CommandSource


def final_state(engine: GameEngine) -> JsonObject:
	state = engine.state
	player = state.player
	return {
		"phase": state.phase.value,
		"round": state.round,
		"turn": state.turn,
		"level": player.level,
		"xp": player.xp,
		"magicLevel": player.magic_level,
		"hp": player.hp,
		"mp": player.mp,
		"gold": player.gold,
		"rngState": engine.rng_state,
		"nextItemUid": state.next_item_uid,
		"stats": state.stats.to_dict(),
	}


def _events_json(events: list[Event]) -> list[JsonValue]:
	return [dict(e) for e in events]


def record(data: GameData, scenario: Scenario) -> JsonObject:
	engine, first = GameEngine.new_run(data, scenario.config, scenario.seed)
	commands: list[JsonValue] = []
	events: list[JsonValue] = [_events_json(first)]
	for index in range(MAX_BOT_STEPS):
		command = scenario.commands(engine, index)
		if command is None:
			break
		commands.append(command_to_dict(command))
		events.append(_events_json(engine.step(command)))
	else:
		raise RuntimeError(f"scenario {scenario.name} did not finish")
	return {
		"name": scenario.name,
		"seed": scenario.seed,
		"config": scenario.config.to_dict(),
		"commands": commands,
		"events": events,
		"finalState": final_state(engine),
		"finalRun": engine.state.to_dict(),
	}


def scripted(commands: list[Command]) -> CommandSource:
	def source(_: GameEngine, index: int) -> Command | None:
		return commands[index] if index < len(commands) else None

	return source


def bot_until_death(data: GameData) -> CommandSource:
	bot = GreedyBot(data)

	def source(engine: GameEngine, _: int) -> Command | None:
		return None if engine.state.phase is Phase.GAME_OVER else bot.choose(engine.state)

	return source


def scenarios(data: GameData) -> list[Scenario]:
	result = [
		Scenario(
			"merchant-and-errors",
			7,
			RunConfig("Alex", "warrior", "normal"),
			scripted(
				[
					BuyPotion("health_potion", 1),
					BuyPotion("great_health_potion", 1),
					BuyPotion("health_potion", 0),
					BuyPotion("mana_potion", 99),
					BuyStockItem(0),
					SellItem(12345),
					Equip(12345),
					Unequip(Slot.RING),
					Unequip(Slot.WEAPON),
					Equip(1),
					Attack(),
					NextFight(),
					BuyPotion("health_potion", 1),
					Cast("flame_strike"),
					Defend(),
					Cast("brutal_strike"),
					UsePotion("mana_potion"),
					Attack(),
					Attack(),
					Attack(),
				]
			),
		),
		Scenario(
			"mage-spells",
			2026,
			RunConfig("Mia", "mage", "easy"),
			scripted(
				[
					NextFight(),
					Cast("flame_strike"),
					Cast("energy_strike"),
					Cast("intense_healing"),
					UsePotion("health_potion"),
					Cast("eternal_winter"),
					Attack(),
					Attack(),
					Attack(),
				]
			),
		),
	]
	for vocation in ("warrior", "archer", "mage"):
		for difficulty in ("easy", "normal", "hard"):
			result.append(
				Scenario(
					f"bot-full-run-{vocation}-{difficulty}",
					1000 + len(result),
					RunConfig("Bot", vocation, difficulty),
					bot_until_death(data),
				)
			)
	return result


def write_golden(shared_dir: Path) -> list[Path]:
	data = load_game_data(shared_dir)
	written = []
	for scenario in scenarios(data):
		path = shared_dir / "golden" / f"{scenario.name}.json"
		path.write_text(json.dumps(record(data, scenario), ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
		written.append(path)
	return written


def main() -> None:
	for path in write_golden(find_shared_dir()):
		print(f"wrote {path}")
