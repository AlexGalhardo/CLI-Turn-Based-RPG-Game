import json

import pytest

from rpg.application.commands import (
	Attack,
	BuyPotion,
	BuyStockItem,
	Cast,
	Command,
	ContinueRun,
	Defend,
	EndRun,
	Equip,
	NextFight,
	SellItem,
	Unequip,
	UsePotion,
	command_from_dict,
	command_to_dict,
)
from rpg.application.run_state import RunState
from rpg.domain.entities import ActiveStatus, AffixRoll, ItemInstance
from rpg.domain.enums import Slot, Stat
from rpg.domain.json_types import json_bool, json_int, json_list, json_obj, json_str
from tests.conftest import EngineFactory

ALL_COMMANDS: list[Command] = [
	Attack(),
	Cast("brutal_strike"),
	UsePotion("health_potion"),
	Defend(),
	NextFight(),
	BuyPotion("mana_potion", 3),
	SellItem(4),
	Equip(5),
	Unequip(Slot.RING),
	BuyStockItem(1),
	EndRun(),
	ContinueRun(),
]


@pytest.mark.parametrize("command", ALL_COMMANDS)
def test_command_round_trip(command: Command) -> None:
	assert command_from_dict(json.loads(json.dumps(command_to_dict(command)))) == command


def test_unknown_command_type() -> None:
	with pytest.raises(ValueError, match="unknown command"):
		command_from_dict({"type": "dance"})


def test_run_state_round_trip_through_json(new_engine: EngineFactory) -> None:
	engine = new_engine()
	engine.step(NextFight())
	for _ in range(3):
		engine.step(Attack())
	state = engine.state
	state.player.bag.append(ItemInstance(90, "sword", "mythic", 2, (AffixRoll(Stat.DODGE, 3),)))
	state.player.statuses.append(ActiveStatus("burn", 2, 4))
	if state.monster is not None:
		state.monster.statuses.append(ActiveStatus("stun", 1, 0))
	raw = json.loads(json.dumps(state.to_dict()))
	restored = RunState.from_dict(raw)
	assert restored == state
	assert restored.to_dict() == state.to_dict()


@pytest.mark.parametrize(
	("reader", "value"),
	[(json_obj, []), (json_list, {}), (json_int, True), (json_int, "1"), (json_str, 1), (json_bool, 0)],
)
def test_json_readers_reject_wrong_types(reader: object, value: object) -> None:
	assert callable(reader)
	with pytest.raises(TypeError):
		reader(value)
