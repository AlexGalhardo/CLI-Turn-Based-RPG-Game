//! Auto-battle policy decisions (port of `tests/unit/test_auto_battle.py`, docs/game-design.md §13).

mod common;

use std::rc::Rc;

use rpg::application::auto_battle::{AUTO_BATTLE_MODES, AutoBattleMode, AutoBattlePolicy};
use rpg::application::commands::Command;
use rpg::application::engine::GameEngine;
use rpg::application::run_state::RunConfig;
use rpg::domain::character::build_sheet;
use rpg::domain::definitions::GameData;
use rpg::domain::enums::Phase;

const OFFENSE_TURN: i64 = 1;

fn battle(data: &Rc<GameData>, vocation: &str, round: i64) -> GameEngine {
	let (mut engine, _) = GameEngine::new_run(Rc::clone(data), RunConfig::new("Auto", vocation, "normal"), 11).unwrap();
	engine.state_mut().round = round;
	engine.step(&Command::NextFight);
	engine
}

fn choose(data: &GameData, engine: &mut GameEngine, mode: AutoBattleMode, turn: i64) -> Command {
	engine.state_mut().turn = turn;
	AutoBattlePolicy::new(data, mode).choose(engine.state())
}

fn support_turn(data: &GameData, mode: AutoBattleMode) -> i64 {
	data.balance.auto_battle.mode(mode.as_str()).support_every - 1
}

#[test]
fn offensive_actions_per_mode() {
	let data = common::data();
	let mut engine = battle(&data, "warrior", 0);
	engine.state_mut().player.mp = 10_000;
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Melee, OFFENSE_TURN + 1), Command::Attack);
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Spells, OFFENSE_TURN), Command::cast("annihilation"));
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Balanced, 2), Command::cast("annihilation"));
	engine.state_mut().player.mp = data.spell("brutal_strike").mana;
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Spells, OFFENSE_TURN), Command::cast("brutal_strike"));
	engine.state_mut().player.mp = 0;
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Spells, OFFENSE_TURN), Command::Attack);
}

#[test]
fn support_turn_cadence_per_mode() {
	let data = common::data();
	let turns: Vec<i64> = AUTO_BATTLE_MODES.iter().map(|&mode| support_turn(&data, mode)).collect();
	assert_eq!(turns, [4, 4, 1]);
	let names: Vec<&str> = AUTO_BATTLE_MODES.iter().map(|mode| mode.as_str()).collect();
	assert_eq!(names, ["melee", "spells", "balanced"]);
}

#[test]
fn support_turn_heals_below_half_hp() {
	let data = common::data();
	for mode in AUTO_BATTLE_MODES {
		let mut engine = battle(&data, "warrior", 0);
		let max_hp = build_sheet(&engine.state().player, &data).max_hp;
		let player = &mut engine.state_mut().player;
		player.hp = max_hp * 40 / 100;
		player.mp = 10_000;
		let turn = support_turn(&data, mode);
		assert_eq!(choose(&data, &mut engine, mode, turn), Command::cast("wound_cleansing"), "{mode:?}");
		engine.state_mut().player.mp = 0;
		assert_eq!(choose(&data, &mut engine, mode, turn), Command::use_potion("health_potion"), "{mode:?}");
		engine.state_mut().player.potions.clear();
		assert_eq!(choose(&data, &mut engine, mode, turn), Command::Attack, "{mode:?}");
	}
}

#[test]
fn heal_waits_for_the_support_turn_above_the_emergency_line() {
	let data = common::data();
	let mut engine = battle(&data, "warrior", 0);
	let max_hp = build_sheet(&engine.state().player, &data).max_hp;
	engine.state_mut().player.hp = max_hp * 40 / 100;
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Melee, OFFENSE_TURN), Command::Attack);
}

#[test]
fn emergency_heal_on_any_turn() {
	let data = common::data();
	let mut engine = battle(&data, "warrior", 0);
	let max_hp = build_sheet(&engine.state().player, &data).max_hp;
	let player = &mut engine.state_mut().player;
	player.hp = max_hp * 20 / 100;
	player.mp = 10_000;
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Melee, OFFENSE_TURN), Command::cast("wound_cleansing"));
}

#[test]
fn best_potion_is_the_strongest_owned() {
	let data = common::data();
	let mut engine = battle(&data, "warrior", 0);
	let player = &mut engine.state_mut().player;
	player.hp = 1;
	player.mp = 0;
	player.potions.insert("strong_health_potion".into(), 1);
	assert_eq!(
		choose(&data, &mut engine, AutoBattleMode::Melee, OFFENSE_TURN),
		Command::use_potion("strong_health_potion")
	);
}

#[test]
fn support_turn_drinks_mana_when_low() {
	let data = common::data();
	let mut engine = battle(&data, "mage", 0);
	engine.state_mut().player.mp = 0;
	let turn = support_turn(&data, AutoBattleMode::Spells);
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Spells, turn), Command::use_potion("mana_potion"));
	engine.state_mut().player.potions.clear();
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Spells, turn), Command::Attack);
}

#[test]
fn support_turn_defends_against_a_telegraphed_charge() {
	let data = common::data();
	let mut engine = battle(&data, "warrior", 9);
	assert!(engine.state().monster.as_ref().unwrap().is_boss);
	let every = data.balance.boss_telegraph_every;
	engine.state_mut().monster.as_mut().unwrap().boss_actions = every;
	let turn = support_turn(&data, AutoBattleMode::Melee);
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Melee, turn), Command::Defend);
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Melee, turn + 1), Command::Attack);
	engine.state_mut().monster.as_mut().unwrap().boss_actions = 0;
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Melee, turn), Command::Attack);
	engine.state_mut().monster = None;
	assert_eq!(choose(&data, &mut engine, AutoBattleMode::Melee, turn), Command::Attack);
}

#[test]
fn policy_finishes_fights_with_valid_commands() {
	let data = common::data();
	for mode in AUTO_BATTLE_MODES {
		let mut engine = battle(&data, "archer", 0);
		let policy = AutoBattlePolicy::new(&data, mode);
		for _ in 0..500 {
			if engine.state().phase != Phase::Battle {
				break;
			}
			let events = engine.step(&policy.choose(engine.state()));
			assert!(events.iter().all(|event| !event.is_error()), "{events:?}");
		}
		assert_ne!(engine.state().phase, Phase::Battle, "{mode:?}");
	}
}

#[test]
fn policy_is_deterministic() {
	let data = common::data();
	let mut engine = battle(&data, "mage", 0);
	let rng_state = engine.rng_state();
	let all = |engine: &mut GameEngine| -> Vec<Command> {
		let mut commands = Vec::new();
		for mode in AUTO_BATTLE_MODES {
			for turn in 0..6 {
				commands.push(choose(&data, engine, mode, turn));
			}
		}
		commands
	};
	let first = all(&mut engine);
	let second = all(&mut engine);
	assert_eq!(first, second);
	assert_eq!(engine.rng_state(), rng_state);
}
