"""Auto-battle policy decisions (docs/game-design.md §13)."""

import pytest

from rpg.application.auto_battle import AutoBattleMode, AutoBattlePolicy
from rpg.application.commands import Attack, Cast, Defend, NextFight, UsePotion
from rpg.application.engine import GameEngine
from rpg.application.run_state import RunConfig
from rpg.domain.character import build_sheet
from rpg.domain.definitions import GameData
from rpg.domain.enums import Phase

OFFENSE_TURN = 1


def _battle(data: GameData, vocation: str = "warrior", round_number: int = 0) -> GameEngine:
	engine, _ = GameEngine.new_run(data, RunConfig("Auto", vocation, "normal"), 11)
	engine.state.round = round_number
	engine.step(NextFight())
	return engine


def _choose(data: GameData, engine: GameEngine, mode: AutoBattleMode, turn: int) -> object:
	engine.state.turn = turn
	return AutoBattlePolicy(data, mode).choose(engine.state)


def _support_turn(data: GameData, mode: AutoBattleMode) -> int:
	every = data.balance.auto_battle.mode(mode.value).support_every
	return every - 1


def test_offensive_actions_per_mode(data: GameData) -> None:
	engine = _battle(data)
	engine.state.player.mp = 10_000
	assert _choose(data, engine, AutoBattleMode.MELEE, OFFENSE_TURN + 1) == Attack()
	assert _choose(data, engine, AutoBattleMode.SPELLS, OFFENSE_TURN) == Cast("annihilation")
	assert _choose(data, engine, AutoBattleMode.BALANCED, 2) == Cast("annihilation")
	engine.state.player.mp = data.spell("brutal_strike").mana
	assert _choose(data, engine, AutoBattleMode.SPELLS, OFFENSE_TURN) == Cast("brutal_strike")
	engine.state.player.mp = 0
	assert _choose(data, engine, AutoBattleMode.SPELLS, OFFENSE_TURN) == Attack()


def test_support_turn_cadence_per_mode(data: GameData) -> None:
	assert [_support_turn(data, mode) for mode in AutoBattleMode] == [4, 4, 1]


@pytest.mark.parametrize("mode", list(AutoBattleMode))
def test_support_turn_heals_below_half_hp(data: GameData, mode: AutoBattleMode) -> None:
	engine = _battle(data)
	player = engine.state.player
	max_hp = build_sheet(player, data).max_hp
	player.hp = max_hp * 40 // 100
	player.mp = 10_000
	assert _choose(data, engine, mode, _support_turn(data, mode)) == Cast("wound_cleansing")
	player.mp = 0
	assert _choose(data, engine, mode, _support_turn(data, mode)) == UsePotion("health_potion")
	player.potions.clear()
	assert _choose(data, engine, mode, _support_turn(data, mode)) == Attack()


def test_heal_waits_for_the_support_turn_above_the_emergency_line(data: GameData) -> None:
	engine = _battle(data)
	player = engine.state.player
	player.hp = build_sheet(player, data).max_hp * 40 // 100
	assert _choose(data, engine, AutoBattleMode.MELEE, OFFENSE_TURN) == Attack()


def test_emergency_heal_on_any_turn(data: GameData) -> None:
	engine = _battle(data)
	player = engine.state.player
	player.hp = build_sheet(player, data).max_hp * 20 // 100
	player.mp = 10_000
	assert _choose(data, engine, AutoBattleMode.MELEE, OFFENSE_TURN) == Cast("wound_cleansing")


def test_best_potion_is_the_strongest_owned(data: GameData) -> None:
	engine = _battle(data)
	player = engine.state.player
	player.hp = 1
	player.mp = 0
	player.potions["strong_health_potion"] = 1
	assert _choose(data, engine, AutoBattleMode.MELEE, OFFENSE_TURN) == UsePotion("strong_health_potion")


def test_support_turn_drinks_mana_when_low(data: GameData) -> None:
	engine = _battle(data, "mage")
	engine.state.player.mp = 0
	turn = _support_turn(data, AutoBattleMode.SPELLS)
	assert _choose(data, engine, AutoBattleMode.SPELLS, turn) == UsePotion("mana_potion")
	engine.state.player.potions.clear()
	assert _choose(data, engine, AutoBattleMode.SPELLS, turn) == Attack()


def test_support_turn_defends_against_a_telegraphed_charge(data: GameData) -> None:
	engine = _battle(data, round_number=9)
	boss = engine.state.monster
	assert boss is not None
	assert boss.is_boss
	every = data.balance.boss_telegraph_every
	boss.boss_actions = every
	turn = _support_turn(data, AutoBattleMode.MELEE)
	assert _choose(data, engine, AutoBattleMode.MELEE, turn) == Defend()
	assert _choose(data, engine, AutoBattleMode.MELEE, turn + 1) == Attack()
	boss.boss_actions = 0
	assert _choose(data, engine, AutoBattleMode.MELEE, turn) == Attack()


@pytest.mark.parametrize("mode", list(AutoBattleMode))
def test_policy_finishes_fights_with_valid_commands(data: GameData, mode: AutoBattleMode) -> None:
	engine = _battle(data, "archer")
	policy = AutoBattlePolicy(data, mode)
	for _ in range(500):
		if engine.state.phase is not Phase.BATTLE:
			break
		events = engine.step(policy.choose(engine.state))
		assert all(e["type"] != "error" for e in events)
	assert engine.state.phase is not Phase.BATTLE


def test_policy_is_deterministic(data: GameData) -> None:
	engine = _battle(data, "mage")
	rng_state = engine.rng_state
	first = [_choose(data, engine, mode, turn) for mode in AutoBattleMode for turn in range(6)]
	second = [_choose(data, engine, mode, turn) for mode in AutoBattleMode for turn in range(6)]
	assert first == second
	assert engine.rng_state == rng_state
