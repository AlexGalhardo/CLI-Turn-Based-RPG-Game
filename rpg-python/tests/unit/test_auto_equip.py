"""Auto-equip with auto-sell (docs/game-design.md §8.1)."""

from dataclasses import replace

from rpg.application.auto_equip import auto_equip
from rpg.application.commands import Attack, BuyStockItem, NextFight
from rpg.application.engine import GameEngine
from rpg.application.run_state import RunConfig
from rpg.domain.character import item_score, item_value
from rpg.domain.definitions import GameData
from rpg.domain.entities import ItemInstance
from rpg.domain.enums import Phase, Slot
from tests.conftest import calm, with_enemy_class, with_test_items


def _engine(data: GameData, auto: bool = True) -> GameEngine:
	engine, _ = GameEngine.new_run(data, RunConfig("Auto", "warrior", "normal", auto), 42)
	return engine


def test_better_item_is_equipped_and_the_old_one_sold(data: GameData) -> None:
	game_data = with_test_items(data)
	engine = _engine(game_data)
	state = engine.state
	starter = state.player.equipment[Slot.WEAPON]
	axe = ItemInstance(50, "test_axe", "common", 0)
	state.player.bag.append(axe)
	gold = state.player.gold
	rng_state = engine.rng_state
	events = auto_equip(state, game_data)
	assert events == [
		{
			"type": "item_auto_equipped",
			"uid": 50,
			"itemId": "test_axe",
			"slot": "weapon",
			"score": item_score(axe, game_data),
		},
		{"type": "item_auto_sold", "uid": starter.uid, "itemId": "sword", "gold": item_value(starter, game_data)},
	]
	assert state.player.equipment[Slot.WEAPON] == axe
	assert state.player.gold == gold + item_value(starter, game_data)
	assert state.player.bag == []
	assert engine.rng_state == rng_state


def test_empty_slots_are_filled_without_selling(data: GameData) -> None:
	game_data = with_test_items(data)
	state = _engine(game_data).state
	state.player.bag.append(ItemInstance(60, "test_helmet", "common", 0))
	events = auto_equip(state, game_data)
	assert [e["type"] for e in events] == ["item_auto_equipped"]
	assert state.player.equipment[Slot.HELMET].uid == 60


def test_ties_go_to_the_lowest_uid_and_worse_items_stay(data: GameData) -> None:
	game_data = with_test_items(data)
	state = _engine(game_data).state
	state.player.bag.extend(
		[
			ItemInstance(72, "test_helmet", "common", 0),
			ItemInstance(71, "test_helmet", "common", 0),
			ItemInstance(73, "test_rod", "common", 0),
		]
	)
	auto_equip(state, game_data)
	assert state.player.equipment[Slot.HELMET].uid == 71
	assert [item.uid for item in state.player.bag] == [72, 73]
	assert auto_equip(state, game_data) == []


def test_items_above_the_player_level_are_skipped(data: GameData) -> None:
	game_data = with_test_items(data)
	state = _engine(game_data).state
	state.player.bag.append(ItemInstance(80, "test_axe", "mythic", 9))
	assert auto_equip(state, game_data) == []
	state.player.level = 1 + 9 * game_data.balance.item_level_per_tier
	assert auto_equip(state, game_data)[0]["uid"] == 80


def test_victory_triggers_auto_equip_only_when_enabled(data: GameData) -> None:
	game_data = with_enemy_class(calm(with_test_items(data)), "normal", drop_chance_pct=100)
	weak_sword = replace(game_data.item("sword"), stats={})
	game_data = replace(game_data, items=tuple(weak_sword if i.id == "sword" else i for i in game_data.items))
	for enabled in (True, False):
		engine = _engine(game_data, enabled)
		engine.step(NextFight())
		monster = engine.state.monster
		assert monster is not None
		monster.hp = 1
		events = engine.step(Attack())
		assert engine.state.phase is Phase.MERCHANT
		assert ("item_auto_equipped" in [e["type"] for e in events]) is enabled
		assert engine.state.stats.items_auto_equipped == (1 if enabled else 0)


def test_buying_a_stock_item_triggers_auto_equip(data: GameData) -> None:
	game_data = with_test_items(data)
	engine = _engine(game_data)
	state = engine.state
	axe = ItemInstance(90, "test_axe", "common", 0)
	state.merchant_stock = [axe]
	state.player.gold = 10_000
	events = engine.step(BuyStockItem(0))
	assert [e["type"] for e in events] == ["item_bought", "item_auto_equipped", "item_auto_sold"]
	assert state.player.equipment[Slot.WEAPON] == axe
	manual = _engine(game_data, auto=False)
	manual.state.merchant_stock = [axe]
	manual.state.player.gold = 10_000
	assert [e["type"] for e in manual.step(BuyStockItem(0))] == ["item_bought"]
