from dataclasses import replace

import pytest

from rpg.application.commands import Attack, BuyPotion, Cast, Defend, NextFight, UsePotion
from rpg.application.engine import GameEngine
from rpg.application.events import Event
from rpg.domain.character import build_sheet
from rpg.domain.definitions import GameData, MonsterAttack, StatusOnHit
from rpg.domain.entities import ActiveStatus, ItemInstance, MonsterInstance
from rpg.domain.enums import Element, Phase, Slot
from tests.conftest import EngineFactory, with_test_items


def _types(events: list[Event]) -> list[object]:
	return [e["type"] for e in events]


def _fight(engine: GameEngine) -> MonsterInstance:
	engine.step(NextFight())
	monster = engine.state.monster
	assert monster is not None
	return monster


def _fixed_attack(
	monster: MonsterInstance, damage: int, element: Element = Element.PHYSICAL, status: StatusOnHit | None = None
) -> None:
	monster.attacks = (MonsterAttack("test_hit", element, damage, damage, 1, status),)
	monster.hp = monster.max_hp = 10_000


def test_melee_damage_is_within_sheet_range(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine()
	_fight(engine)
	sheet = build_sheet(engine.state.player, data)
	assert engine.state.monster is not None
	resistance = data.creature(engine.state.monster.creature_id).resistance(Element.PHYSICAL)
	events = engine.step(Attack())
	hit = next(e for e in events if e["type"] == "player_attacked")
	low = max(1, sheet.melee_min * resistance // 100)
	high = sheet.melee_max * resistance // 100
	assert hit["crit"] is False
	assert isinstance(hit["damage"], int)
	assert low <= hit["damage"] <= max(high, low)


def test_invalid_battle_commands_do_not_consume_rng(new_engine: EngineFactory) -> None:
	engine = new_engine()
	assert engine.step(Attack()) == [{"type": "error", "code": "invalid_phase"}]
	_fight(engine)
	rng_state = engine.rng_state
	assert engine.step(Cast("flame_strike")) == [{"type": "error", "code": "unknown_spell"}]
	engine.state.player.mp = 0
	assert engine.step(Cast("brutal_strike")) == [{"type": "error", "code": "not_enough_mana"}]
	engine.state.player.potions.clear()
	assert engine.step(UsePotion("health_potion")) == [{"type": "error", "code": "no_potion"}]
	assert engine.step(UsePotion("elixir")) == [{"type": "error", "code": "unknown_potion"}]
	assert engine.step(NextFight()) == [{"type": "error", "code": "invalid_phase"}]
	assert engine.step(BuyPotion("health_potion", 1)) == [{"type": "error", "code": "invalid_phase"}]
	assert engine.rng_state == rng_state


def test_defend_halves_incoming_damage(new_engine: EngineFactory) -> None:
	engine = new_engine()
	_fixed_attack(_fight(engine), 100)
	engine.state.player.hp = 150
	events = engine.step(Defend())
	hit = next(e for e in events if e["type"] == "monster_attacked")
	assert hit["damage"] == 50
	assert engine.state.player.defending is False


def test_undefended_damage_and_death(new_engine: EngineFactory) -> None:
	engine = new_engine()
	monster = _fight(engine)
	_fixed_attack(monster, 1000)
	events = engine.step(Attack())
	assert events[-1] == {"type": "player_died", "monsterId": monster.creature_id, "round": 1}
	assert engine.state.phase is Phase.GAME_OVER
	assert engine.state.death_cause == monster.creature_id
	assert engine.step(Attack()) == [{"type": "error", "code": "invalid_phase"}]
	assert engine.step(NextFight()) == [{"type": "error", "code": "invalid_phase"}]


def test_monster_status_applies_and_ticks(new_engine: EngineFactory) -> None:
	engine = new_engine()
	_fixed_attack(_fight(engine), 10, Element.FIRE, StatusOnHit("burn", 100, 50))
	events = engine.step(Defend())
	applied = next(e for e in events if e["type"] == "status_applied")
	assert applied == {"type": "status_applied", "target": "player", "status": "burn", "turns": 3, "perTurn": 2}
	ticked = next(e for e in events if e["type"] == "status_ticked")
	assert ticked == {"type": "status_ticked", "target": "player", "status": "burn", "damage": 2}
	assert engine.state.player.statuses == [ActiveStatus("burn", 2, 2)]


def test_reapplying_status_refreshes_turns_and_keeps_higher_damage(new_engine: EngineFactory) -> None:
	engine = new_engine()
	_fixed_attack(_fight(engine), 10, Element.FIRE, StatusOnHit("burn", 100, 50))
	engine.state.player.statuses.append(ActiveStatus("burn", 1, 9))
	engine.step(Defend())
	assert engine.state.player.statuses == [ActiveStatus("burn", 2, 9)]


def test_status_expires(new_engine: EngineFactory) -> None:
	engine = new_engine()
	_fixed_attack(_fight(engine), 1)
	engine.state.player.statuses.append(ActiveStatus("bleed", 1, 1))
	events = engine.step(Defend())
	assert "status_expired" in _types(events)
	assert engine.state.player.statuses == []


def test_player_stun_skips_turn_and_has_cooldown(new_engine: EngineFactory) -> None:
	engine = new_engine()
	_fixed_attack(_fight(engine), 1, Element.PHYSICAL, StatusOnHit("stun", 100, 0))
	events = engine.step(Defend())
	types = _types(events)
	assert types.count("monster_attacked") == 2
	assert "player_stunned" in types
	assert types.count("status_applied") == 1
	assert engine.state.player.stun_cooldown == 1
	events = engine.step(Defend())
	assert "status_applied" not in _types(events)


def test_monster_stun_skips_its_attack(new_engine: EngineFactory) -> None:
	engine = new_engine()
	monster = _fight(engine)
	_fixed_attack(monster, 5)
	monster.statuses.append(ActiveStatus("stun", 1, 0))
	events = engine.step(Defend())
	assert "monster_stunned" in _types(events)
	assert "monster_attacked" not in _types(events)
	assert monster.stun_cooldown == 1


def test_monster_dies_from_status_tick(new_engine: EngineFactory) -> None:
	engine = new_engine()
	monster = _fight(engine)
	monster.hp = 1
	monster.statuses.append(ActiveStatus("bleed", 3, 5))
	events = engine.step(Defend())
	assert "monster_killed" in _types(events)
	assert engine.state.phase is Phase.MERCHANT


def test_boss_telegraphs_then_charges(new_engine: EngineFactory) -> None:
	engine = new_engine()
	engine.state.round = 9
	boss = _fight(engine)
	assert boss.is_boss
	boss.hp = boss.max_hp = 1_000_000
	boss.attacks = tuple(replace(attack, status=None) for attack in boss.attacks)
	engine.state.player.hp = 1_000_000
	sequence = []
	for _ in range(4):
		events = engine.step(Defend())
		sequence.append(
			next(
				(
					"telegraph"
					if e["type"] == "boss_telegraph"
					else ("charged" if e["type"] == "monster_attacked" and e["charged"] else "normal")
					for e in events
					if e["type"] in {"boss_telegraph", "monster_attacked", "attack_dodged", "attack_parried"}
				),
				"none",
			)
		)
	assert sequence == ["normal", "normal", "telegraph", "charged"]


def test_heal_is_capped_and_level_three_cleanses(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine()
	_fixed_attack(_fight(engine), 1)
	player = engine.state.player
	player.spell_uses["wound_cleansing"] = 50
	player.statuses.append(ActiveStatus("poison", 5, 1))
	player.hp = build_sheet(player, data).max_hp - 3
	events = engine.step(Cast("wound_cleansing"))
	healed = next(e for e in events if e["type"] == "spell_healed")
	assert healed["amount"] == 3
	assert healed["mana"] == 32
	assert {"type": "status_expired", "target": "player", "status": "poison"} in events
	assert player.spell_uses["wound_cleansing"] == 51


def test_spell_levels_up_after_twenty_uses(new_engine: EngineFactory) -> None:
	engine = new_engine()
	_fixed_attack(_fight(engine), 1)
	player = engine.state.player
	player.spell_uses["brutal_strike"] = 19
	player.mp = 1000
	events = engine.step(Cast("brutal_strike"))
	assert {"type": "spell_level_up", "spellId": "brutal_strike", "level": 2} in events
	cast = next(e for e in events if e["type"] == "spell_cast")
	assert cast["mana"] == 20


def test_magic_level_grows_with_mana_spent(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine()
	_fixed_attack(_fight(engine), 1)
	player = engine.state.player
	player.mana_spent = data.balance.magic_level_base - 1
	player.mp = 1000
	events = engine.step(Cast("brutal_strike"))
	assert {"type": "magic_level_up", "magicLevel": 2} in events


def test_immune_monster_takes_no_damage(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine("mage")
	monster = _fight(engine)
	_fixed_attack(monster, 1)
	engine.state.monster = replace(monster, creature_id="fire_elemental")
	assert data.creature("fire_elemental").resistance(Element.FIRE) == 0
	engine.state.player.mp = 1000
	events = engine.step(Cast("flame_strike"))
	cast = next(e for e in events if e["type"] == "spell_cast")
	assert cast["damage"] == 0


def test_items_add_crit_dodge_and_leech(new_engine: EngineFactory, data: GameData) -> None:
	game_data = with_test_items(data)
	engine = new_engine(game_data=game_data)
	player = engine.state.player
	player.equipment[Slot.RING] = ItemInstance(99, "test_ring", "common", 0)
	sheet = build_sheet(player, game_data)
	assert sheet.crit_chance == game_data.balance.caps.crit_chance
	assert sheet.dodge == game_data.balance.caps.dodge
	_fixed_attack(_fight(engine), 5)
	crits = dodges = 0
	for _ in range(60):
		player.hp = 100
		events = engine.step(Attack())
		crits += sum(1 for e in events if e["type"] == "player_attacked" and e["crit"])
		dodges += _types(events).count("attack_dodged")
	assert crits > 0
	assert dodges > 0


def test_potion_restores_and_is_consumed(new_engine: EngineFactory, data: GameData) -> None:
	engine = new_engine()
	_fixed_attack(_fight(engine), 1)
	player = engine.state.player
	player.hp = 10
	before = player.potion_count("mana_potion")
	player.mp = 0
	events = engine.step(UsePotion("mana_potion"))
	used = next(e for e in events if e["type"] == "potion_used")
	assert used["resource"] == "mp"
	assert player.potion_count("mana_potion") == before - 1
	assert 0 < player.mp <= build_sheet(player, data).max_mp


@pytest.mark.parametrize("vocation", ["warrior", "archer", "mage"])
def test_every_vocation_spell_can_be_cast(new_engine: EngineFactory, data: GameData, vocation: str) -> None:
	engine = new_engine(vocation)
	_fixed_attack(_fight(engine), 1)
	for spell_id in data.vocation(vocation).spells:
		engine.state.player.mp = 10_000
		engine.state.player.hp = 5
		events = engine.step(Cast(spell_id))
		assert any(e["type"] in {"spell_cast", "spell_healed"} and e["spellId"] == spell_id for e in events)
