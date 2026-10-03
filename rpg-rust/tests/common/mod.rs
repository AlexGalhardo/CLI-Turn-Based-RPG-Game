//! Shared fixtures (the Rust counterpart of the reference `conftest.py`).
//!
//! Every integration test file compiles this module separately, so not every helper is used everywhere.
#![allow(dead_code)]

use std::cell::Cell;
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use std::{fs, process};

use rpg::application::commands::Command;
use rpg::application::engine::GameEngine;
use rpg::application::events::Event;
use rpg::application::game_session::Repositories;
use rpg::application::ports::Clock;
use rpg::application::run_state::RunConfig;
use rpg::assets::SharedFs;
use rpg::domain::definitions::{GameData, ItemDef};
use rpg::domain::entities::MonsterInstance;
use rpg::domain::enums::{Slot, Stat};
use rpg::infrastructure::data_loader::load_game_data;
use rpg::infrastructure::repositories::{
	FileHistoryRepository, FileProfileRepository, FileSaveRepository, SettingsRepository, SystemClock,
};
use rpg::presentation::controller::{Controller, Services};
use rpg::version::VERSION;

pub fn shared() -> Rc<SharedFs> {
	SharedFs::embedded()
}

pub fn data() -> Rc<GameData> {
	Rc::new(load_game_data(&shared()).expect("shared data loads"))
}

/// `../shared` on disk (golden files are read at runtime, not embedded).
pub fn shared_dir() -> PathBuf {
	Path::new(env!("CARGO_MANIFEST_DIR")).join("..").join("shared")
}

pub fn new_engine(data: &Rc<GameData>, vocation: &str, difficulty: &str, seed: u64) -> GameEngine {
	GameEngine::new_run(Rc::clone(data), RunConfig::new("Tester", vocation, difficulty), seed).expect("valid config").0
}

pub fn warrior(data: &Rc<GameData>) -> GameEngine {
	new_engine(data, "warrior", "normal", 42)
}

fn test_item(id: &str, name: &str, slot: Slot, item_type: &str, stats: &[(Stat, i64)]) -> ItemDef {
	ItemDef {
		id: id.to_owned(),
		name: name.to_owned(),
		slot,
		item_type: item_type.to_owned(),
		tier: 0,
		element: None,
		stats: stats.iter().copied().collect::<BTreeMap<_, _>>(),
		value: 100,
	}
}

/// Adds deterministic test items (one per slot with every stat) without touching the shared files.
pub fn with_test_items(data: &GameData) -> Rc<GameData> {
	let mut content = data.content();
	content.items.extend([
		test_item("test_helmet", "Test Helmet", Slot::Helmet, "helmet", &[(Stat::Armor, 10), (Stat::MaxHp, 50)]),
		test_item("test_ring", "Test Ring", Slot::Ring, "ring", &[(Stat::CritChance, 80), (Stat::Dodge, 90)]),
		test_item("test_axe", "Test Axe", Slot::Weapon, "axe", &[(Stat::Attack, 20)]),
		test_item("test_rod", "Test Rod", Slot::Weapon, "rod", &[(Stat::Attack, 1)]),
	]);
	Rc::new(GameData::new(content))
}

pub fn types(events: &[Event]) -> Vec<String> {
	events.iter().map(Event::kind).collect()
}

/// Starts the first fight and returns the spawned monster.
pub fn fight(engine: &mut GameEngine) -> &mut MonsterInstance {
	engine.step(&Command::NextFight);
	engine.state_mut().monster.as_mut().expect("a monster was spawned")
}

/// A temporary directory removed on drop. Tests never touch the real save directory.
pub struct TempDir(PathBuf);

static COUNTER: AtomicUsize = AtomicUsize::new(0);

impl TempDir {
	pub fn new() -> TempDir {
		let nanos = SystemTime::now().duration_since(UNIX_EPOCH).map_or(0, |elapsed| elapsed.as_nanos());
		let unique = COUNTER.fetch_add(1, Ordering::SeqCst);
		let path = std::env::temp_dir().join(format!("rpg-rust-test-{}-{nanos}-{unique}", process::id()));
		fs::create_dir_all(&path).expect("create temp dir");
		TempDir(path)
	}

	pub fn path(&self) -> &Path {
		&self.0
	}
}

impl Drop for TempDir {
	fn drop(&mut self) {
		let _ = fs::remove_dir_all(&self.0);
	}
}

/// A clock that advances 10 seconds on every call, starting at 2026-09-27T12:00:00Z (497 364 h after the epoch).
pub struct FakeClock {
	current: Cell<SystemTime>,
}

impl FakeClock {
	pub fn new() -> Rc<FakeClock> {
		Rc::new(FakeClock { current: Cell::new(UNIX_EPOCH + Duration::from_hours(497_364)) })
	}
}

impl Clock for FakeClock {
	fn now(&self) -> SystemTime {
		let next = self.current.get() + Duration::from_secs(10);
		self.current.set(next);
		next
	}
}

pub fn repositories(directory: &Path) -> Repositories {
	Repositories {
		saves: Rc::new(FileSaveRepository::new(directory)),
		history: Rc::new(FileHistoryRepository::new(directory)),
		profile: Rc::new(FileProfileRepository::new(directory)),
	}
}

pub fn services(data: &Rc<GameData>, directory: &Path) -> Services {
	Services {
		data: Rc::clone(data),
		shared: shared(),
		settings: SettingsRepository::new(directory),
		repositories: repositories(directory),
		clock: Rc::new(SystemClock),
		version: VERSION.to_owned(),
	}
}

pub fn make_controller(data: &Rc<GameData>, directory: &Path, lang: Option<&str>, seed: u64) -> Controller {
	Controller::new(services(data, directory), Some(seed), lang, None).expect("controller")
}
