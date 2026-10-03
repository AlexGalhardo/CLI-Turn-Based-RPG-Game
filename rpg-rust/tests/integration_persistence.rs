//! Saves, history, profile and settings on disk (port of `tests/integration/test_persistence.py`).

mod common;

use std::fs;
use std::rc::Rc;

use common::{FakeClock, TempDir, repositories};
use rpg::application::bot::GreedyBot;
use rpg::application::commands::Command;
use rpg::application::game_session::{GameSession, SessionError};
use rpg::application::ports::{Clock, HistoryRepository, ProfileRepository, SaveRepository};
use rpg::application::profile::{BestiaryEntry, HallOfFameEntry, Profile, ProfileService};
use rpg::application::run_state::RunConfig;
use rpg::application::save_game::{PersistenceError, parse_timestamp};
use rpg::domain::enums::Phase;
use rpg::infrastructure::repositories::{
	FileHistoryRepository, FileProfileRepository, FileSaveRepository, Settings, SettingsRepository, SystemClock,
};
use serde_json::Value;

fn read(path: &std::path::Path) -> Value {
	serde_json::from_str(&fs::read_to_string(path).unwrap()).unwrap()
}

#[test]
fn new_session_autosaves_at_merchant() {
	let data = common::data();
	let dir = TempDir::new();
	let (mut session, events) = GameSession::start(
		Rc::clone(&data),
		RunConfig::new("Alex", "archer", "normal"),
		5,
		repositories(dir.path()),
		FakeClock::new(),
		"9.9.9",
	)
	.unwrap();
	assert_eq!(events[0].kind(), "run_started");
	let save = read(&dir.path().join("save.json"));
	assert_eq!(save["schemaVersion"], 2);
	assert_eq!(save["run"]["config"]["autoEquip"], false);
	assert_eq!(save["run"]["won"], false);
	assert_eq!(save["implementation"], "rust");
	assert_eq!(save["gameVersion"], "9.9.9");
	assert_eq!(save["session"]["runId"], session.info.run_id.as_str());
	assert_eq!(save["session"]["runId"], "20260927T120010Z-5");
	assert_eq!(save["run"]["phase"], "merchant");
	let text = fs::read_to_string(dir.path().join("save.json")).unwrap();
	assert!(text.starts_with("{\n\t\"schemaVersion\": 2,"), "tab-indented like the reference");
	session.step(&Command::buy_potion("health_potion", 1)).unwrap();
	let save = read(&dir.path().join("save.json"));
	assert_eq!(save["run"]["player"]["potions"]["health_potion"], 6);
}

#[test]
fn quit_mid_battle_resumes_from_last_merchant() {
	let data = common::data();
	let dir = TempDir::new();
	let clock = FakeClock::new();
	let (mut session, _) = GameSession::start(
		Rc::clone(&data),
		RunConfig::new("Alex", "warrior", "normal"),
		5,
		repositories(dir.path()),
		clock.clone(),
		"1",
	)
	.unwrap();
	session.step(&Command::NextFight).unwrap();
	let monster = session.state_mut().monster.as_mut().unwrap();
	monster.hp = 1_000_000;
	monster.max_hp = 1_000_000;
	session.step(&Command::Attack).unwrap();
	assert_eq!(session.state().phase, Phase::Battle);
	session.save_and_quit().unwrap();

	let resumed = GameSession::resume(Rc::clone(&data), repositories(dir.path()), clock, "1").unwrap().unwrap();
	assert_eq!(resumed.state().phase, Phase::Merchant);
	assert_eq!(resumed.state().round, 0);
	assert_eq!(resumed.info.sessions, 2);
	assert!(resumed.info.play_time_seconds > 0);
	assert_eq!(resumed.info.run_id, session.info.run_id);
}

#[test]
fn resume_without_save() {
	let data = common::data();
	let dir = TempDir::new();
	assert!(GameSession::resume(data, repositories(dir.path()), FakeClock::new(), "1").unwrap().is_none());
}

