from dataclasses import replace

import pytest

from rpg.application.commands import BuyPotion, BuyStockItem, Equip, NextFight, SellItem, Unequip
from rpg.application.loot import can_use, generate_item, roll_rarity
from rpg.application.merchant import available_potions, stock_price
from rpg.domain.character import build_sheet, item_score, item_stats, item_value, required_level
from rpg.domain.definitions import AffixDef, GameData
from rpg.domain.entities import AffixRoll, ItemInstance
from rpg.domain.enums import Slot, Stat
from rpg.domain.rng import Rng
from tests.conftest import EngineFactory, with_test_items


def _loot_data(data: GameData) -> GameData:
	affixes = (
		AffixDef("of_power", Stat.ATTACK, 1, 3, 2, (Slot.WEAPON,)),
		AffixDef("of_the_bear", Stat.MAX_HP, 5, 10, 5, (Slot.WEAPON, Slot.HELMET)),
		AffixDef("of_speed", Stat.DODGE, 1, 2, 0, (Slot.WEAPON, Slot.RING)),
		AffixDef("of_speed_2", Stat.DODGE, 1, 2, 0, (Slot.WEAPON,)),
	)
	return replace(with_test_items(data), affixes=affixes)


def test_buy_potion_rules(new_engine: EngineFactory) -> None:
	engine = new_engine()
	player = engine.state.player
	gold = player.gold
	assert engine.step(BuyPotion("health_potion", 2)) == [
		{"type": "potion_bought", "potionId": "health_potion", "quantity": 2, "gold": 100}
	]
	assert player.gold == gold - 100
	assert engine.step(BuyPotion("health_potion", 1)) == [{"type": "error", "code": "not_enough_gold"}]
	assert engine.step(BuyPotion("health_potion", 0)) == [{"type": "error", "code": "invalid_quantity"}]
	assert engine.step(BuyPotion("great_health_potion", 1)) == [{"type": "error", "code": "potion_locked"}]
	assert engine.step(BuyPotion("elixir", 1)) == [{"type": "error", "code": "unknown_potion"}]
	assert engine.state.stats.potions_bought["health_potion"] == 2
	assert engine.state.stats.gold_spent == 100


