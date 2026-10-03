//! The framework-independent UI controller and event texts (port of `tests/unit/test_controller.py` and
//! `tests/unit/test_event_text.py`).

mod common;

use std::rc::Rc;

use common::{TempDir, make_controller, warrior};
use rpg::application::commands::Command;
use rpg::application::events::Event;
use rpg::application::profile::BestiaryEntry;
use rpg::domain::entities::{ActiveStatus, ItemInstance};
use rpg::domain::enums::{Element, Slot};
use rpg::infrastructure::i18n::Translator;
use rpg::presentation::controller::{Controller, PAGE_SIZE, View};
use rpg::presentation::event_text::EventFormatter;
use rpg::presentation::render::list_key;
use serde_json::{Map, Value, json};

fn start_run(controller: &mut Controller, name: &str, vocation_key: &str) {
	controller.press("2");
	controller.press("2");
	for character in name.chars() {
		controller.press(&character.to_string());
	}
	controller.press("enter");
	controller.press(vocation_key);
}

fn labels(controller: &Controller) -> Vec<String> {
	controller.options().into_iter().map(|option| option.label).collect()
}

#[test]
fn name_validation_and_editing() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = make_controller(&data, dir.path(), Some("en"), 7);
	controller.press("2");
	controller.press("1");
	assert_eq!(controller.view, View::Name);
	controller.press("enter");
	assert_eq!(controller.message, controller.t("new_run.name_invalid", &[]));
	for character in "Abcdefghijklmnopqrstuvwxyz".chars() {
		controller.press(&character.to_string());
	}
	assert_eq!(controller.input_buffer, "Abcdefghijklmnop");
	controller.press("backspace");
	assert_eq!(controller.input_prompt().as_deref(), Some("> Abcdefghijklmno_"));
	controller.press("\u{7}");
	controller.press("f1");
	assert_eq!(controller.input_buffer, "Abcdefghijklmno");
	controller.press("escape");
	assert_eq!(controller.view, View::Difficulty);
	assert_eq!(controller.input_prompt(), None);
	controller.press("0");
	assert_eq!(controller.view, View::Title);
}

#[test]
fn title_without_save_has_no_continue() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = make_controller(&data, dir.path(), Some("en"), 7);
	assert!(!controller.options().iter().any(|option| option.key == "1"));
	controller.press("1");
	assert_eq!(controller.view, View::Title);
	assert_eq!(controller.header(), controller.t("app.subtitle", &[]));
	assert!(controller.monster_view().is_none());
	assert!(controller.player_view().is_none());
	controller.press("0");
	assert!(controller.exit_requested);
}

#[test]
fn first_launch_asks_for_the_language() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = make_controller(&data, dir.path(), None, 7);
	assert_eq!(controller.view, View::Language);
	assert_eq!(labels(&controller), ["English", "Português (Brasil)"]);
	controller.press("1");
	assert_eq!(controller.view, View::Title);
	let again = make_controller(&data, dir.path(), None, 7);
	assert_eq!(again.view, View::Title, "the choice is saved in settings.json");
	assert_eq!(again.locale, "en");
}

#[test]
fn language_switch_from_title() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = make_controller(&data, dir.path(), Some("en"), 7);
	controller.press("6");
	assert_eq!(controller.view, View::Language);
	controller.press("2");
	assert_eq!(controller.view, View::Title);
	assert_eq!(controller.locale, "pt-BR");
	assert_eq!(controller.title(), "CLI Turn-Based RPG");
	assert!(labels(&controller).contains(&"Sair".to_owned()));
}

