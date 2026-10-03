//! M8 screens of the UI controller: settings, auto-equip step, auto-battle, victory and the equipment screen (port
//! of `tests/unit/test_controller_arpg.py`).

mod common;

use std::path::Path;
use std::rc::Rc;

use common::{TempDir, calm, make_controller, with_balance, with_test_items};
use rpg::application::profile::HallOfFameEntry;
use rpg::domain::character::item_score;
use rpg::domain::definitions::GameData;
use rpg::domain::entities::{AffixRoll, ItemInstance};
use rpg::domain::enums::{EnemyClass, Phase, Slot, Stat};
use rpg::infrastructure::repositories::Settings;
use rpg::presentation::controller::{AUTO_BATTLE_BASE_MS, Controller, View};
use rpg::presentation::render::{STYLE_DIM, STYLE_GAIN, STYLE_LOSS, STYLE_WARNING, format_delta};

fn controller(data: &Rc<GameData>, directory: &Path) -> Controller {
	make_controller(data, directory, Some("en"), 7)
}

fn start_run(controller: &mut Controller, vocation_key: &str, auto_equip_key: &str) {
	controller.press("2");
	controller.press("2");
	for character in "Zed".chars() {
		controller.press(&character.to_string());
	}
	controller.press("enter");
	controller.press(vocation_key);
	controller.press(auto_equip_key);
}

fn labels(controller: &Controller) -> Vec<String> {
	controller.options().into_iter().map(|option| option.label).collect()
}

fn keys(controller: &Controller) -> Vec<String> {
	controller.options().into_iter().map(|option| option.key).collect()
}

fn with_settings(auto_equip: bool, battle_speed: i64, locale: Option<&str>) -> Settings {
	Settings { locale: locale.map(str::to_owned), auto_equip, battle_speed }
}

#[test]
fn settings_toggle_and_persist() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = controller(&data, dir.path());
	controller.press("6");
	assert_eq!(controller.title(), "Settings");
	let labels = labels(&controller);
	assert_eq!(labels[0], "Language: English");
	assert_eq!(labels[1], "Auto-equip on new runs: Off");
	assert_eq!(labels[2], "Auto-battle speed: 1x");
	controller.press("2");
	controller.press("3");
	assert_eq!(controller.settings, with_settings(true, 2, None));
	assert_eq!(controller.auto_battle_interval_ms(), AUTO_BATTLE_BASE_MS / 2);
	assert_eq!(controller.services().settings.load().unwrap(), with_settings(true, 2, None));
	assert!(controller.options()[1].label.ends_with("On"));
	controller.press("3");
	assert_eq!(controller.settings.battle_speed, 1);
	controller.press("1");
	controller.press("1");
	assert_eq!(controller.view, View::Settings);
	assert_eq!(controller.services().settings.load().unwrap(), with_settings(true, 1, Some("en")));
	controller.press("0");
	assert_eq!(controller.view, View::Title);
}

#[test]
fn unknown_battle_speed_cycles_from_the_first() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = controller(&data, dir.path());
	controller.settings.battle_speed = 7;
	controller.press("6");
	controller.press("3");
	assert_eq!(controller.settings.battle_speed, 2);
}

#[test]
fn new_run_auto_equip_step_marks_the_default() {
	let data = common::data();
	let dir = TempDir::new();
	let first = controller(&data, dir.path());
	first.services().settings.save(&with_settings(true, 1, Some("en"))).unwrap();
	let mut controller = controller(&data, dir.path());
	start_run(&mut controller, "1", "0");
	assert_eq!(controller.view, View::Vocation);
	controller.press("1");
	assert_eq!(controller.view, View::AutoEquip);
	assert_eq!(controller.title(), controller.t("new_run.auto_equip", &[]));
	let labels = labels(&controller);
	assert!(labels[0].ends_with("(default)"));
	assert!(!labels[1].ends_with("(default)"));
	controller.press("1");
	assert_eq!(controller.view, View::Merchant);
	assert!(controller.session.as_ref().unwrap().state().config.auto_equip);
}