def test_available_potions_unlock_by_round(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine()
	assert available_potions(engine.state, data) == ["health_potion", "mana_potion"]
	engine.state.round = 80
	assert len(available_potions(engine.state, data)) == len(data.potions)


def test_equip_swap_sell_and_unequip(new_engine: EngineFactory, data: GameData) -> None:
	game_data = with_test_items(data)
	engine = new_engine(game_data=game_data)
	player = engine.state.player
	axe = ItemInstance(50, "test_axe", "common", 0)
	rod = ItemInstance(51, "test_rod", "rare", 0)
	player.bag.extend([axe, rod])
	starter_uid = player.equipment[Slot.WEAPON].uid

	assert engine.step(Equip(51)) == [{"type": "error", "code": "cannot_equip"}]
	events = engine.step(Equip(50))
	assert events == [
		{"type": "item_unequipped", "uid": starter_uid, "itemId": "sword", "slot": "weapon"},
		{"type": "item_equipped", "uid": 50, "itemId": "test_axe", "slot": "weapon"},
	]
	assert build_sheet(player, game_data).melee_min == 8 + 20

	gold = player.gold
	assert engine.step(SellItem(51)) == [
		{"type": "item_sold", "uid": 51, "itemId": "test_rod", "gold": item_value(rod, game_data)}
	]
	assert player.gold == gold + 250
	assert engine.step(SellItem(51)) == [{"type": "error", "code": "invalid_item"}]
	assert engine.step(Unequip(Slot.WEAPON)) == [
		{"type": "item_unequipped", "uid": 50, "itemId": "test_axe", "slot": "weapon"}
	]
	assert engine.step(Unequip(Slot.WEAPON)) == [{"type": "error", "code": "invalid_item"}]
	assert engine.step(Equip(999)) == [{"type": "error", "code": "invalid_item"}]


def test_unequip_with_full_bag_and_hp_clamp(new_engine: EngineFactory, data: GameData) -> None:
	game_data = with_test_items(data)
	engine = new_engine(game_data=game_data)
	player = engine.state.player
	player.equipment[Slot.HELMET] = ItemInstance(70, "test_helmet", "common", 0)
	player.hp = build_sheet(player, game_data).max_hp
	player.bag.extend(ItemInstance(100 + i, "test_ring", "common", 0) for i in range(game_data.balance.bag_capacity))
	assert engine.step(Unequip(Slot.HELMET)) == [{"type": "error", "code": "bag_full"}]
	player.bag.pop()
	engine.step(Unequip(Slot.HELMET))
	assert player.hp == build_sheet(player, game_data).max_hp


def test_merchant_stock_purchase(new_engine: EngineFactory, data: GameData) -> None:
	game_data = _loot_data(data)
	engine = new_engine(game_data=game_data)
	state = engine.state
	assert len(state.merchant_stock) == game_data.balance.merchant_stock_size
	item = state.merchant_stock[0]
	price = stock_price(item, game_data)
	state.player.gold = price
	assert engine.step(BuyStockItem(0)) == [
		{"type": "item_bought", "uid": item.uid, "itemId": item.item_id, "gold": price}
	]
	assert item in state.player.bag
	assert engine.step(BuyStockItem(0)) == [{"type": "error", "code": "not_enough_gold"}]
	assert engine.step(BuyStockItem(9)) == [{"type": "error", "code": "invalid_item"}]
	state.player.bag.extend(ItemInstance(200 + i, "test_ring", "common", 0) for i in range(30))
	assert engine.step(BuyStockItem(0)) == [{"type": "error", "code": "bag_full"}]
	engine.step(NextFight())
	assert state.merchant_stock == []


def test_generate_item_is_deterministic_and_unique_affixes(data: GameData) -> None:
	game_data = _loot_data(data)
	vocation = game_data.vocation("warrior")
	weights = game_data.balance.enemy_class("boss").rarity_weights
	first = [generate_item(game_data, Rng(5), vocation=vocation, tier=0, weights=weights, uid=i) for i in range(20)]
	second = [generate_item(game_data, Rng(5), vocation=vocation, tier=0, weights=weights, uid=i) for i in range(20)]
	assert first == second
	rng = Rng(11)
	for uid in range(200):
		item = generate_item(game_data, rng, vocation=vocation, tier=1, weights=weights, uid=uid)
		assert item is not None
		assert item.rarity in {"legendary", "mythic"}
		stats = [affix.stat for affix in item.affixes]
		assert len(stats) == len(set(stats))
		assert can_use(game_data.item(item.item_id), vocation)


def test_generate_item_without_candidates_consumes_nothing(data: GameData) -> None:
	rng = Rng(3)
	empty = replace(data, items=())
	weights = data.balance.enemy_class("normal").rarity_weights
	assert generate_item(empty, rng, vocation=data.vocation("mage"), tier=9, weights=weights, uid=1) is None
	assert rng.state == 3


def test_roll_rarity_skips_zero_weights_and_single_options(data: GameData) -> None:
	rng = Rng(1)
	assert roll_rarity(data, rng, {"rare": 5}).id == "rare"
	assert roll_rarity(data, rng, {"common": 0, "mythic": 3}).id == "mythic"
	assert rng.state == 1
	rolled = {roll_rarity(data, rng, {"common": 1, "legendary": 1}).id for _ in range(40)}
	assert rolled == {"common", "legendary"}
	assert rng.state != 1
	with pytest.raises(ValueError, match="positive"):
		roll_rarity(data, rng, {"common": 0})


@pytest.mark.parametrize(
	("rarity", "attack", "affixes"), [("common", 20, 0), ("rare", 30, 1), ("legendary", 40, 2), ("mythic", 60, 2)]
)
def test_rarities_scale_base_stats_and_affix_counts(data: GameData, rarity: str, attack: int, affixes: int) -> None:
	game_data = with_test_items(data)
	assert item_stats(ItemInstance(1, "test_axe", rarity, 0), game_data) == {Stat.ATTACK: attack}
	definition = game_data.balance.rarity(rarity)
	assert (definition.affix_min, definition.affix_max) == (affixes, affixes)


def test_item_stats_apply_rarity_and_affixes(data: GameData) -> None:
	game_data = with_test_items(data)
	item = ItemInstance(1, "test_helmet", "legendary", 0, (AffixRoll(Stat.MAX_HP, 7),))
	assert item_stats(item, game_data) == {Stat.ARMOR: 20, Stat.MAX_HP: 107}
	assert item_value(item, game_data) == 600


def test_item_score_weights_final_stats(data: GameData) -> None:
	game_data = with_test_items(data)
	weights = game_data.balance.item_score_weights
	common = ItemInstance(1, "test_helmet", "common", 0)
	assert item_score(common, game_data) == 10 * weights[Stat.ARMOR] + 50 * weights[Stat.MAX_HP]
	scores = [item_score(ItemInstance(1, "test_helmet", r, 0), game_data) for r in ("common", "rare", "legendary")]
	assert scores == sorted(scores)
	assert len(set(scores)) == 3
	with_affix = ItemInstance(1, "test_helmet", "common", 0, (AffixRoll(Stat.DODGE, 2),))
	assert item_score(with_affix, game_data) == item_score(common, game_data) + 2 * weights[Stat.DODGE]


def test_required_level_grows_with_the_item_tier(data: GameData) -> None:
	per_tier = data.balance.item_level_per_tier
	assert required_level(ItemInstance(1, "sword", "common", 0), data) == 1
	assert required_level(ItemInstance(1, "sword", "common", 3), data) == 1 + 3 * per_tier


def test_equip_rejects_items_above_the_player_level(new_engine: EngineFactory, data: GameData) -> None:
	game_data = with_test_items(data)
	engine = new_engine(game_data=game_data)
	player = engine.state.player
	axe = ItemInstance(60, "test_axe", "common", 5)
	player.bag.append(axe)
	rng_state = engine.rng_state
	assert engine.step(Equip(60)) == [{"type": "error", "code": "level_too_low"}]
	assert axe in player.bag
	assert engine.rng_state == rng_state
	player.level = required_level(axe, game_data)
	assert engine.step(Equip(60))[-1] == {"type": "item_equipped", "uid": 60, "itemId": "test_axe", "slot": "weapon"}


def test_selling_an_equipped_uid_is_rejected(new_engine: EngineFactory) -> None:
	engine = new_engine()
	player = engine.state.player
	weapon = player.equipment[Slot.WEAPON]
	gold = player.gold
	assert engine.step(SellItem(weapon.uid)) == [{"type": "error", "code": "invalid_item"}]
	assert player.equipment[Slot.WEAPON] == weapon
	assert player.gold == gold