#[test]
fn merchant_menus() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = make_controller(&data, dir.path(), Some("en"), 7);
	start_run(&mut controller, "Zed", "1");
	assert_eq!(controller.title(), controller.t("merchant.title_start", &[]));
	assert!(controller.body_lines()[0].contains("Zed"));
	controller.press("2");
	assert_eq!(controller.body_lines(), [controller.t("merchant.empty_bag", &[])]);
	controller.press("0");
	let bag = &mut controller.session.as_mut().unwrap().state_mut().player.bag;
	bag.push(ItemInstance::new(900, "hand_axe", "rare", 0));
	bag.push(ItemInstance::new(901, "bow", "common", 0));
	controller.press("3");
	let equipment = labels(&controller);
	assert!(equipment.iter().any(|label| label.contains("Hand Axe")));
	assert!(!equipment.iter().any(|label| label.contains("Bow")));
	assert_eq!(controller.options()[0].color.as_deref(), Some("rare"));
	controller.press("1");
	let weapon_uid = controller.session.as_ref().unwrap().state().player.equipment[&Slot::Weapon].uid;
	assert_eq!(weapon_uid, 900);
	controller.press("0");
	controller.press("2");
	let sell_keys: Vec<String> = controller.options().into_iter().map(|option| option.key).collect();
	assert_eq!(sell_keys.last().map(String::as_str), Some("0"));
	assert!(controller.body_lines().is_empty());
	controller.press(&sell_keys[0]);
	controller.press("0");
	controller.press("4");
	let stock = controller.session.as_ref().unwrap().state().merchant_stock.len();
	assert_eq!(controller.options().len(), stock + 1);
	controller.session.as_mut().unwrap().state_mut().player.gold = 0;
	controller.press("1");
	assert_eq!(controller.message, controller.t("error.not_enough_gold", &[]));
	controller.press("0");
	controller.press("1");
	controller.press("1");
	assert_eq!(controller.view, View::Quantity);
	assert!(controller.title().contains("Health Potion"));
	controller.press("x");
	controller.press("1");
	controller.press("2");
	controller.press("3");
	assert_eq!(controller.input_buffer, "12");
	controller.press("escape");
	assert_eq!(controller.view, View::BuyPotions);
	controller.press("1");
	controller.press("enter");
	assert_eq!(controller.view, View::BuyPotions);
	controller.press("0");
	controller.press("5");
	assert_eq!(controller.view, View::Character);
	assert!(controller.body_lines().iter().any(|line| line.contains("Equipment")));
	controller.press("n");
	assert!(controller.body_lines().last().unwrap().starts_with("Page 2/"));
	controller.press("0");
	controller.session.as_mut().unwrap().state_mut().merchant_stock.clear();
	controller.press("4");
	assert_eq!(controller.body_lines(), [controller.t("merchant.empty_stock", &[])]);
}

#[test]
fn buying_potions_through_the_quantity_prompt() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = make_controller(&data, dir.path(), Some("en"), 7);
	start_run(&mut controller, "Zed", "2");
	controller.press("1");
	assert_eq!(controller.view, View::BuyPotions);
	assert_eq!(controller.options().len(), 3);
	controller.press("1");
	controller.press("1");
	controller.press("enter");
	assert_eq!(controller.view, View::BuyPotions);
	assert_eq!(controller.session.as_ref().unwrap().state().player.potion_count("health_potion"), 6);
	assert!(controller.log.back().unwrap().contains("Health Potion"));
}

#[test]
fn battle_submenus_and_messages() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = make_controller(&data, dir.path(), Some("en"), 7);
	start_run(&mut controller, "Zed", "3");
	controller.press("0");
	assert_eq!(controller.view, View::Battle);
	assert!(controller.header().contains("Seed 7"));
	let monster = controller.monster_view().expect("a monster");
	assert!(!monster.details.is_empty());
	assert!(controller.player_view().is_some());
	let session = controller.session.as_mut().unwrap();
	session.state_mut().player.potions.clear();
	controller.press("3");
	assert_eq!(controller.body_lines(), [controller.t("battle.no_potions", &[])]);
	controller.press("0");
	controller.session.as_mut().unwrap().state_mut().player.mp = 0;
	controller.press("2");
	assert_eq!(controller.options().last().unwrap().key, "0");
	controller.press(&list_key(0));
	assert_eq!(controller.message, controller.t("error.not_enough_mana", &[]));
	controller.press("escape");
	controller.press("4");
	assert!(controller.animation_cues.is_empty() || controller.animation_cues == ["attack"]);
	controller.press("q");
	assert_eq!(controller.view, View::Title);
	assert!(controller.session.is_none());
	assert!(labels(&controller).iter().any(|label| label.starts_with("Continue")));
}

#[test]
fn monster_and_player_views_show_statuses_and_weaknesses() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = make_controller(&data, dir.path(), Some("en"), 7);
	start_run(&mut controller, "Zed", "1");
	controller.press("0");
	let state = controller.session.as_mut().unwrap().state_mut();
	state.player.statuses.push(ActiveStatus::new("poison", 3, 2));
	let monster = state.monster.as_mut().unwrap();
	monster.creature_id = "fire_elemental".into();
	monster.statuses.push(ActiveStatus::new("bleed", 2, 1));
	let view = controller.monster_view().unwrap();
	assert_eq!(view.name, "Fire Elemental");
	assert!(view.details.contains("weak"), "{}", view.details);
	assert!(view.details.ends_with("(2)"), "{}", view.details);
	assert!(controller.player_view().unwrap().statuses.ends_with("(3)"));
	let element = data.creature("fire_elemental").resistance(Element::Ice);
	assert!(element > 100);
}