#[test]
fn death_writes_history_profile_and_deletes_save() {
	let data = common::data();
	let dir = TempDir::new();
	let repos = repositories(dir.path());
	let (mut session, _) = GameSession::start(
		Rc::clone(&data),
		RunConfig::new("Bot", "mage", "hard"),
		3,
		repos.clone(),
		FakeClock::new(),
		"1",
	)
	.unwrap();
	let bot = GreedyBot::new(&data);
	let mut unlocked = Vec::new();
	while session.state().phase != Phase::GameOver {
		let command = bot.choose(session.state());
		unlocked.extend(session.step(&command).unwrap().achievements.into_iter().map(|achievement| achievement.id));
	}
	assert!(!dir.path().join("save.json").exists());
	let records = repos.history.list().unwrap();
	assert_eq!(records.len(), 1);
	let record = &records[0];
	assert_eq!(record.round, session.state().round);
	assert_eq!(Some(record.death_cause.clone()), session.state().death_cause);
	assert_eq!(record.implementation, "rust");
	assert!(parse_timestamp(&record.ended_at).unwrap() > parse_timestamp(&record.started_at).unwrap());
	assert_eq!(session.finished_record.as_ref(), Some(record));
	let profile = repos.profile.load().unwrap();
	assert_eq!(profile.hall_of_fame[0].run_id, record.run_id);
	assert!(unlocked.contains(&"first_blood".to_owned()));
	assert!(profile.achievements.contains_key("first_blood"));
	assert_eq!(profile.bestiary.values().map(|entry| entry.kills).sum::<i64>(), session.state().round - 1);
	session.save_and_quit().unwrap();
	assert!(!dir.path().join("save.json").exists());
}

