"""M8 rules (docs/game-design.md §3, §6, §8, §9): enemy classes, monster dodge/parry/heal/crit, parry reflects,
class drop tables, potion drops, spell level effects and the victory phase."""

from dataclasses import replace

import pytest

from rpg.application.commands import (
	Attack,
	BuyPotion,
	Cast,
	ContinueRun,
	Defend,
	EndRun,
	Equip,
	NextFight,
	SellItem,
)
from rpg.application.engine import GameEngine
from rpg.application.events import Event
from rpg.application.spawner import spawn_monster
from rpg.domain.character import build_sheet
from rpg.domain.definitions import Caps, GameData, ItemDef, MonsterAttack
from rpg.domain.entities import ItemInstance, MonsterInstance
from rpg.domain.enums import Element, Phase, Slot, Stat
from rpg.domain.formulas import pct
from rpg.domain.rng import Rng
from tests.conftest import EngineFactory, calm, with_enemy_class

MAX_TURNS = 300


def _types(events: list[Event]) -> list[object]:
	return [e["type"] for e in events]


def _fight(engine: GameEngine, damage: int = 1, element: Element = Element.PHYSICAL) -> MonsterInstance:
	engine.step(NextFight())
	monster = engine.state.monster
	assert monster is not None
	monster.attacks = (MonsterAttack("test_hit", element, damage, damage, 1),)
	monster.hp = monster.max_hp = 10_000
	return monster


def _kill(engine: GameEngine) -> list[Event]:
	"""Finishes the current fight with melee hits (the monster is left with 1 HP before each hit)."""
	for _ in range(MAX_TURNS):
		monster = engine.state.monster
		if monster is None:
			break
		monster.hp = 1
		engine.state.player.hp = 1_000_000
		events = engine.step(Attack())
		if engine.state.phase is not Phase.BATTLE:
			return events
	raise AssertionError("the fight did not end")


def _phase(engine: GameEngine) -> Phase:
	"""Reads the phase through a function so type checkers don't narrow it between steps."""
	return engine.state.phase


def _physical_resistant_100(data: GameData) -> str:
	return next(m.id for m in data.monsters if m.resistance(Element.PHYSICAL) == 100)


# ── spawn ────────────────────────────────────────────────────────────────────


def test_elite_spawn_scales_stats_and_rewards(data: GameData) -> None:
	normal_data = with_enemy_class(calm(data), "normal")
	elite_data = replace(normal_data, balance=replace(normal_data.balance, elite_chance_pct=100))
	difficulty = data.balance.difficulty("normal")
	normal, _ = spawn_monster(normal_data, Rng(5), 3, difficulty)
	elite, _ = spawn_monster(elite_data, Rng(5), 3, difficulty)
	row = data.balance.enemy_class("elite")
	assert (normal.enemy_class, elite.enemy_class) == ("normal", "elite")
	assert elite.creature_id == normal.creature_id
	assert elite.max_hp == pct(normal.max_hp, row.stat_pct)
	assert elite.attacks[0].max == pct(normal.attacks[0].max, row.stat_pct)
	assert elite.xp == pct(normal.xp, row.reward_pct)
	assert elite.gold_max == pct(normal.gold_max, row.reward_pct)


def test_elite_roll_follows_the_monster_pick(data: GameData) -> None:
	difficulty = data.balance.difficulty("normal")
	rng = Rng(77)
	classes = [spawn_monster(data, rng, 1 + i % 9, difficulty)[0].enemy_class for i in range(300)]
	assert set(classes) == {"normal", "elite"}
	assert 30 <= classes.count("elite") <= 90
	boss, _ = spawn_monster(data, Rng(1), 10, difficulty)
	assert boss.enemy_class == "boss"


def test_round_started_reports_the_enemy_class(new_engine: EngineFactory, data: GameData) -> None:
	elite_data = replace(data, balance=replace(data.balance, elite_chance_pct=100))
	engine = new_engine(game_data=elite_data)
	started = engine.step(NextFight())[0]
	assert started["enemyClass"] == "elite"
	assert started["isBoss"] is False


