//! End-to-end tests: the real ratatui app driven by key events and rendered to a `TestBackend` (no animation,
//! fixed seed, temp data dir) — the counterpart of Textual's Pilot, ink-testing-library and teatest.

mod common;

use std::fs;
use std::path::Path;

use common::TempDir;
use crossterm::event::{KeyCode, KeyEvent, KeyEventKind, KeyModifiers};
use ratatui::Terminal;
use ratatui::backend::TestBackend;
use rpg::application::bot::GreedyBot;
use rpg::application::commands::Command;
use rpg::application::loot::can_use;
use rpg::application::merchant::available_potions;
use rpg::assets::SharedFs;
use rpg::domain::enums::Phase;
use rpg::infrastructure::art::ArtLibrary;
use rpg::presentation::controller::{Controller, View};
use rpg::presentation::render::list_key;
use rpg::presentation::tui::app::{App, key_name, option_style, screen_text};

const SIZE: (u16, u16) = (100, 30);

struct Harness {
	app: App,
	terminal: Terminal<TestBackend>,
	exited: bool,
}

impl Harness {
	fn new(directory: &Path, lang: Option<&str>) -> Harness {
		Harness::sized(directory, lang, SIZE, false)
	}

	fn sized(directory: &Path, lang: Option<&str>, size: (u16, u16), animate: bool) -> Harness {
		let data = common::data();
		let controller = common::make_controller(&data, directory, lang, 42);
		let app = App::new(controller, ArtLibrary::new(SharedFs::embedded()), animate);
		let terminal = Terminal::new(TestBackend::new(size.0, size.1)).expect("test terminal");
		let mut harness = Harness { app, terminal, exited: false };
		harness.screen();
		harness
	}

	fn controller(&self) -> &Controller {
		&self.app.controller
	}

	fn view(&self) -> View {
		self.app.controller.view
	}

	/// Presses keys by name (`"1"`, `"enter"`, `"escape"`…) and redraws after each one, like a real terminal.
	fn press(&mut self, keys: &[&str]) {
		for key in keys {
			let code = match *key {
				"enter" => KeyCode::Enter,
				"escape" => KeyCode::Esc,
				"backspace" => KeyCode::Backspace,
				other => KeyCode::Char(other.chars().next().expect("a key")),
			};
			self.exited |= self.app.on_key(&KeyEvent::new(code, KeyModifiers::NONE));
			self.screen();
		}
	}

	fn type_text(&mut self, text: &str) {
		for character in text.chars() {
			self.press(&[&character.to_string()]);
		}
	}

	fn screen(&mut self) -> String {
		self.terminal.draw(|frame| self.app.render(frame)).expect("draw");
		screen_text(self.terminal.backend().buffer())
	}
}

#[test]
fn first_launch_language_then_new_run_flow() {
	let dir = TempDir::new();
	let mut ui = Harness::new(dir.path(), None);
	assert_eq!(ui.view(), View::Language);
	assert!(ui.screen().contains("Português (Brasil)"));
	ui.press(&["2"]);
	assert_eq!(ui.view(), View::Title);
	assert!(ui.screen().contains("Nova jornada"));
	ui.press(&["2"]);
	assert_eq!(ui.view(), View::Difficulty);
	ui.press(&["3"]);
	ui.type_text("Ana");
	assert!(ui.screen().contains("> Ana_"));
	ui.press(&["enter"]);
	assert_eq!(ui.view(), View::Vocation);
	ui.press(&["3"]);
	assert_eq!(ui.view(), View::Merchant);
	let text = ui.screen();
	assert!(text.contains("Ana"));
	assert!(text.contains("Mago"));
	let settings: serde_json::Value =
		serde_json::from_str(&fs::read_to_string(dir.path().join("settings.json")).unwrap()).unwrap();
	assert_eq!(settings["locale"], "pt-BR");
	assert!(dir.path().join("save.json").exists());
}

