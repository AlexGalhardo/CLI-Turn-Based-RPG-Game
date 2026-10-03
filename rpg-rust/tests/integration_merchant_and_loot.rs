//! Merchant phase and item generation (port of `tests/unit/test_merchant_and_loot.py`).

mod common;

use std::collections::BTreeMap;
use std::rc::Rc;

use common::{new_engine, warrior, with_test_items};
use rpg::application::commands::Command;
use rpg::application::events::{ErrorCode, Event};
use rpg::application::loot::{can_use, generate_item, rarity_weights};
use rpg::application::merchant::{available_potions, stock_price};
use rpg::domain::character::{build_sheet, item_stats, item_value};
use rpg::domain::definitions::{AffixDef, GameData};
use rpg::domain::entities::{AffixRoll, ItemInstance};
use rpg::domain::enums::{Slot, Stat};
use rpg::domain::rng::Rng;

fn error(code: ErrorCode) -> Vec<Event> {
	vec![Event::error(code)]
}

fn affix(id: &str, stat: Stat, min: i64, max: i64, per_tier: i64, slots: &[Slot]) -> AffixDef {
	AffixDef { id: id.into(), stat, min, max, per_tier, slots: slots.to_vec() }
}

fn loot_data(data: &GameData) -> Rc<GameData> {
	let mut content = with_test_items(data).content();
	content.affixes = vec![
		affix("of_power", Stat::Attack, 1, 3, 2, &[Slot::Weapon]),
		affix("of_the_bear", Stat::MaxHp, 5, 10, 5, &[Slot::Weapon, Slot::Helmet]),
		affix("of_speed", Stat::Dodge, 1, 2, 0, &[Slot::Weapon, Slot::Ring]),
		affix("of_speed_2", Stat::Dodge, 1, 2, 0, &[Slot::Weapon]),
	];
	Rc::new(GameData::new(content))
}

#[test]
fn buy_potion_rules() {
	let data = common::data();
	let mut engine = warrior(&data);
	let gold = engine.state().player.gold;
	assert_eq!(
		engine.step(&Command::buy_potion("health_potion", 2)),
		vec![Event::PotionBought { potion_id: "health_potion".into(), quantity: 2, gold: 100 }]
	);
	assert_eq!(engine.state().player.gold, gold - 100);
	assert_eq!(engine.step(&Command::buy_potion("health_potion", 1)), error(ErrorCode::NotEnoughGold));
	assert_eq!(engine.step(&Command::buy_potion("health_potion", 0)), error(ErrorCode::InvalidQuantity));
	assert_eq!(engine.step(&Command::buy_potion("health_potion", 100)), error(ErrorCode::InvalidQuantity));
	assert_eq!(engine.step(&Command::buy_potion("great_health_potion", 1)), error(ErrorCode::PotionLocked));
	assert_eq!(engine.step(&Command::buy_potion("elixir", 1)), error(ErrorCode::UnknownPotion));
	assert_eq!(engine.state().stats.potions_bought["health_potion"], 2);
	assert_eq!(engine.state().stats.gold_spent, 100);
}

#[test]
fn available_potions_unlock_by_round() {
	let data = common::data();
	let mut engine = warrior(&data);
	assert_eq!(available_potions(engine.state(), &data), ["health_potion", "mana_potion"]);
	engine.state_mut().round = 80;
	assert_eq!(available_potions(engine.state(), &data).len(), data.potions.len());
}

#[test]
fn equip_swap_sell_and_unequip() {
	let base = common::data();
	let data = with_test_items(&base);
	let mut engine = new_engine(&data, "warrior", "normal", 42);
	let axe = ItemInstance::new(50, "test_axe", "common", 0);
	let rod = ItemInstance::new(51, "test_rod", "rare", 0);
	let rod_value = item_value(&rod, &data);
	engine.state_mut().player.bag.extend([axe, rod]);
	let starter_uid = engine.state().player.equipment[&Slot::Weapon].uid;

	assert_eq!(engine.step(&Command::Equip { uid: 51 }), error(ErrorCode::CannotEquip));
	assert_eq!(
		engine.step(&Command::Equip { uid: 50 }),
		vec![
			Event::ItemUnequipped { uid: starter_uid, item_id: "sword".into(), slot: Slot::Weapon },
			Event::ItemEquipped { uid: 50, item_id: "test_axe".into(), slot: Slot::Weapon },
		]
	);
	assert_eq!(build_sheet(&engine.state().player, &data).melee_min, 8 + 20);

	let gold = engine.state().player.gold;
	assert_eq!(
		engine.step(&Command::SellItem { uid: 51 }),
		vec![Event::ItemSold { uid: 51, item_id: "test_rod".into(), gold: rod_value }]
	);
	assert_eq!(engine.state().player.gold, gold + 250);
	assert_eq!(engine.step(&Command::SellItem { uid: 51 }), error(ErrorCode::InvalidItem));
	assert_eq!(
		engine.step(&Command::Unequip { slot: Slot::Weapon }),
		vec![Event::ItemUnequipped { uid: 50, item_id: "test_axe".into(), slot: Slot::Weapon }]
	);
	assert_eq!(engine.step(&Command::Unequip { slot: Slot::Weapon }), error(ErrorCode::InvalidItem));
	assert_eq!(engine.step(&Command::Equip { uid: 999 }), error(ErrorCode::InvalidItem));
}

