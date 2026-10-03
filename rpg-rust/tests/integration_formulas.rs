//! Formulas that read balance data, and derived character stats (port of `tests/unit/test_formulas.py`).

mod common;

use common::{warrior, with_test_items};
use rpg::domain::character::build_sheet;
use rpg::domain::entities::ItemInstance;
use rpg::domain::enums::{Element, Slot};
use rpg::domain::formulas::{
	mana_for_magic_level, round_info, scale_reward, scale_stat, scaling, spell_level_for_uses,
};

#[test]
fn mana_for_magic_level_is_cumulative() {
	let data = common::data();
	let balance = &data.balance;
	let base = balance.magic_level.base;
	let growth = balance.magic_level.growth_pct;
	assert_eq!(mana_for_magic_level(1, balance), base);
	assert_eq!(mana_for_magic_level(2, balance), base + base * growth / 100);
	assert!(mana_for_magic_level(5, balance) > mana_for_magic_level(4, balance));
}

#[test]
fn spell_level_for_uses_thresholds() {
	let data = common::data();
	let levels = &data.balance.spell_levels;
	for (uses, level) in [(0, 1), (19, 1), (20, 2), (49, 2), (50, 3), (5000, 3)] {
		assert_eq!(spell_level_for_uses(uses, levels).level, level, "{uses} uses");
	}
}

#[test]
fn round_info_cycles_through_tiers() {
	let data = common::data();
	let cases = [
		(1, (0, 0, 0, false)),
		(9, (0, 0, 8, false)),
		(10, (0, 0, 9, true)),
		(11, (1, 0, 0, false)),
		(100, (9, 0, 9, true)),
		(101, (0, 1, 0, false)),
		(250, (4, 2, 9, true)),
	];
	for (round, expected) in cases {
		let info = round_info(round, &data.balance, data.tier_count());
		assert_eq!((info.tier, info.cycle, info.position, info.is_boss), expected, "round {round}");
		assert_eq!(info.round, round);
	}
}

#[test]
fn scaling_combines_difficulty_cycle_and_position() {
	let data = common::data();
	let balance = &data.balance;
	let hard = balance.difficulty("hard");
	let info = round_info(103, balance, data.tier_count());
	let factors = scaling(&info, balance, hard);
	let cycle_pct = 100 + balance.cycle_stat_pct;
	let position_pct = 100 + 2 * balance.position_pct;
	assert_eq!(factors.hp_pct_product, hard.hp_pct * cycle_pct * position_pct);
	assert_eq!(scale_stat(1000, factors.hp_pct_product), 1000 * hard.hp_pct * cycle_pct * position_pct / 1_000_000);
	assert_eq!(
		scale_reward(100, factors.reward_xp_pct_product),
		100 * hard.xp_pct * (100 + balance.cycle_reward_pct) / 10_000
	);
}

#[test]
fn boss_ignores_position_scaling() {
	let data = common::data();
	let normal = data.balance.difficulty("normal");
	let info = round_info(10, &data.balance, data.tier_count());
	assert_eq!(scaling(&info, &data.balance, normal).hp_pct_product, 100 * 100 * 100);
}

#[test]
fn sheet_adds_vocation_level_and_equipment() {
	let base = common::data();
	let data = with_test_items(&base);
	let mut engine = common::new_engine(&data, "warrior", "normal", 1);
	let player = &mut engine.state_mut().player;
	let vocation = data.vocation("warrior");
	player.level = 3;
	player.equipment.insert(Slot::Helmet, ItemInstance::new(9, "test_helmet", "common", 0));
	let sheet = build_sheet(player, &data);
	assert_eq!(sheet.max_hp, vocation.start_hp + 2 * vocation.hp_per_level + 50);
	assert_eq!(sheet.max_mp, vocation.start_mp + 2 * vocation.mp_per_level);
	assert_eq!(sheet.armor, 10);
	assert_eq!(sheet.melee_min, vocation.melee_min + 2 * vocation.melee_per_level + 6);
	assert_eq!(sheet.weapon_element, Element::Physical);
	assert_eq!(sheet.protection(Element::Fire), 0);
	player.equipment.clear();
	assert_eq!(build_sheet(player, &data).weapon_element, Element::Physical);
	let _ = warrior(&data);
}

#[test]
fn protections_are_capped() {
	let data = common::data();
	let mut engine = warrior(&data);
	let player = &mut engine.state_mut().player;
	let mut amulet = ItemInstance::new(9, "sword", "common", 0);
	amulet.affixes = vec![rpg::domain::entities::AffixRoll { stat: rpg::domain::enums::Stat::ProtFire, value: 500 }];
	player.equipment.insert(Slot::Weapon, amulet);
	let sheet = build_sheet(player, &data);
	assert_eq!(sheet.protection(Element::Fire), data.balance.caps.protection);
	assert_eq!(sheet.protections.len(), 7);
}