# ── monster dodge, parry, heal and crit ──────────────────────────────────────


def test_monster_dodge_stops_melee_and_spells(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine(game_data=with_enemy_class(calm(data), "normal", dodge=100))
	monster = _fight(engine)
	events = engine.step(Attack())
	assert events[0] == {"type": "monster_dodged"}
	assert "player_attacked" not in _types(events)
	assert monster.hp == monster.max_hp
	player = engine.state.player
	player.mp = 1000
	events = engine.step(Cast("brutal_strike"))
	assert events[0] == {"type": "monster_dodged"}
	assert player.mp < 1000
	assert player.spell_uses["brutal_strike"] == 1


def test_monster_parry_reflects_physical_hits(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine(game_data=with_enemy_class(calm(data), "normal", parry=100))
	monster = _fight(engine)
	player = engine.state.player
	events = engine.step(Defend())
	assert "monster_parried" not in _types(events)
	hp = player.hp
	events = engine.step(Attack())
	parried = events[0]
	assert parried["type"] == "monster_parried"
	assert isinstance(parried["reflected"], int)
	assert parried["reflected"] >= 1
	assert "leeched" not in _types(events)
	assert monster.hp == monster.max_hp
	hits = sum(e["damage"] for e in events if e["type"] == "monster_attacked" and isinstance(e["damage"], int))
	regen = sum(e["hp"] for e in events if e["type"] == "regenerated" and isinstance(e["hp"], int))
	assert player.hp == hp - parried["reflected"] - hits + regen


def test_monster_parry_reflect_can_kill_the_player(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine(game_data=with_enemy_class(calm(data), "normal", parry=100))
	monster = _fight(engine)
	engine.state.player.hp = 1
	events = engine.step(Attack())
	assert _types(events) == ["monster_parried", "player_died"]
	assert engine.state.phase is Phase.GAME_OVER
	assert monster.hp == monster.max_hp


def test_monster_parry_ignores_non_physical_spells(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine("mage", game_data=with_enemy_class(calm(data), "normal", parry=100))
	_fight(engine)
	engine.state.player.mp = 1000
	events = engine.step(Cast("flame_strike"))
	assert "monster_parried" not in _types(events)
	assert "spell_cast" in _types(events)


def test_monster_heals_instead_of_attacking(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine(game_data=with_enemy_class(calm(data), "normal", heal=100))
	monster = _fight(engine)
	events = engine.step(Defend())
	assert "monster_healed" not in _types(events)
	assert "monster_attacked" in _types(events)
	monster.hp = monster.max_hp - 5
	events = engine.step(Defend())
	assert {"type": "monster_healed", "amount": 5} in events
	assert "monster_attacked" not in _types(events)
	monster.hp = 100
	events = engine.step(Defend())
	assert {"type": "monster_healed", "amount": pct(monster.max_hp, data.balance.monster_heal_pct)} in events


def test_healing_boss_does_not_advance_its_pattern(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine(game_data=with_enemy_class(calm(data), "boss", heal=100))
	engine.state.round = 9
	engine.step(NextFight())
	boss = engine.state.monster
	assert boss is not None
	boss.hp = boss.max_hp - 1
	engine.state.player.hp = 1_000_000
	engine.step(Defend())
	assert boss.boss_actions == 0


@pytest.mark.parametrize(("crit", "expected"), [(0, 100), (100, 150)])
def test_monster_crit_multiplies_raw_damage(
	new_engine: EngineFactory, data: GameData, crit: int, expected: int
) -> None:
	engine = new_engine(game_data=with_enemy_class(calm(data), "normal", crit=crit))
	_fight(engine, damage=100, element=Element.FIRE)
	engine.state.player.hp = 1000
	hit = next(e for e in engine.step(Attack()) if e["type"] == "monster_attacked")
	assert hit["crit"] is (crit == 100)
	assert hit["damage"] == expected


def _parry_data(data: GameData) -> GameData:
	shield = ItemDef("test_parry_shield", "Test Shield", Slot.SHIELD, "shield", 0, None, {Stat.PARRY: 100}, 10)
	caps = data.balance.caps
	game_data = replace(calm(data), items=(*data.items, shield))
	return replace(game_data, balance=replace(game_data.balance, caps=Caps(caps.crit_chance, 0, 100, 25, 75)))


def test_player_parry_reflects_damage_to_the_monster(new_engine: EngineFactory, data: GameData) -> None:
	game_data = _parry_data(data)
	engine = new_engine(game_data=game_data)
	engine.state.player.equipment[Slot.SHIELD] = ItemInstance(90, "test_parry_shield", "common", 0)
	assert build_sheet(engine.state.player, game_data).parry == 100
	monster = _fight(engine, damage=50)
	events = engine.step(Defend())
	assert {"type": "attack_parried", "attackId": "test_hit", "reflected": 10} in events
	assert monster.hp == monster.max_hp - 10
	assert engine.state.stats.parries == 1
	monster.hp = 5
	events = engine.step(Defend())
	assert "monster_killed" in _types(events)
	assert engine.state.phase is Phase.MERCHANT


# ── victory, drops and potions ───────────────────────────────────────────────


def test_normal_drop_table(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine(game_data=with_enemy_class(calm(data), "normal", drop_chance_pct=100))
	_fight(engine)
	events = _kill(engine)
	drops = [e for e in events if e["type"] == "item_dropped"]
	assert len(drops) == 1
	assert drops[0]["rarity"] in {"common", "rare"}
	assert "potion_dropped" not in _types(events)
	engine = new_engine(game_data=with_enemy_class(calm(data), "normal", drop_chance_pct=0))
	_fight(engine)
	assert "item_dropped" not in _types(_kill(engine))


def test_elite_drops_a_good_item_and_maybe_a_potion(new_engine: EngineFactory, data: GameData) -> None:
	game_data = with_enemy_class(calm(data), "elite", potion_drop_pct=100)
	game_data = replace(game_data, balance=replace(game_data.balance, elite_chance_pct=100))
	engine = new_engine(game_data=game_data)
	_fight(engine)
	before = dict(engine.state.player.potions)
	events = _kill(engine)
	drops = [e for e in events if e["type"] == "item_dropped"]
	assert len(drops) == 1
	assert drops[0]["rarity"] in {"rare", "legendary"}
	potion = next(e for e in events if e["type"] == "potion_dropped")
	potion_id = str(potion["potionId"])
	assert data.potion(potion_id).unlock_round <= 1
	assert engine.state.player.potions[potion_id] == before.get(potion_id, 0) + 1
	assert engine.state.stats.potions_dropped[potion_id] == 1
	assert engine.state.stats.elites_killed == 1
	assert next(e for e in events if e["type"] == "monster_killed")["enemyClass"] == "elite"


def test_potion_drop_without_unlocked_potions_consumes_nothing(new_engine: EngineFactory, data: GameData) -> None:
	locked = tuple(replace(p, unlock_round=99) for p in data.potions)
	game_data = with_enemy_class(calm(data), "normal", drop_chance_pct=0, potion_drop_pct=100)
	engine = new_engine(game_data=replace(game_data, potions=locked))
	_fight(engine)
	assert "potion_dropped" not in _types(_kill(engine))


def test_boss_drops_several_top_items(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine(game_data=calm(data))
	engine.state.round = 9
	_fight(engine)
	events = _kill(engine)
	drops = [e for e in events if e["type"] == "item_dropped"]
	assert len(drops) == data.balance.enemy_class("boss").drops
	assert {d["rarity"] for d in drops} <= {"legendary", "mythic"}


def test_full_bag_auto_sells_drops(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine(game_data=with_enemy_class(calm(data), "normal", drop_chance_pct=100))
	player = engine.state.player
	player.bag.extend(ItemInstance(500 + i, "sword", "common", 0) for i in range(data.balance.bag_capacity))
	_fight(engine)
	events = _kill(engine)
	assert "item_auto_sold" in _types(events)
	assert len(player.bag) == data.balance.bag_capacity


def _final_victory(engine: GameEngine) -> list[Event]:
	engine.state.round = engine.data.balance.final_round - 1
	_fight(engine)
	return _kill(engine)


def test_beating_the_final_boss_enters_the_victory_phase(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine(game_data=calm(data))
	events = _final_victory(engine)
	assert events[-1] == {"type": "run_won", "round": data.balance.final_round}
	assert "merchant_entered" not in _types(events)
	assert engine.state.phase is Phase.VICTORY
	assert engine.state.won is True
	rng_state = engine.rng_state
	stock = list(engine.state.merchant_stock)
	for command in (Attack(), Defend(), NextFight(), BuyPotion("health_potion", 1), Equip(1), SellItem(1)):
		assert engine.step(command) == [{"type": "error", "code": "invalid_phase"}]
	assert engine.rng_state == rng_state
	assert engine.state.merchant_stock == stock
	assert engine.step(EndRun()) == [{"type": "run_ended", "won": True}]
	assert _phase(engine) is Phase.GAME_OVER
	assert engine.state.death_cause is None
	assert engine.step(ContinueRun()) == [{"type": "error", "code": "invalid_phase"}]


def test_continue_run_enters_the_merchant(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine(game_data=calm(data))
	_final_victory(engine)
	events = engine.step(ContinueRun())
	assert events == [{"type": "merchant_entered", "round": data.balance.final_round}]
	assert engine.state.phase is Phase.MERCHANT
	assert len(engine.state.merchant_stock) == data.balance.merchant_stock_size
	engine.step(NextFight())
	assert engine.state.round == data.balance.final_round + 1
	assert engine.state.won is True


def test_victory_commands_are_rejected_outside_the_victory_phase(new_engine: EngineFactory) -> None:
	engine = new_engine()
	assert engine.step(EndRun()) == [{"type": "error", "code": "invalid_phase"}]
	assert engine.step(ContinueRun()) == [{"type": "error", "code": "invalid_phase"}]


# ── spell levels ─────────────────────────────────────────────────────────────


@pytest.mark.parametrize(("uses", "effect"), [(0, 100), (20, 150), (50, 200)])
def test_spell_level_effect_scales_damage(new_engine: EngineFactory, data: GameData, uses: int, effect: int) -> None:
	engine = new_engine(game_data=calm(data))
	monster = _fight(engine)
	engine.state.monster = replace(monster, creature_id=_physical_resistant_100(data))
	player = engine.state.player
	player.spell_uses["brutal_strike"] = uses
	player.mp = 1000
	rng = Rng(engine.rng_state)
	spell = data.spell("brutal_strike")
	bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level
	base = rng.roll(spell.min + bonus, spell.max + bonus)
	cast = next(e for e in engine.step(Cast("brutal_strike")) if e["type"] == "spell_cast")
	assert cast["damage"] == max(1, pct(base, effect))


def test_spell_level_effect_scales_healing(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine(game_data=calm(data))
	_fight(engine)
	player = engine.state.player
	player.spell_uses["wound_cleansing"] = 20
	player.mp = 1000
	player.hp = 1
	rng = Rng(engine.rng_state)
	spell = data.spell("wound_cleansing")
	bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level
	expected = pct(rng.roll(spell.min + bonus, spell.max + bonus), 150)
	room = build_sheet(player, data).max_hp - player.hp
	healed = next(e for e in engine.step(Cast("wound_cleansing")) if e["type"] == "spell_healed")
	assert healed["amount"] == min(expected, room)