#[test]
fn bestiary_paging_and_reveal() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = make_controller(&data, dir.path(), Some("en"), 7);
	let repository = Rc::clone(&controller.services().repositories.profile);
	let mut profile = repository.load().unwrap();
	profile.bestiary.insert("rat".into(), BestiaryEntry { kills: 9, first_killed_at: "2026-01-01T00:00:00Z".into() });
	profile.bestiary.insert("bat".into(), BestiaryEntry { kills: 1, first_killed_at: "2026-01-01T00:00:00Z".into() });
	repository.save(&profile).unwrap();
	controller.press("4");
	let lines = controller.body_lines();
	assert_eq!(lines.len(), PAGE_SIZE + 2);
	assert!(lines.iter().any(|line| line.starts_with("Rat") && line.contains("weak")));
	assert!(lines.iter().any(|line| line.starts_with("Bat") && !line.contains("weak")));
	controller.press("n");
	assert_ne!(controller.body_lines(), lines);
	for _ in 0..50 {
		controller.press("n");
	}
	let pages = (data.monsters.len() + data.bosses.len()).div_ceil(PAGE_SIZE);
	assert!(controller.body_lines().last().unwrap().starts_with(&format!("Page {pages}/{pages}")));
	controller.press("p");
	controller.press("0");
	controller.press("3");
	assert_eq!(controller.body_lines(), [controller.t("hall.empty", &[])]);
	controller.press("0");
	controller.press("5");
	assert!(controller.body_lines()[..PAGE_SIZE].iter().all(|line| line.starts_with("[ ]")));
}

#[test]
fn game_over_screen_and_new_run() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = make_controller(&data, dir.path(), Some("en"), 7);
	start_run(&mut controller, "Zed", "1");
	controller.press("0");
	let monster = controller.session.as_mut().unwrap().state_mut().monster.as_mut().unwrap();
	monster.attacks[0].min = 100_000;
	monster.attacks[0].max = 100_000;
	monster.attacks.truncate(1);
	monster.hp = 100_000;
	while controller.view != View::GameOver {
		controller.press("1");
	}
	assert_eq!(controller.title(), "GAME OVER");
	let body = controller.body_lines();
	assert!(body[0].contains("Zed"));
	controller.press("2");
	assert_eq!(controller.view, View::Title);
	assert!(!labels(&controller).iter().any(|label| label.starts_with("Continue")));
	controller.press("3");
	assert!(controller.body_lines()[0].contains("Zed"));
	controller.press("0");
	controller.press("2");
	assert_eq!(controller.view, View::Difficulty);
	assert!(controller.session.is_none());
}

#[test]
fn continue_resumes_the_saved_run() {
	let data = common::data();
	let dir = TempDir::new();
	let mut controller = make_controller(&data, dir.path(), Some("en"), 7);
	start_run(&mut controller, "Zed", "1");
	controller.press("q");
	let mut fresh = make_controller(&data, dir.path(), Some("en"), 7);
	fresh.press("1");
	assert_eq!(fresh.view, View::Merchant);
	assert_eq!(fresh.session.as_ref().unwrap().info.sessions, 2);
	assert!(fresh.log.back().unwrap().contains("Zed"));
}

