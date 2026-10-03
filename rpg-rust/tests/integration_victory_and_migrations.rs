//! Victory phase through the session (save, resume, history, Hall of Fame) and schema 1 → 2 migrations (port of
//! `tests/integration/test_victory_and_migrations.py`).

mod common;

use std::fs;
use std::path::Path;
use std::rc::Rc;

use common::{FakeClock, TempDir, repositories};
use rpg::application::commands::Command;
use rpg::application::events::Event;
use rpg::application::game_session::{GameSession, Repositories};
use rpg::application::ports::{HistoryRepository, ProfileRepository, SaveRepository};
use rpg::application::run_state::RunConfig;
use rpg::domain::definitions::GameData;
use rpg::domain::enums::{EnemyClass, Phase};
use rpg::infrastructure::repositories::{FileHistoryRepository, FileProfileRepository, FileSaveRepository};
use serde_json::{Value, json};

const MAX_SWINGS: usize = 200;

fn read(path: &Path) -> Value {
	serde_json::from_str(&fs::read_to_string(path).unwrap()).unwrap()
}

fn write(path: &Path, document: &Value) {
	fs::write(path, serde_json::to_string(document).unwrap()).unwrap();
}

fn win_final_fight(data: &Rc<GameData>, directory: &Path) -> (GameSession, Repositories, Rc<FakeClock>) {
	let repos = repositories(directory);
	let clock = FakeClock::new();
	let config = RunConfig::new("Vic", "warrior", "easy").with_auto_equip(true);
	let (mut session, _) = GameSession::start(Rc::clone(data), config, 8, repos.clone(), clock.clone(), "1").unwrap();
	session.state_mut().round = data.balance.final_round - 1;
	session.step(&Command::NextFight).unwrap();
	let monster = session.state().monster.as_ref().unwrap();
	assert_eq!(monster.creature_id, "ferumbras");
	assert_eq!(monster.enemy_class, EnemyClass::Boss);
	for _ in 0..MAX_SWINGS {
		if session.state().phase != Phase::Battle {
			break;
		}
		let state = session.state_mut();
		state.player.hp = 1_000_000;
		state.monster.as_mut().unwrap().hp = 1;
		session.step(&Command::Attack).unwrap();
	}
	assert_eq!(session.state().phase, Phase::Victory);
	(session, repos, clock)
}

#[test]
fn victory_is_saved_resumed_and_ended_as_won() {
	let data = common::data();
	let dir = TempDir::new();
	let (_, repos, clock) = win_final_fight(&data, dir.path());
	let save = read(&dir.path().join("save.json"));
	assert_eq!(save["run"]["phase"], "victory");
	assert_eq!(save["run"]["won"], true);

	let mut resumed = GameSession::resume(Rc::clone(&data), repos.clone(), clock, "1").unwrap().unwrap();
	assert_eq!(resumed.state().phase, Phase::Victory);
	let result = resumed.step(&Command::EndRun).unwrap();
	assert_eq!(result.events, [Event::RunEnded { won: true }]);
	assert!(repos.profile.load().unwrap().achievements.contains_key("conqueror"));
	assert!(!dir.path().join("save.json").exists());
	let record = &repos.history.list().unwrap()[0];
	assert!(record.won);
	assert_eq!(record.death_cause, "");
	assert!(repos.profile.load().unwrap().hall_of_fame[0].won);
}

#[test]
fn continue_after_victory_keeps_the_run_won() {
	let data = common::data();
	let dir = TempDir::new();
	let (mut session, _, _) = win_final_fight(&data, dir.path());
	let events = session.step(&Command::ContinueRun).unwrap().events;
	assert_eq!(events, [Event::MerchantEntered { round: data.balance.final_round }]);
	assert_eq!(session.state().phase, Phase::Merchant);
	assert!(session.state().won);
	session.step(&Command::NextFight).unwrap();
	assert_eq!(session.state().round, data.balance.final_round + 1);
}

/// Turns a current save into what version 1 wrote: no M8 fields and the old `epic` rarity.
fn v1(document: &Value) -> Value {
	let mut old = document.clone();
	old["schemaVersion"] = json!(1);
	let run = old["run"].as_object_mut().unwrap();
	run["config"].as_object_mut().unwrap().remove("autoEquip");
	run.remove("won");
	run["player"]["equipment"]["weapon"]["rarity"] = json!("epic");
	let stats = run["stats"].as_object_mut().unwrap();
	for key in ["itemsAutoEquipped", "elitesKilled", "potionsDropped"] {
		stats.remove(key);
	}
	stats.insert("itemsDropped".into(), json!({"epic": 2, "legendary": 1}));
	stats.insert("droppedItems".into(), json!([{"itemId": "sword", "rarity": "epic", "round": 3}]));
	old
}

