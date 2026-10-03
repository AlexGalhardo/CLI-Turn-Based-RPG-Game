//! Auto-equip with auto-sell (port of `tests/unit/test_auto_equip.py`, docs/game-design.md §8.1).

mod common;

use std::rc::Rc;

use common::{ClassOverrides, calm, types, with_enemy_class, with_test_items};
use rpg::application::auto_equip::auto_equip;
use rpg::application::commands::Command;
use rpg::application::engine::GameEngine;
use rpg::application::events::Event;
use rpg::application::run_state::RunConfig;
use rpg::domain::character::{item_score, item_value};
use rpg::domain::definitions::GameData;
use rpg::domain::entities::ItemInstance;
use rpg::domain::enums::{EnemyClass, Phase, Slot};

fn engine(data: &Rc<GameData>, auto: bool) -> GameEngine {
	let config = RunConfig::new("Auto", "warrior", "normal").with_auto_equip(auto);
	GameEngine::new_run(Rc::clone(data), config, 42).unwrap().0
}

#[test]
fn better_item_is_equipped_and_the_old_one_sold() {
	let data = with_test_items(&common::data());
	let mut engine = engine(&data, true);
	let rng_state = engine.rng_state();
	let state = engine.state_mut();
	let starter = state.player.equipment[&Slot::Weapon].clone();
	let axe = ItemInstance::new(50, "test_axe", "common", 0);
	state.player.bag.push(axe.clone());
	let gold = state.player.gold;
	let events = auto_equip(state, &data);
	assert_eq!(
		events,
		[
			Event::ItemAutoEquipped {
				uid: 50,
				item_id: "test_axe".into(),
				slot: Slot::Weapon,
				score: item_score(&axe, &data)
			},
			Event::ItemAutoSold { uid: starter.uid, item_id: "sword".into(), gold: item_value(&starter, &data) },
		]
	);
	assert_eq!(state.player.equipment[&Slot::Weapon], axe);
	assert_eq!(state.player.gold, gold + item_value(&starter, &data));
	assert!(state.player.bag.is_empty());
	assert_eq!(engine.rng_state(), rng_state);
}

#[test]
fn empty_slots_are_filled_without_selling() {
	let data = with_test_items(&common::data());
	let mut engine = engine(&data, true);
	let state = engine.state_mut();
	state.player.bag.push(ItemInstance::new(60, "test_helmet", "common", 0));
	let events = auto_equip(state, &data);
	assert_eq!(types(&events), ["item_auto_equipped"]);
	assert_eq!(state.player.equipment[&Slot::Helmet].uid, 60);
}

#[test]
fn ties_go_to_the_lowest_uid_and_worse_items_stay() {
	let data = with_test_items(&common::data());
	let mut engine = engine(&data, true);
	let state = engine.state_mut();
	state.player.bag.extend([
		ItemInstance::new(72, "test_helmet", "common", 0),
		ItemInstance::new(71, "test_helmet", "common", 0),
		ItemInstance::new(73, "test_rod", "common", 0),
	]);
	auto_equip(state, &data);
	assert_eq!(state.player.equipment[&Slot::Helmet].uid, 71);
	assert_eq!(state.player.bag.iter().map(|item| item.uid).collect::<Vec<_>>(), [72, 73]);
	assert!(auto_equip(state, &data).is_empty());
}

#[test]
fn items_above_the_player_level_are_skipped() {
	let data = with_test_items(&common::data());
	let mut engine = engine(&data, true);
	let state = engine.state_mut();
	state.player.bag.push(ItemInstance::new(80, "test_axe", "mythic", 9));
	assert!(auto_equip(state, &data).is_empty());
	state.player.level = 1 + 9 * data.balance.item_level_per_tier;
	assert!(matches!(auto_equip(state, &data)[0], Event::ItemAutoEquipped { uid: 80, .. }));
}

#[test]
fn victory_triggers_auto_equip_only_when_enabled() {
	let base = calm(&with_test_items(&common::data()));
	let drops = ClassOverrides { drop_chance_pct: Some(100), ..ClassOverrides::default() };
	let mut content = with_enemy_class(&base, EnemyClass::Normal, drops).content();
	for item in &mut content.items {
		if item.id == "sword" {
			item.stats.clear();
		}
	}
	let data = Rc::new(GameData::new(content));
	for enabled in [true, false] {
		let mut engine = engine(&data, enabled);
		engine.step(&Command::NextFight);
		engine.state_mut().monster.as_mut().unwrap().hp = 1;
		let events = engine.step(&Command::Attack);
		assert_eq!(engine.state().phase, Phase::Merchant);
		assert_eq!(types(&events).contains(&"item_auto_equipped".to_owned()), enabled);
		assert_eq!(engine.state().stats.items_auto_equipped, i64::from(enabled));
	}
}

#[test]
fn buying_a_stock_item_triggers_auto_equip() {
	let data = with_test_items(&common::data());
	let axe = ItemInstance::new(90, "test_axe", "common", 0);
	let mut auto = engine(&data, true);
	auto.state_mut().merchant_stock = vec![axe.clone()];
	auto.state_mut().player.gold = 10_000;
	let events = auto.step(&Command::BuyStockItem { index: 0 });
	assert_eq!(types(&events), ["item_bought", "item_auto_equipped", "item_auto_sold"]);
	assert_eq!(auto.state().player.equipment[&Slot::Weapon], axe);
	let mut manual = engine(&data, false);
	manual.state_mut().merchant_stock = vec![axe];
	manual.state_mut().player.gold = 10_000;
	assert_eq!(types(&manual.step(&Command::BuyStockItem { index: 0 })), ["item_bought"]);
}