#[test]
fn battle_merchant_save_quit_and_continue() {
	let dir = TempDir::new();
	let mut ui = Harness::new(dir.path(), Some("en"));
	assert!(ui.screen().contains(&format!("v{} · Rust", rpg::version::VERSION)));
	ui.press(&["2", "2"]);
	ui.type_text("Bo");
	ui.press(&["enter", "1"]);
	assert_eq!(ui.view(), View::Merchant);
	ui.press(&["1"]);
	assert_eq!(ui.view(), View::BuyPotions);
	ui.press(&["1"]);
	assert_eq!(ui.view(), View::Quantity);
	ui.press(&["1", "enter"]);
	assert_eq!(ui.controller().session.as_ref().unwrap().state().player.potion_count("health_potion"), 6);
	ui.press(&["0", "5"]);
	assert!(ui.screen().contains("Equipment"));
	ui.press(&["0", "0"]);
	assert_eq!(ui.view(), View::Battle);
	let battle = ui.screen();
	assert!(battle.contains("HP"));
	assert!(battle.contains("Round 1"));
	assert!(battle.contains("Seed 42"));
	ui.press(&["1", "2"]);
	assert_eq!(ui.view(), View::Spells);
	ui.press(&[&list_key(0), "3"]);
	assert_eq!(ui.view(), View::Potions);
	ui.press(&["escape"]);
	assert_eq!(ui.view(), View::Battle);
	ui.press(&["q"]);
	assert_eq!(ui.view(), View::Title);
	assert!(ui.screen().contains("Continue"));
	ui.press(&["1"]);
	assert_eq!(ui.view(), View::Merchant);
	assert_eq!(ui.controller().session.as_ref().unwrap().info.sessions, 2);
	assert!(!ui.exited);
}

#[test]
fn full_run_until_game_over() {
	let dir = TempDir::new();
	let data = common::data();
	let bot = GreedyBot::new(&data);
	let mut ui = Harness::new(dir.path(), Some("en"));
	ui.press(&["2", "3"]);
	ui.type_text("Hero");
	ui.press(&["enter", "1"]);
	for _ in 0..5000 {
		let state = ui.controller().session.as_ref().expect("a run").state();
		if state.phase == Phase::GameOver {
			break;
		}
		let keys = keys_for(&bot.choose(state), ui.controller());
		let keys: Vec<&str> = keys.iter().map(String::as_str).collect();
		ui.press(&keys);
	}
	assert_eq!(ui.view(), View::GameOver);
	assert!(ui.screen().contains("GAME OVER"));
	ui.press(&["2", "3"]);
	assert!(ui.screen().contains("Hero"));
	ui.press(&["0", "5"]);
	assert!(ui.screen().contains("[x] First Blood"));
	ui.press(&["0", "4"]);
	assert_eq!(ui.view(), View::Bestiary);
	ui.press(&["n", "p", "0", "0"]);
	assert!(ui.exited);
	assert!(!dir.path().join("save.json").exists());
	assert_eq!(fs::read_dir(dir.path().join("history")).unwrap().count(), 1);
}