fn battle_controller(data: &Rc<GameData>, directory: &Path) -> Controller {
	let mut controller = controller(data, directory);
	start_run(&mut controller, "1", "2");
	controller.press("0");
	assert_eq!(controller.view, View::Battle);
	controller
}

#[test]
fn auto_battle_menu_and_instant_run() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = battle_controller(&data, dir.path());
	controller.press("5");
	assert_eq!(controller.view, View::AutoBattle);
	assert_eq!(controller.title(), controller.t("auto_battle.title", &[]));
	assert_eq!(keys(&controller), ["1", "2", "3", "0"]);
	controller.press("0");
	assert_eq!(controller.view, View::Battle);
	controller.press("5");
	controller.press("3");
	assert!(controller.auto_battle_active());
	let balanced = controller.t("auto_battle.balanced", &[]);
	assert_eq!(controller.log.back(), Some(&controller.t("auto_battle.started", &[("mode", &balanced)])));
	let turn = controller.session.as_ref().unwrap().state().turn;
	controller.press("1");
	assert_eq!(controller.session.as_ref().unwrap().state().turn, turn);
	controller.run_auto_battle();
	assert!(!controller.auto_battle_active());
	assert_ne!(controller.session.as_ref().unwrap().state().phase, Phase::Battle);
	assert!(matches!(controller.view, View::Merchant | View::GameOver));
	assert!(!controller.auto_battle_step());
}

#[test]
fn auto_battle_steps_one_turn_at_a_time() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = battle_controller(&data, dir.path());
	let monster = controller.session.as_mut().unwrap().state_mut().monster.as_mut().unwrap();
	monster.hp = 1_000_000;
	monster.max_hp = 1_000_000;
	controller.press("5");
	controller.press("1");
	let state = controller.session.as_mut().unwrap().state_mut();
	let turn = state.turn;
	state.player.hp = 1_000_000;
	assert!(controller.auto_battle_step());
	assert_eq!(controller.session.as_ref().unwrap().state().turn, turn + 1);
}

#[test]
fn auto_battle_stops_when_the_run_is_gone() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = battle_controller(&data, dir.path());
	controller.press("5");
	controller.press("2");
	controller.session = None;
	assert!(!controller.auto_battle_step());
	assert!(!controller.auto_battle_active());
}

fn victory_controller(data: &Rc<GameData>, directory: &Path) -> Controller {
	let mut controller = controller(&calm(data), directory);
	start_run(&mut controller, "1", "2");
	controller.session.as_mut().unwrap().state_mut().round = data.balance.final_round - 1;
	controller.press("0");
	for _ in 0..50 {
		let Some(monster) = controller.session.as_mut().unwrap().state_mut().monster.as_mut() else {
			break;
		};
		monster.hp = 1;
		controller.press("1");
	}
	assert_eq!(controller.view, View::Victory);
	controller
}

#[test]
fn victory_screen_end_run() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = victory_controller(&data, dir.path());
	assert_eq!(controller.title(), controller.t("victory.title", &[]));
	assert!(controller.body_lines()[0].contains("Ferumbras"));
	controller.press("9");
	assert_eq!(controller.view, View::Victory);
	controller.press("1");
	assert_eq!(controller.view, View::GameOver);
	assert_eq!(controller.title(), controller.t("gameover.title_won", &[]));
	assert!(controller.body_lines()[0].contains("won the run"));
	controller.press("2");
	controller.press("3");
	assert!(controller.body_lines()[0].contains("WON"));
}

#[test]
fn victory_screen_continue() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = victory_controller(&data, dir.path());
	controller.press("2");
	assert_eq!(controller.view, View::Merchant);
	assert!(controller.session.as_ref().unwrap().state().won);
}

