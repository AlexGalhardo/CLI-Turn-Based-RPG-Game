//! Replays every shared/golden scenario (the Python, TypeScript and Go suites run the same files).

mod common;

use std::fs;
use std::path::PathBuf;
use std::rc::Rc;

use rpg::application::bot::GreedyBot;
use rpg::application::commands::{command_from_json, command_to_json};
use rpg::application::engine::GameEngine;
use rpg::application::events::Event;
use rpg::application::run_state::{RunConfig, RunState};
use rpg::domain::rng::Rng;
use serde_json::{Value, json};

fn golden_dir() -> PathBuf {
	common::shared_dir().join("golden")
}

fn load(path: &PathBuf) -> Value {
	serde_json::from_str(&fs::read_to_string(path).expect("golden file")).expect("golden JSON")
}

fn scenario_files() -> Vec<PathBuf> {
	let mut files: Vec<PathBuf> = fs::read_dir(golden_dir())
		.expect("golden dir")
		.map(|entry| entry.expect("entry").path())
		.filter(|path| path.extension().is_some_and(|extension| extension == "json"))
		.filter(|path| path.file_name().is_some_and(|name| name != "prng.json"))
		.collect();
	files.sort();
	files
}

/// Same shape as `final_state()` of the reference golden tool.
fn final_state(engine: &GameEngine) -> Value {
	let state = engine.state();
	let player = &state.player;
	json!({
		"phase": state.phase.as_str(),
		"round": state.round,
		"turn": state.turn,
		"level": player.level,
		"xp": player.xp,
		"magicLevel": player.magic_level,
		"hp": player.hp,
		"mp": player.mp,
		"gold": player.gold,
		"rngState": engine.rng_state(),
		"nextItemUid": state.next_item_uid,
		"stats": serde_json::to_value(&state.stats).unwrap(),
	})
}

fn events_json(events: &[Event]) -> Value {
	serde_json::to_value(events).unwrap()
}

#[test]
fn prng_matches_reference_vectors() {
	let document = load(&golden_dir().join("prng.json"));
	for vector in document["vectors"].as_array().unwrap() {
		let mut rng = Rng::new(vector["seed"].as_u64().unwrap());
		let expected: Vec<u64> = vector["outputs"].as_array().unwrap().iter().map(|v| v.as_u64().unwrap()).collect();
		let actual: Vec<u64> = expected.iter().map(|_| u64::from(rng.next_u32())).collect();
		assert_eq!(actual, expected, "seed {}", vector["seed"]);
	}
}

#[test]
fn golden_files_exist() {
	assert!(scenario_files().len() >= 11);
}

#[test]
fn replay_matches_every_golden_file() {
	let data = common::data();
	for path in scenario_files() {
		let name = path.file_stem().unwrap().to_string_lossy().into_owned();
		let golden = load(&path);
		let config: RunConfig = serde_json::from_value(golden["config"].clone()).unwrap();
		let seed = golden["seed"].as_u64().unwrap();
		let (mut engine, first) = GameEngine::new_run(Rc::clone(&data), config, seed).unwrap();
		let expected = golden["events"].as_array().unwrap();
		assert_eq!(events_json(&first), expected[0], "{name}: run creation");
		for (index, raw) in golden["commands"].as_array().unwrap().iter().enumerate() {
			let command = command_from_json(raw).unwrap();
			assert_eq!(command_to_json(&command), *raw, "{name}: command #{} round trip", index + 1);
			let events = engine.step(&command);
			assert_eq!(events_json(&events), expected[index + 1], "{name}: command #{}", index + 1);
		}
		assert_eq!(final_state(&engine), golden["finalState"], "{name}: final state");
		assert_eq!(engine.state().to_json(), golden["finalRun"], "{name}: final run");
		let restored = RunState::from_json(&golden["finalRun"]).unwrap();
		assert_eq!(restored.to_json(), golden["finalRun"], "{name}: finalRun round trip");
	}
}

#[test]
fn bot_issues_exactly_the_recorded_commands() {
	let data = common::data();
	let bot = GreedyBot::new(&data);
	let mut checked = 0;
	for path in scenario_files().into_iter().filter(|path| path.to_string_lossy().contains("bot-full-run")) {
		let golden = load(&path);
		let config: RunConfig = serde_json::from_value(golden["config"].clone()).unwrap();
		let (mut engine, _) = GameEngine::new_run(Rc::clone(&data), config, golden["seed"].as_u64().unwrap()).unwrap();
		for (index, raw) in golden["commands"].as_array().unwrap().iter().enumerate() {
			let command = bot.choose(engine.state());
			assert_eq!(command_to_json(&command), *raw, "{}: bot command #{}", path.display(), index + 1);
			engine.step(&command);
		}
		assert_eq!(engine.state().phase.as_str(), "game_over");
		checked += 1;
	}
	assert_eq!(checked, 9);
}
