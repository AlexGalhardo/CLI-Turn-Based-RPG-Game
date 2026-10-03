from pathlib import Path

import pytest

from rpg.application.commands import NextFight
from rpg.application.events import Event
from rpg.domain.definitions import GameData
from rpg.domain.entities import ItemInstance, MonsterInstance
from rpg.infrastructure.i18n import Translator
from rpg.presentation.event_text import EventFormatter
from tests.conftest import EngineFactory


@pytest.fixture
def formatter(data: GameData, shared_dir: Path) -> EventFormatter:
	return EventFormatter(data, Translator(shared_dir))


@pytest.mark.parametrize(
	("event", "expected"),
	[
		({"type": "player_attacked", "damage": 12, "crit": False, "element": "fire"}, "You hit for 12 fire damage."),
		(
			{"type": "player_attacked", "damage": 30, "crit": True, "element": "physical"},
			"CRITICAL! You hit for 30 physical damage.",
		),
		(
			{
				"type": "spell_cast",
				"spellId": "flame_strike",
				"damage": 9,
				"crit": False,
				"element": "fire",
				"mana": 20,
			},
			"Flame Strike deals 9 fire damage.",
		),
		(
			{"type": "potion_used", "potionId": "mana_potion", "amount": 80, "resource": "mp"},
			"Mana Potion restores 80 MP.",
		),
		(
			{"type": "status_applied", "target": "player", "status": "burn", "turns": 3, "perTurn": 2},
			"You are burning (3 turns).",
		),
		({"type": "monster_killed", "monsterId": "dragon", "isBoss": False}, "You defeated Dragon!"),
		(
			{
				"type": "round_started",
				"round": 10,
				"tier": 0,
				"cycle": 0,
				"monsterId": "munster",
				"isBoss": True,
				"hp": 5,
			},
			"Round 10: the boss Munster challenges you! (5 HP)",
		),
		({"type": "item_sold", "uid": 3, "itemId": "sword", "gold": 25}, "You sold Sword for 25 gold."),
		({"type": "error", "code": "not_enough_mana"}, "Not enough mana."),
		({"type": "error", "code": "level_too_low"}, "Your level is too low for that item."),
		(
			{
				"type": "round_started",
				"round": 3,
				"tier": 0,
				"cycle": 0,
				"monsterId": "rat",
				"isBoss": False,
				"enemyClass": "elite",
				"hp": 90,
			},
			"Round 3: an ELITE Rat appears! (90 HP)",
		),
		(
			{
				"type": "monster_attacked",
				"attackId": "bite",
				"damage": 9,
				"element": "physical",
				"charged": False,
				"crit": True,
			},
			"CRITICAL! Rat hits you for 9 physical damage.",
		),
		({"type": "monster_dodged"}, "Rat dodges your attack!"),
		({"type": "monster_parried", "reflected": 4}, "Rat parries your attack: you take 4 damage!"),
		({"type": "monster_healed", "amount": 12}, "Rat heals 12 HP."),
		(
			{"type": "attack_parried", "attackId": "bite", "reflected": 3},
			"You parry the attack and reflect 3 damage!",
		),
		(
			{"type": "item_auto_equipped", "uid": 5, "itemId": "sword", "slot": "weapon", "score": 60},
			"Auto-equipped Sword (score 60).",
		),
		({"type": "item_auto_sold", "uid": 6, "itemId": "bow", "gold": 30}, "Sold Bow for 30 gold (auto-sell)."),
		({"type": "potion_dropped", "potionId": "mana_potion"}, "Loot: Mana Potion!"),
		({"type": "run_won", "round": 100}, "VICTORY! You defeated the final boss on round 100!"),
		({"type": "run_ended", "won": True}, "Your victory is recorded in the Hall of Fame."),
		(
			{"type": "spell_cast", "spellId": "ghost_spell", "damage": 1, "crit": False, "element": "fire", "mana": 1},
			None,
		),
	],
)
def test_format_events(
	new_engine: EngineFactory, formatter: EventFormatter, event: Event, expected: str | None
) -> None:
	state = new_engine().state
	state.monster = _rat()
	text = formatter.format(event, state)
	if expected is None:
		assert "ghost_spell" in text
	else:
		assert text == expected


def _rat() -> MonsterInstance:
	return MonsterInstance("rat", False, "normal", 10, 10, 1, 1, 1, ())


def test_monster_name_comes_from_state(new_engine: EngineFactory, formatter: EventFormatter, data: GameData) -> None:
	engine = new_engine()
	engine.step(NextFight())
	assert engine.state.monster is not None
	name = data.creature(engine.state.monster.creature_id).name
	text = formatter.format({"type": "monster_stunned"}, engine.state)
	assert text.startswith(name)


def test_item_name_by_uid(new_engine: EngineFactory, formatter: EventFormatter) -> None:
	state = new_engine().state
	state.player.bag.append(ItemInstance(77, "bow", "rare", 0))
	assert formatter.format({"type": "item_dropped", "uid": 77, "itemId": "bow", "rarity": "rare"}, state) == (
		"Loot: Bow [Rare]!"
	)
	assert "#999" in formatter.format({"type": "item_equipped", "uid": 999, "slot": "ring"}, state)