fn start(data: &Rc<GameData>, vocation: &str, directory: &Path) -> GameSession {
	let config = RunConfig::new("Old", vocation, "normal");
	GameSession::start(Rc::clone(data), config, 4, repositories(directory), FakeClock::new(), "1").unwrap().0
}

#[test]
fn v1_save_is_migrated() {
	let data = common::data();
	let dir = TempDir::new();
	start(&data, "warrior", dir.path());
	let path = dir.path().join("save.json");
	let mut old = v1(&read(&path));
	old["run"]["player"]["bag"] = json!([{"uid": 9, "itemId": "sword", "rarity": "epic", "tier": 0, "affixes": []}]);
	old["run"]["merchantStock"][0]["rarity"] = json!("epic");
	write(&path, &old);

	let loaded = FileSaveRepository::new(dir.path()).load().unwrap().unwrap();
	assert_eq!(loaded.schema_version, 2);
	let run = loaded.run;
	assert!(!run.config.auto_equip);
	assert!(!run.won);
	assert!(run.player.equipment.values().all(|item| item.rarity == "legendary"));
	assert_eq!(run.player.bag[0].rarity, "legendary");
	assert_eq!(run.merchant_stock[0].rarity, "legendary");
	assert_eq!(run.stats.items_dropped, [("legendary".to_owned(), 3)].into());
	assert_eq!(run.stats.dropped_items[0].rarity, "legendary");
	assert_eq!(run.stats.elites_killed, 0);
	assert_eq!(run.stats.items_auto_equipped, 0);
	assert!(run.stats.potions_dropped.is_empty());
}

#[test]
fn v1_monster_gets_its_class_from_is_boss() {
	let data = common::data();
	let dir = TempDir::new();
	let mut session = start(&data, "mage", dir.path());
	session.state_mut().round = 9;
	session.step(&Command::NextFight).unwrap();
	let path = dir.path().join("save.json");
	let mut document = read(&path);
	document["run"]["monster"] = session.state().to_json()["monster"].clone();
	let mut old = v1(&document);
	old["run"]["monster"].as_object_mut().unwrap().remove("enemyClass");
	write(&path, &old);
	let loaded = FileSaveRepository::new(dir.path()).load().unwrap().unwrap();
	assert_eq!(loaded.run.monster.unwrap().enemy_class, EnemyClass::Boss);

	old["run"]["monster"]["isBoss"] = json!(false);
	write(&path, &old);
	let loaded = FileSaveRepository::new(dir.path()).load().unwrap().unwrap();
	assert_eq!(loaded.run.monster.unwrap().enemy_class, EnemyClass::Normal);
}

#[test]
fn newer_documents_are_refused_before_migrating() {
	let dir = TempDir::new();
	write(&dir.path().join("save.json"), &json!({"schemaVersion": 3}));
	assert!(FileSaveRepository::new(dir.path()).load().unwrap_err().to_string().contains("update the game"));
	fs::create_dir_all(dir.path().join("history")).unwrap();
	write(&dir.path().join("history").join("a.json"), &json!({"schemaVersion": 3}));
	assert!(FileHistoryRepository::new(dir.path()).list().is_err());
	write(&dir.path().join("profile.json"), &json!({"schemaVersion": 3}));
	assert!(FileProfileRepository::new(dir.path()).load().is_err());
}

#[test]
fn v1_history_and_profile_are_migrated() {
	let data = common::data();
	let dir = TempDir::new();
	let won_dir = dir.path().join("won");
	let old_dir = dir.path().join("old");
	let (mut session, _, _) = win_final_fight(&data, &won_dir);
	session.step(&Command::EndRun).unwrap();
	let record_path = fs::read_dir(won_dir.join("history")).unwrap().next().unwrap().unwrap().path();
	let mut record = read(&record_path);
	record["schemaVersion"] = json!(1);
	record.as_object_mut().unwrap().remove("won");
	let stats = record["stats"].as_object_mut().unwrap();
	for key in ["itemsAutoEquipped", "elitesKilled", "potionsDropped"] {
		stats.remove(key);
	}
	let history_dir = old_dir.join("history");
	fs::create_dir_all(&history_dir).unwrap();
	write(&history_dir.join(record_path.file_name().unwrap()), &record);
	let migrated = &FileHistoryRepository::new(&old_dir).list().unwrap()[0];
	assert!(!migrated.won);
	assert_eq!(migrated.schema_version, 2);

	let mut profile = read(&won_dir.join("profile.json"));
	profile["schemaVersion"] = json!(1);
	for entry in profile["hallOfFame"].as_array_mut().unwrap() {
		entry.as_object_mut().unwrap().remove("won");
	}
	write(&old_dir.join("profile.json"), &profile);
	assert!(!FileProfileRepository::new(&old_dir).load().unwrap().hall_of_fame[0].won);
}