#[test]
fn unequip_with_full_bag_and_hp_clamp() {
	let base = common::data();
	let data = with_test_items(&base);
	let mut engine = new_engine(&data, "warrior", "normal", 42);
	let player = &mut engine.state_mut().player;
	player.equipment.insert(Slot::Helmet, ItemInstance::new(70, "test_helmet", "common", 0));
	player.hp = build_sheet(player, &data).max_hp;
	for index in 0..data.balance.bag_capacity {
		player.bag.push(ItemInstance::new(100 + index, "test_ring", "common", 0));
	}
	assert_eq!(engine.step(&Command::Unequip { slot: Slot::Helmet }), error(ErrorCode::BagFull));
	engine.state_mut().player.bag.pop();
	engine.step(&Command::Unequip { slot: Slot::Helmet });
	let player = &engine.state().player;
	assert_eq!(player.hp, build_sheet(player, &data).max_hp);
}

#[test]
fn merchant_stock_purchase() {
	let base = common::data();
	let data = loot_data(&base);
	let mut engine = new_engine(&data, "warrior", "normal", 42);
	assert_eq!(engine.state().merchant_stock.len() as i64, data.balance.merchant_stock_size);
	let item = engine.state().merchant_stock[0].clone();
	let price = stock_price(&item, &data);
	engine.state_mut().player.gold = price;
	assert_eq!(
		engine.step(&Command::BuyStockItem { index: 0 }),
		vec![Event::ItemBought { uid: item.uid, item_id: item.item_id.clone(), gold: price }]
	);
	assert!(engine.state().player.bag.contains(&item));
	assert_eq!(engine.step(&Command::BuyStockItem { index: 0 }), error(ErrorCode::NotEnoughGold));
	assert_eq!(engine.step(&Command::BuyStockItem { index: 9 }), error(ErrorCode::InvalidItem));
	assert_eq!(engine.step(&Command::BuyStockItem { index: -1 }), error(ErrorCode::InvalidItem));
	for index in 0..30 {
		engine.state_mut().player.bag.push(ItemInstance::new(200 + index, "test_ring", "common", 0));
	}
	assert_eq!(engine.step(&Command::BuyStockItem { index: 0 }), error(ErrorCode::BagFull));
	engine.step(&Command::NextFight);
	assert!(engine.state().merchant_stock.is_empty());
}

#[test]
fn generate_item_is_deterministic_and_unique_affixes() {
	let base = common::data();
	let data = loot_data(&base);
	let vocation = data.vocation("warrior");
	let normal = data.balance.difficulty("normal");
	let generate = |uid| generate_item(&data, &mut Rng::new(5), vocation, 0, "boss", normal, uid);
	let first: Vec<_> = (0..20).map(generate).collect();
	let second: Vec<_> = (0..20).map(generate).collect();
	assert_eq!(first, second);
	let mut rng = Rng::new(11);
	for uid in 0..200 {
		let item = generate_item(&data, &mut rng, vocation, 1, "boss", normal, uid).expect("an item");
		assert_ne!(item.rarity, "common");
		let mut stats: Vec<Stat> = item.affixes.iter().map(|affix| affix.stat).collect();
		let count = stats.len();
		stats.sort();
		stats.dedup();
		assert_eq!(stats.len(), count, "duplicate affix stat");
		assert!(can_use(data.item(&item.item_id), vocation));
	}
}

#[test]
fn generate_item_without_candidates_consumes_nothing() {
	let data = common::data();
	let mut rng = Rng::new(3);
	let normal = data.balance.difficulty("normal");
	let mut content = data.content();
	content.items = Vec::new();
	let empty = GameData::new(content);
	let result = generate_item(&empty, &mut rng, data.vocation("mage"), 9, "monster", normal, 1);
	assert_eq!(result, None);
	assert_eq!(rng.state(), 3);
}

#[test]
fn hard_difficulty_boosts_non_common_weights() {
	let data = common::data();
	let normal = rarity_weights(&data, "monster", data.balance.difficulty("normal"));
	let hard = rarity_weights(&data, "monster", data.balance.difficulty("hard"));
	assert_eq!(hard[0], normal[0]);
	assert!(hard[1..].iter().zip(&normal[1..]).all(|(h, n)| h >= n));
}

#[test]
fn item_stats_apply_rarity_and_affixes() {
	let base = common::data();
	let data = with_test_items(&base);
	let mut item = ItemInstance::new(1, "test_helmet", "legendary", 0);
	item.affixes = vec![AffixRoll { stat: Stat::MaxHp, value: 7 }];
	assert_eq!(item_stats(&item, &data), BTreeMap::from([(Stat::Armor, 15), (Stat::MaxHp, 82)]));
	assert_eq!(item_value(&item, &data), 1000);
}

#[test]
fn can_use_checks_weapon_and_shield_types() {
	let data = common::data();
	let warrior = data.vocation("warrior");
	let mage = data.vocation("mage");
	let shields: Vec<_> = data.items.iter().filter(|item| item.slot == Slot::Shield).collect();
	assert!(shields.iter().any(|shield| can_use(shield, warrior) != can_use(shield, mage)));
	assert!(can_use(data.item("sword"), warrior));
	assert!(!can_use(data.item("sword"), mage));
	let armor = data.items.iter().find(|item| item.slot == Slot::Armor).unwrap();
	assert!(can_use(armor, mage) && can_use(armor, warrior));
}