#[test]
fn continue_saved_victory_returns_to_the_victory_screen() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = victory_controller(&data, dir.path());
	controller.session = None;
	controller.view = View::Title;
	controller.press("1");
	assert_eq!(controller.view, View::Victory);
}

fn equipment_controller(data: &Rc<GameData>, directory: &Path) -> Controller {
	let mut controller = controller(&with_test_items(data), directory);
	start_run(&mut controller, "1", "2");
	controller.press("3");
	assert_eq!(controller.view, View::Equipment);
	controller
}

#[test]
fn equipment_screen_lists_every_slot_and_the_bag() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = equipment_controller(&data, dir.path());
	let game_data = Rc::clone(&controller.services().data);
	let player = &mut controller.session.as_mut().unwrap().state_mut().player;
	player.bag.extend([
		ItemInstance::new(900, "test_axe", "rare", 0),
		ItemInstance::new(901, "test_rod", "common", 0),
		ItemInstance::new(902, "test_helmet", "legendary", 9),
	]);
	let starter = player.equipment[&Slot::Weapon].clone();
	let lines = controller.body_lines();
	let colors = controller.body_colors();
	assert_eq!(lines[0], format!("EQUIPPED · total score {}", item_score(&starter, &game_data)));
	assert!(lines[1].starts_with("Weapon: Sword [Common] · Lv 1"), "{}", lines[1]);
	assert_eq!(lines[2], "Shield: - empty -");
	assert_eq!(colors[2].as_deref(), Some(STYLE_WARNING));
	assert_eq!(lines.iter().filter(|line| line.contains("- empty -")).count(), 7);
	assert_eq!(lines.last().map(String::as_str), Some("BAG (usable)"));
	let options = controller.options();
	assert_eq!(keys(&controller), ["1", "2", "3", "0"]);
	let (axe, helmet, slot) = (&options[0], &options[1], &options[2]);
	let delta =
		item_score(&ItemInstance::new(900, "test_axe", "rare", 0), &game_data) - item_score(&starter, &game_data);
	assert_eq!(axe.detail, format_delta(delta));
	assert_eq!(axe.detail_color.as_deref(), Some(STYLE_GAIN));
	assert_eq!(axe.color.as_deref(), Some("rare"));
	assert_eq!(helmet.color.as_deref(), Some(STYLE_DIM));
	assert!(helmet.label.ends_with("requires Lv 37"), "{}", helmet.label);
	assert_eq!(slot.label, "Weapon: Sword [Common]");
}

#[test]
fn comparison_shows_stat_and_score_deltas() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = equipment_controller(&data, dir.path());
	let player = &mut controller.session.as_mut().unwrap().state_mut().player;
	let mut current = ItemInstance::new(800, "test_helmet", "common", 0);
	current.affixes = vec![AffixRoll { stat: Stat::Dodge, value: 3 }];
	player.equipment.insert(Slot::Helmet, current);
	let mut new = ItemInstance::new(801, "test_helmet", "rare", 0);
	new.affixes = vec![AffixRoll { stat: Stat::CritChance, value: 2 }];
	player.bag.push(new);
	controller.press("1");
	assert_eq!(controller.view, View::Compare);
	assert_eq!(controller.title(), "Helmet: Test Helmet → Test Helmet");
	let lines = controller.body_lines();
	let colors = controller.body_colors();
	let color_of = |text: &str| {
		let index = lines.iter().position(|line| line == text).unwrap_or_else(|| panic!("{text} in {lines:?}"));
		colors[index].as_deref()
	};
	assert_eq!(color_of("Armor: 10 → 15 (+5)"), Some(STYLE_GAIN));
	assert_eq!(color_of("Max HP: 50 → 75 (+25)"), Some(STYLE_GAIN));
	assert_eq!(color_of("Critical chance: 0 → 2 (+2)"), Some(STYLE_GAIN));
	assert_eq!(color_of("Dodge: 3 → 0 (-3)"), Some(STYLE_LOSS));
	assert_eq!(color_of("Affixes gained: +2 Critical chance"), Some(STYLE_GAIN));
	assert_eq!(color_of("Affixes lost: +3 Dodge"), Some(STYLE_LOSS));
	assert!(lines.iter().any(|line| line.starts_with("Score: ")));
	assert_eq!(keys(&controller), ["1", "0"]);
	controller.press("1");
	assert_eq!(controller.view, View::Equipment);
	assert_eq!(controller.session.as_ref().unwrap().state().player.equipment[&Slot::Helmet].uid, 801);
}