#[test]
fn newer_schema_is_refused() {
	let dir = TempDir::new();
	fs::write(dir.path().join("save.json"), r#"{"schemaVersion": 99}"#).unwrap();
	assert!(matches!(FileSaveRepository::new(dir.path()).load(), Err(PersistenceError::NewerSchema(_))));
	fs::write(dir.path().join("profile.json"), r#"{"schemaVersion": 99}"#).unwrap();
	assert!(matches!(FileProfileRepository::new(dir.path()).load(), Err(PersistenceError::NewerSchema(_))));
	fs::write(dir.path().join("settings.json"), r#"{"schemaVersion": 99}"#).unwrap();
	assert!(matches!(SettingsRepository::new(dir.path()).load(), Err(PersistenceError::NewerSchema(_))));
	fs::create_dir_all(dir.path().join("history")).unwrap();
	fs::write(dir.path().join("history").join("x.json"), r#"{"schemaVersion": 99}"#).unwrap();
	assert!(matches!(FileHistoryRepository::new(dir.path()).list(), Err(PersistenceError::NewerSchema(_))));
	let data = common::data();
	let result = GameSession::resume(data, repositories(dir.path()), FakeClock::new(), "1");
	assert!(matches!(result, Err(PersistenceError::NewerSchema(_))));
}

#[test]
fn corrupt_files_are_reported() {
	let dir = TempDir::new();
	fs::write(dir.path().join("save.json"), "{ nope").unwrap();
	assert!(matches!(FileSaveRepository::new(dir.path()).load(), Err(PersistenceError::Invalid(_))));
	fs::write(dir.path().join("profile.json"), r#"{"schemaVersion": 1, "bestiary": 3}"#).unwrap();
	let error = FileProfileRepository::new(dir.path()).load().unwrap_err();
	assert!(error.to_string().contains("profile.json"));
	let data = common::data();
	let start = GameSession::start(
		data,
		RunConfig::new("A", "mage", "easy"),
		1,
		repositories(dir.path()),
		FakeClock::new(),
		"1",
	);
	assert!(matches!(start, Err(SessionError::Persistence(_))));
	let data = common::data();
	let invalid = GameSession::start(
		data,
		RunConfig::new("A", "knight", "easy"),
		1,
		repositories(dir.path()),
		FakeClock::new(),
		"1",
	);
	assert!(matches!(&invalid, Err(SessionError::InvalidConfig(_))));
	assert!(invalid.err().unwrap().to_string().contains("knight"));
}

#[test]
fn settings_round_trip() {
	let dir = TempDir::new();
	let repo = SettingsRepository::new(dir.path());
	assert_eq!(repo.load().unwrap(), Settings::default());
	let custom = Settings { locale: Some("pt-BR".into()), auto_equip: true, battle_speed: 2 };
	repo.save(&custom).unwrap();
	assert_eq!(repo.load().unwrap(), custom);
	let saved: serde_json::Value =
		serde_json::from_str(&fs::read_to_string(dir.path().join("settings.json")).unwrap()).unwrap();
	assert_eq!(saved, serde_json::json!({"schemaVersion": 2, "locale": "pt-BR", "autoEquip": true, "battleSpeed": 2}));
	repo.save(&Settings::default()).unwrap();
	assert_eq!(repo.load().unwrap(), Settings::default());
	fs::write(dir.path().join("settings.json"), r#"{"schemaVersion": 1, "locale": "fr"}"#).unwrap();
	assert_eq!(repo.load().unwrap(), Settings::default());
	fs::write(dir.path().join("settings.json"), r#"{"schemaVersion": 1, "locale": "en"}"#).unwrap();
	assert_eq!(repo.load().unwrap(), Settings { locale: Some("en".into()), ..Settings::default() });
	fs::write(dir.path().join("settings.json"), r#"{"schemaVersion": 2, "autoEquip": true, "battleSpeed": 7}"#)
		.unwrap();
	assert_eq!(repo.load().unwrap(), Settings { locale: None, auto_equip: true, battle_speed: 1 });
	fs::write(dir.path().join("settings.json"), r#"{"schemaVersion": 2, "battleSpeed": 1}"#).unwrap();
	assert!(repo.load().is_err());
	fs::write(dir.path().join("settings.json"), r#"{"schemaVersion": 2, "autoEquip": false}"#).unwrap();
	assert!(repo.load().is_err());
}

#[test]
fn profile_round_trip_and_hall_of_fame_order() {
	let data = common::data();
	let mut service = ProfileService::new(Rc::clone(&data), Profile::default());
	for index in 0..12 {
		service.record_finished_run(HallOfFameEntry {
			run_id: format!("run{index}"),
			name: "A".into(),
			vocation: "mage".into(),
			difficulty: "normal".into(),
			round: index % 5,
			level: index,
			ended_at: format!("2026-01-{:02}T00:00:00Z", index + 1),
			won: false,
		});
	}
	let hall = &service.profile.hall_of_fame;
	assert_eq!(hall.len(), 10);
	assert_eq!(hall[..3].iter().map(|entry| entry.round).collect::<Vec<_>>(), [4, 4, 3]);
	assert!(hall[0].level > hall[1].level);
	service.record_finished_run(HallOfFameEntry {
		run_id: "winner".into(),
		name: "W".into(),
		vocation: "mage".into(),
		difficulty: "easy".into(),
		round: 1,
		level: 1,
		ended_at: "2026-02-01T00:00:00Z".into(),
		won: true,
	});
	assert_eq!(service.profile.hall_of_fame[0].run_id, "winner");
	service.profile.bestiary.insert("rat".into(), BestiaryEntry { kills: 4, first_killed_at: "x".into() });
	let dir = TempDir::new();
	let repo = FileProfileRepository::new(dir.path());
	repo.save(&service.profile).unwrap();
	assert_eq!(repo.load().unwrap(), service.profile);
	assert!(!service.revealed("rat"));
	assert!(!service.revealed("dragon"));
}

#[test]
fn history_lists_records_sorted_and_ignores_other_files() {
	let data = common::data();
	let dir = TempDir::new();
	let repos = repositories(dir.path());
	assert!(repos.history.list().unwrap().is_empty());
	let bot = GreedyBot::new(&data);
	for seed in [2, 1] {
		let (mut session, _) = GameSession::start(
			Rc::clone(&data),
			RunConfig::new("Bot", "warrior", "hard"),
			seed,
			repos.clone(),
			FakeClock::new(),
			"1",
		)
		.unwrap();
		while session.state().phase != Phase::GameOver {
			let command = bot.choose(session.state());
			session.step(&command).unwrap();
		}
	}
	fs::write(dir.path().join("history").join("notes.txt"), "ignored").unwrap();
	let records = repos.history.list().unwrap();
	assert_eq!(records.iter().map(|record| record.seed).collect::<Vec<_>>(), [1, 2]);
	repos.saves.delete().unwrap();
}

#[test]
fn achievements_progress_by_type() {
	let data = common::data();
	let dir = TempDir::new();
	let (mut session, _) = GameSession::start(
		Rc::clone(&data),
		RunConfig::new("Rich", "warrior", "hard"),
		8,
		repositories(dir.path()),
		FakeClock::new(),
		"1",
	)
	.unwrap();
	let state = session.state_mut();
	state.player.gold = 1_000_000;
	state.player.level = 200;
	state.round = 500;
	state.won = true;
	state.stats.items_dropped.insert("legendary".into(), 50);
	for spell in ["brutal_strike", "fierce_berserk", "annihilation", "wound_cleansing"] {
		state.player.spell_uses.insert(spell.into(), 1000);
	}
	let ids = data.monsters.iter().chain(&data.bosses).map(|creature| creature.id.clone());
	// Extra ids make `distinct_monsters` reach any threshold, whatever the size of the bestiary data.
	for id in ids.chain((0..200).map(|index| format!("future_monster_{index}"))) {
		session
			.profile
			.profile
			.bestiary
			.insert(id, BestiaryEntry { kills: 10_000, first_killed_at: "2026-01-01T00:00:00Z".into() });
	}
	let result = session.step(&Command::buy_potion("health_potion", 1)).unwrap();
	let unlocked: Vec<String> = result.achievements.into_iter().map(|achievement| achievement.id).collect();
	assert_eq!(unlocked.len(), data.achievements.len(), "every achievement type is reachable: {unlocked:?}");
}

#[test]
fn system_clock_is_close_to_now() {
	let before = std::time::SystemTime::now();
	assert!(SystemClock.now() >= before);
}