/// The keys a player presses to issue the bot's command through the menus.
fn keys_for(command: &Command, controller: &Controller) -> Vec<String> {
	let session = controller.session.as_ref().unwrap();
	let state = session.state();
	let data = controller.services().data.as_ref();
	let index_of = |items: Vec<String>, wanted: &str| items.iter().position(|item| item == wanted).unwrap();
	match command {
		Command::Attack => vec!["1".into()],
		Command::Defend => vec!["4".into()],
		Command::Cast { spell_id } => {
			let spells = data.vocation(&state.player.vocation_id).spells.clone();
			vec!["2".into(), list_key(index_of(spells, spell_id))]
		}
		Command::UsePotion { potion_id } => {
			let owned: Vec<String> = data
				.potions
				.iter()
				.filter(|potion| state.player.potion_count(&potion.id) > 0)
				.map(|potion| potion.id.clone())
				.collect();
			vec!["3".into(), list_key(index_of(owned, potion_id))]
		}
		Command::NextFight => vec!["0".into()],
		Command::BuyPotion { potion_id, quantity } => {
			let index = index_of(available_potions(state, data), potion_id);
			let mut keys = vec!["1".into(), list_key(index)];
			keys.extend(quantity.to_string().chars().map(String::from));
			keys.extend(["enter".into(), "0".into()]);
			keys
		}
		Command::SellItem { uid } => {
			let index = state.player.bag.iter().position(|item| item.uid == *uid).unwrap();
			vec!["2".into(), list_key(index), "0".into()]
		}
		Command::Equip { uid } => {
			let vocation = data.vocation(&state.player.vocation_id);
			let usable: Vec<i64> = state
				.player
				.bag
				.iter()
				.filter(|item| can_use(data.item(&item.item_id), vocation))
				.map(|item| item.uid)
				.collect();
			let index = usable.iter().position(|candidate| candidate == uid).unwrap();
			vec!["3".into(), list_key(index), "0".into()]
		}
		Command::BuyStockItem { index } => vec!["4".into(), list_key(*index as usize), "0".into()],
		Command::Unequip { .. } => panic!("the bot never unequips"),
	}
}

#[test]
fn small_terminals_show_the_resize_message() {
	let dir = TempDir::new();
	let mut ui = Harness::sized(dir.path(), Some("en"), (99, 30), false);
	assert!(ui.screen().contains("Please resize your terminal to at least 100 x 30."));
	let mut ui = Harness::sized(dir.path(), Some("en"), (120, 40), false);
	assert!(ui.screen().contains("New run"));
}

#[test]
fn paged_views_hide_the_combat_log_and_use_two_columns() {
	let dir = TempDir::new();
	let mut ui = Harness::new(dir.path(), Some("en"));
	ui.press(&["2", "2"]);
	ui.type_text("Cy");
	ui.press(&["enter", "2"]);
	let merchant = ui.screen();
	let row = merchant.lines().find(|line| line.contains("[1]")).unwrap();
	assert!(row.contains("[2]"), "merchant options use two columns: {row}");
	assert!(merchant.contains(ui.controller().log.back().unwrap().as_str()), "the log panel shows the newest line");
	ui.press(&["5"]);
	let character = ui.screen();
	assert!(character.contains("Page 1/"));
	assert!(!character.contains(ui.controller().log.back().unwrap().as_str()));
}

#[test]
fn keys_ctrl_c_release_events_and_animation_ticks() {
	let dir = TempDir::new();
	let mut ui = Harness::sized(dir.path(), Some("en"), SIZE, true);
	let release = KeyEvent { kind: KeyEventKind::Release, ..KeyEvent::new(KeyCode::Char('0'), KeyModifiers::NONE) };
	assert!(!ui.app.on_key(&release));
	assert!(!ui.app.on_key(&KeyEvent::new(KeyCode::F(1), KeyModifiers::NONE)));
	assert!(ui.app.on_key(&KeyEvent::new(KeyCode::Char('c'), KeyModifiers::CONTROL)));
	let title_frame = ui.screen();
	ui.app.on_tick();
	assert_ne!(ui.screen(), title_frame, "the title art animates");
	ui.press(&["2", "2"]);
	ui.type_text("Di");
	ui.press(&["enter", "1", "0"]);
	for _ in 0..20 {
		ui.press(&["4"]);
		ui.app.on_tick();
		ui.screen();
	}
	assert_eq!(key_name(&KeyEvent::new(KeyCode::Delete, KeyModifiers::NONE)).as_deref(), Some("backspace"));
	assert_eq!(key_name(&KeyEvent::new(KeyCode::Tab, KeyModifiers::NONE)), None);
	assert_eq!(option_style(Some("fire")), option_style(Some("red")));
	assert_ne!(option_style(Some("legendary")), option_style(None));
}