#[test]
fn persistence_errors_are_shown_instead_of_crashing() {
	let data = common::data();
	let dir = TempDir::new();
	std::fs::write(dir.path().join("profile.json"), r#"{"schemaVersion": 99}"#).unwrap();
	let mut controller = make_controller(&data, dir.path(), Some("en"), 7);
	start_run(&mut controller, "Zed", "1");
	assert_eq!(controller.view, View::Vocation);
	assert!(controller.error.as_deref().is_some_and(|error| error.contains("update the game")));
	std::fs::write(dir.path().join("settings.json"), r#"{"schemaVersion": 99}"#).unwrap();
	assert!(Controller::new(common::services(&data, dir.path()), None, None, None).is_err());
}

#[test]
fn random_seed_is_used_without_a_fixed_seed() {
	let data = common::data();
	let dir = TempDir::new();
	let source: Box<dyn Fn() -> u64> = Box::new(|| 31_337);
	let mut controller = Controller::new(common::services(&data, dir.path()), None, Some("en"), Some(source)).unwrap();
	start_run(&mut controller, "Zed", "1");
	assert_eq!(controller.session.as_ref().unwrap().state().seed, 31_337);
	assert!(u32::try_from(rpg::presentation::controller::random_seed()).is_ok());
}

// ── event texts ───────────────────────────────────────────────────────────────

fn formatter() -> EventFormatter {
	let data = common::data();
	let translator = Rc::new(Translator::new(&common::shared(), "en").unwrap());
	EventFormatter::new(data, translator)
}

fn object(value: &Value) -> Map<String, Value> {
	value.as_object().unwrap().clone()
}

#[test]
fn format_events() {
	let data = common::data();
	let engine = warrior(&data);
	let formatter = formatter();
	let cases = [
		(
			json!({"type": "player_attacked", "damage": 12, "crit": false, "element": "fire"}),
			"You hit for 12 fire damage.",
		),
		(
			json!({"type": "player_attacked", "damage": 30, "crit": true, "element": "physical"}),
			"CRITICAL! You hit for 30 physical damage.",
		),
		(
			json!({"type": "spell_cast", "spellId": "flame_strike", "damage": 9, "crit": false, "element": "fire", "mana": 20}),
			"Flame Strike deals 9 fire damage.",
		),
		(
			json!({"type": "potion_used", "potionId": "mana_potion", "amount": 80, "resource": "mp"}),
			"Mana Potion restores 80 MP.",
		),
		(
			json!({"type": "status_applied", "target": "player", "status": "burn", "turns": 3, "perTurn": 2}),
			"You are burning (3 turns).",
		),
		(json!({"type": "monster_killed", "monsterId": "dragon", "isBoss": false}), "You defeated Dragon!"),
		(
			json!({"type": "round_started", "round": 10, "tier": 0, "cycle": 0, "monsterId": "munster", "isBoss": true, "hp": 5}),
			"Round 10: the boss Munster challenges you! (5 HP)",
		),
		(json!({"type": "item_sold", "uid": 3, "itemId": "sword", "gold": 25}), "You sold Sword for 25 gold."),
		(json!({"type": "error", "code": "not_enough_mana"}), "Not enough mana."),
	];
	for (event, expected) in cases {
		assert_eq!(formatter.format_fields(&object(&event), engine.state()), expected, "{event}");
		let typed: Event = serde_json::from_value(event).unwrap();
		assert_eq!(formatter.format(&typed, engine.state()), expected);
	}
	let ghost = json!({"type": "spell_cast", "spellId": "ghost_spell", "damage": 1, "crit": false, "element": "fire", "mana": 1});
	assert!(formatter.format_fields(&object(&ghost), engine.state()).contains("ghost_spell"));
}

#[test]
fn every_event_type_has_a_text() {
	let data = common::data();
	let translator = Translator::new(&common::shared(), "en").unwrap();
	let mut engine = warrior(&data);
	let formatter = formatter();
	let mut seen = std::collections::BTreeSet::new();
	let bot = rpg::application::bot::GreedyBot::new(&data);
	while engine.state().phase != rpg::domain::enums::Phase::GameOver {
		for event in engine.step(&bot.choose(engine.state())) {
			let text = formatter.format(&event, engine.state());
			assert!(!text.starts_with("event."), "missing text for {event:?}");
			seen.insert(event.kind());
		}
	}
	assert!(seen.len() > 15, "{seen:?}");
	assert!(translator.has("event.boss_telegraph"));
}

#[test]
fn monster_name_comes_from_state() {
	let data = common::data();
	let mut engine = warrior(&data);
	engine.step(&Command::NextFight);
	let name = &data.creature(&engine.state().monster.as_ref().unwrap().creature_id).name;
	let text = formatter().format(&Event::MonsterStunned, engine.state());
	assert!(text.starts_with(name.as_str()), "{text}");
}

#[test]
fn item_name_by_uid() {
	let data = common::data();
	let mut engine = warrior(&data);
	engine.state_mut().player.bag.push(ItemInstance::new(77, "bow", "rare", 0));
	let formatter = formatter();
	let dropped = json!({"type": "item_dropped", "uid": 77, "itemId": "bow", "rarity": "rare"});
	assert_eq!(formatter.format_fields(&object(&dropped), engine.state()), "Loot: Bow [Rare]!");
	let partial = json!({"type": "item_equipped", "uid": 999, "slot": "ring"});
	assert!(formatter.format_fields(&object(&partial), engine.state()).contains("#999"));
	let by_uid = json!({"type": "item_equipped", "uid": 77, "slot": "ring"});
	assert!(formatter.format_fields(&object(&by_uid), engine.state()).contains("Bow"));
}
