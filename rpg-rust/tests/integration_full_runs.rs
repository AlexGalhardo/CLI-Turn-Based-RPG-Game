//! Whole runs driven by the bot: the engine must always terminate, be deterministic and survive save/restore
//! (port of `tests/integration/test_full_runs.py`).

mod common;

use std::collections::HashSet;
use std::rc::Rc;

use rpg::application::bot::GreedyBot;
use rpg::application::engine::GameEngine;
use rpg::application::events::Event;
use rpg::application::run_state::{RunConfig, RunState};
use rpg::domain::definitions::GameData;
use rpg::domain::enums::Phase;

const MAX_STEPS: usize = 50_000;

fn play_to_death(engine: &mut GameEngine, bot: &GreedyBot<'_>) -> Vec<Vec<Event>> {
	let mut log = Vec::new();
	for _ in 0..MAX_STEPS {
		if engine.state().phase == Phase::GameOver {
			return log;
		}
		let events = engine.step(&bot.choose(engine.state()));
		assert!(events.iter().all(|event| !event.is_error()), "{events:?}");
		log.push(events);
	}
	panic!("run did not finish");
}

fn start(data: &Rc<GameData>, vocation: &str, difficulty: &str, seed: u64) -> (GameEngine, Vec<Event>) {
	GameEngine::new_run(Rc::clone(data), RunConfig::new("Bot", vocation, difficulty), seed).unwrap()
}

#[test]
fn bot_plays_until_the_run_ends() {
	let data = common::data();
	let mut won = 0;
	for vocation in ["warrior", "archer", "mage"] {
		for difficulty in ["easy", "normal", "hard"] {
			let (mut engine, _) = start(&data, vocation, difficulty, 1234);
			let log = play_to_death(&mut engine, &GreedyBot::new(&data));
			let state = engine.state();
			assert_eq!(state.phase, Phase::GameOver);
			assert!(state.round >= 1);
			assert!(state.stats.damage_dealt > 0);
			if state.won {
				won += 1;
				assert_eq!(state.round, data.balance.final_round);
				assert_eq!(state.death_cause, None);
				assert_eq!(log[log.len() - 2].last(), Some(&Event::RunWon { round: state.round }));
				assert_eq!(log[log.len() - 1], [Event::RunEnded { won: true }]);
				assert_eq!(state.stats.total_kills(), state.round);
			} else {
				assert!(state.death_cause.as_deref().is_some_and(|cause| !cause.is_empty()));
				let last = log.last().and_then(|events| events.last()).map(Event::kind);
				assert_eq!(last.as_deref(), Some("player_died"));
				assert_eq!(state.stats.total_kills(), state.round - 1);
			}
		}
	}
	assert!(won > 0, "both branches are covered");
}

/// The balance must keep the victory reachable.
#[test]
fn some_bot_runs_are_won() {
	let data = common::data();
	let won = (2002..2006)
		.filter(|&seed| {
			let (mut engine, _) = start(&data, "archer", "easy", seed);
			play_to_death(&mut engine, &GreedyBot::new(&data));
			engine.state().won
		})
		.count();
	assert!(won > 0);
}

#[test]
fn same_seed_same_events() {
	let data = common::data();
	let logs: Vec<Vec<Vec<Event>>> = (0..2)
		.map(|_| {
			let (mut engine, first) = start(&data, "archer", "normal", 777);
			let mut log = vec![first];
			log.extend(play_to_death(&mut engine, &GreedyBot::new(&data)));
			log
		})
		.collect();
	assert_eq!(logs[0], logs[1]);
}

#[test]
fn different_seeds_diverge() {
	let data = common::data();
	let finals: HashSet<String> = (1..=3)
		.map(|seed| {
			let (mut engine, _) = start(&data, "warrior", "normal", seed);
			play_to_death(&mut engine, &GreedyBot::new(&data));
			serde_json::to_string(&engine.state().stats).unwrap()
		})
		.collect();
	assert!(finals.len() > 1);
}

#[test]
fn restore_mid_run_continues_identically() {
	let data = common::data();
	let bot = GreedyBot::new(&data);
	let (mut reference, _) = start(&data, "mage", "hard", 99);
	let reference_log = play_to_death(&mut reference, &bot);

	let (mut engine, _) = start(&data, "mage", "hard", 99);
	let mut log = Vec::new();
	while engine.state().phase != Phase::GameOver {
		if engine.state().phase == Phase::Merchant && engine.state().round % 3 == 0 {
			let snapshot: serde_json::Value =
				serde_json::from_str(&serde_json::to_string(&engine.state().to_json()).unwrap()).unwrap();
			engine = GameEngine::restore(Rc::clone(&data), RunState::from_json(&snapshot).unwrap(), engine.rng_state());
		}
		log.push(engine.step(&bot.choose(engine.state())));
	}
	assert_eq!(log, reference_log);
}

#[test]
fn new_run_rejects_invalid_config() {
	let data = common::data();
	let error = GameEngine::new_run(Rc::clone(&data), RunConfig::new("X", "knight", "normal"), 1).unwrap_err();
	assert_eq!(error.to_string(), "invalid run config: 'knight'");
	let error = GameEngine::new_run(Rc::clone(&data), RunConfig::new("X", "mage", "nightmare"), 1).unwrap_err();
	assert!(error.to_string().contains("invalid run config"));
}