fn list_key_of(controller: &Controller, prefix: &str) -> String {
	controller.options().into_iter().find(|option| option.label.starts_with(prefix)).unwrap().key
}

#[test]
fn comparison_warns_about_the_required_level() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = equipment_controller(&data, dir.path());
	controller.session.as_mut().unwrap().state_mut().player.bag.push(ItemInstance::new(810, "test_axe", "common", 3));
	controller.press("1");
	assert_eq!(controller.body_colors().last().cloned().flatten().as_deref(), Some(STYLE_LOSS));
	assert_eq!(controller.body_lines().last().map(String::as_str), Some("Requires level 13 (you are level 1)."));
	controller.press("1");
	assert_eq!(controller.message, controller.t("error.level_too_low", &[]));
	assert_eq!(controller.view, View::Equipment);
	controller.press("0");
	controller.press("3");
	let key = list_key_of(&controller, "Weapon");
	controller.press(&key);
	assert_eq!(controller.view, View::EquippedSlot);
	assert_eq!(controller.title(), "Weapon");
	assert_eq!(controller.body_lines()[1], "Attack: 6");
	controller.press("1");
	assert_eq!(controller.view, View::Equipment);
	assert!(!controller.session.as_ref().unwrap().state().player.equipment.contains_key(&Slot::Weapon));
	controller.press("0");
	assert_eq!(controller.view, View::Merchant);
}

#[test]
fn compare_and_slot_views_survive_missing_items() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = equipment_controller(&data, dir.path());
	assert_eq!(controller.body_lines().last(), Some(&controller.t("equipment.bag_empty", &[])));
	controller.view = View::Compare;
	assert!(controller.body_lines().is_empty());
	assert_eq!(controller.title(), controller.t("merchant.equipment", &[]));
	controller.view = View::EquippedSlot;
	controller.press("0");
	assert_eq!(controller.view, View::Equipment);
	controller.view = View::EquippedSlot;
	controller.session.as_mut().unwrap().state_mut().player.equipment.clear();
	assert_eq!(controller.body_lines(), [controller.t("equipment.empty", &[])]);
	assert_eq!(controller.body_colors(), [Some(STYLE_WARNING.to_owned())]);
}

#[test]
fn monster_view_and_hall_of_fame_markers() {
	let data = common::data();
	let dir = TempDir::new();
	let elite_data = with_balance(&data, |balance| balance.elite_chance_pct = 100);
	let mut controller = controller(&elite_data, dir.path());
	let repository = Rc::clone(&controller.services().repositories.profile);
	let mut profile = repository.load().unwrap();
	profile.hall_of_fame.push(HallOfFameEntry {
		run_id: "r".into(),
		name: "Ana".into(),
		vocation: "mage".into(),
		difficulty: "hard".into(),
		round: 100,
		level: 50,
		ended_at: "2026-01-01T00:00:00Z".into(),
		won: true,
	});
	repository.save(&profile).unwrap();
	controller.press("3");
	assert!(controller.body_lines()[0].contains("WON"));
	assert_eq!(controller.body_colors(), vec![None; controller.body_lines().len()]);
	controller.press("0");
	start_run(&mut controller, "1", "2");
	controller.press("0");
	assert_eq!(controller.monster_view().unwrap().enemy_class, EnemyClass::Elite);
	assert!(controller.log.iter().any(|line| line.contains("ELITE")));
}
