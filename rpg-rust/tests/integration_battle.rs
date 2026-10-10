//! Battle rules with the real shared data (port of `tests/unit/test_battle.py`).

mod common;

use common::{fight, new_engine, types, warrior, with_test_items};
use rpg::application::commands::Command;
use rpg::application::events::{ErrorCode, Event};
use rpg::domain::character::build_sheet;
use rpg::domain::definitions::{MonsterAttack, StatusOnHit};
use rpg::domain::entities::{ActiveStatus, ItemInstance, MonsterInstance};
use rpg::domain::enums::{Element, Phase, Resource, Slot, Target};

fn fixed_attack(monster: &mut MonsterInstance, damage: i64, element: Element, status: Option<StatusOnHit>) {
	monster.attacks =
		vec![MonsterAttack { id: "test_hit".into(), element, min: damage, max: damage, weight: 1, status }];
	monster.hp = 10_000;
	monster.max_hp = 10_000;
}

fn physical(monster: &mut MonsterInstance, damage: i64) {
	fixed_attack(monster, damage, Element::Physical, None);
}

fn on_hit(status: &str, chance: i64, damage_pct: i64) -> StatusOnHit {
	StatusOnHit { status: status.into(), chance, damage_pct }
}

fn error(code: ErrorCode) -> Vec<Event> {
	vec![Event::error(code)]
}

#[test]
fn melee_damage_is_within_sheet_range() {
	let data = common::data();
	let mut engine = warrior(&data);
	fight(&mut engine);
	let sheet = build_sheet(&engine.state().player, &data);
	let monster = engine.state().monster.as_ref().unwrap();
	let resistance = data.creature(&monster.creature_id).resistance(Element::Physical);
	let events = engine.step(&Command::Attack);
	let (damage, crit) = events
		.iter()
		.find_map(|event| match event {
			Event::PlayerAttacked { damage, crit, .. } => Some((*damage, *crit)),
			_ => None,
		})
		.expect("player attacked");
	let low = 1.max(sheet.melee_min * resistance / 100);
	let high = sheet.melee_max * resistance / 100;
	assert!(!crit);
	assert!((low..=high.max(low)).contains(&damage), "{damage} not in [{low}, {high}]");
}

#[test]
fn invalid_battle_commands_do_not_consume_rng() {
	let data = common::data();
	let mut engine = warrior(&data);
	assert_eq!(engine.step(&Command::Attack), error(ErrorCode::InvalidPhase));
	fight(&mut engine);
	let rng_state = engine.rng_state();
	assert_eq!(engine.step(&Command::cast("flame_strike")), error(ErrorCode::UnknownSpell));
	engine.state_mut().player.mp = 0;
	assert_eq!(engine.step(&Command::cast("brutal_strike")), error(ErrorCode::NotEnoughMana));
	engine.state_mut().player.potions.clear();
	assert_eq!(engine.step(&Command::use_potion("health_potion")), error(ErrorCode::NoPotion));
	assert_eq!(engine.step(&Command::use_potion("elixir")), error(ErrorCode::UnknownPotion));
	assert_eq!(engine.step(&Command::NextFight), error(ErrorCode::InvalidPhase));
	assert_eq!(engine.step(&Command::buy_potion("health_potion", 1)), error(ErrorCode::InvalidPhase));
	assert_eq!(engine.rng_state(), rng_state);
}

#[test]
fn defend_halves_incoming_damage() {
	let data = common::data();
	let mut engine = warrior(&data);
	physical(fight(&mut engine), 100);
	engine.state_mut().player.hp = 150;
	let events = engine.step(&Command::Defend);
	let hit = events.iter().find_map(|event| match event {
		Event::MonsterAttacked { damage, .. } => Some(*damage),
		_ => None,
	});
	assert_eq!(hit, Some(50));
	assert!(!engine.state().player.defending);
}

#[test]
fn undefended_damage_and_death() {
	let data = common::data();
	let mut engine = warrior(&data);
	let monster = fight(&mut engine);
	physical(monster, 1000);
	let monster_id = monster.creature_id.clone();
	let events = engine.step(&Command::Attack);
	assert_eq!(events.last(), Some(&Event::PlayerDied { monster_id: monster_id.clone(), round: 1 }));
	assert_eq!(engine.state().phase, Phase::GameOver);
	assert_eq!(engine.state().death_cause.as_deref(), Some(monster_id.as_str()));
	assert_eq!(engine.step(&Command::Attack), error(ErrorCode::InvalidPhase));
	assert_eq!(engine.step(&Command::NextFight), error(ErrorCode::InvalidPhase));
}

#[test]
fn monster_status_applies_and_ticks() {
	let data = common::data();
	let mut engine = warrior(&data);
	fixed_attack(fight(&mut engine), 10, Element::Fire, Some(on_hit("burn", 100, 50)));
	let events = engine.step(&Command::Defend);
	assert!(events.contains(&Event::StatusApplied {
		target: Target::Player,
		status: "burn".into(),
		turns: 3,
		per_turn: 2
	}));
	assert!(events.contains(&Event::StatusTicked { target: Target::Player, status: "burn".into(), damage: 2 }));
	assert_eq!(engine.state().player.statuses, vec![ActiveStatus::new("burn", 2, 2)]);
}

#[test]
fn reapplying_status_refreshes_turns_and_keeps_higher_damage() {
	let data = common::data();
	let mut engine = warrior(&data);
	fixed_attack(fight(&mut engine), 10, Element::Fire, Some(on_hit("burn", 100, 50)));
	engine.state_mut().player.statuses.push(ActiveStatus::new("burn", 1, 9));
	engine.step(&Command::Defend);
	assert_eq!(engine.state().player.statuses, vec![ActiveStatus::new("burn", 2, 9)]);
}

#[test]
fn status_expires() {
	let data = common::data();
	let mut engine = warrior(&data);
	physical(fight(&mut engine), 1);
	engine.state_mut().player.statuses.push(ActiveStatus::new("bleed", 1, 1));
	let events = engine.step(&Command::Defend);
	assert!(types(&events).contains(&"status_expired".to_owned()));
	assert!(engine.state().player.statuses.is_empty());
}

#[test]
fn player_stun_skips_turn_and_has_cooldown() {
	let data = common::data();
	let mut engine = warrior(&data);
	fixed_attack(fight(&mut engine), 1, Element::Physical, Some(on_hit("stun", 100, 0)));
	let events = types(&engine.step(&Command::Defend));
	let count = |kind: &str| events.iter().filter(|event| *event == kind).count();
	assert_eq!(count("monster_attacked"), 2);
	assert_eq!(count("player_stunned"), 1);
	assert_eq!(count("status_applied"), 1);
	assert_eq!(engine.state().player.stun_cooldown, 1);
	let events = types(&engine.step(&Command::Defend));
	assert!(!events.contains(&"status_applied".to_owned()));
}

#[test]
fn monster_stun_skips_its_attack() {
	let data = common::data();
	let mut engine = warrior(&data);
	let monster = fight(&mut engine);
	physical(monster, 5);
	monster.statuses.push(ActiveStatus::new("stun", 1, 0));
	let events = types(&engine.step(&Command::Defend));
	assert!(events.contains(&"monster_stunned".to_owned()));
	assert!(!events.contains(&"monster_attacked".to_owned()));
	assert_eq!(engine.state().monster.as_ref().unwrap().stun_cooldown, 1);
}

#[test]
fn monster_dies_from_status_tick() {
	let data = common::data();
	let mut engine = warrior(&data);
	let monster = fight(&mut engine);
	monster.hp = 1;
	monster.statuses.push(ActiveStatus::new("bleed", 3, 5));
	let events = types(&engine.step(&Command::Defend));
	assert!(events.contains(&"monster_killed".to_owned()));
	assert_eq!(engine.state().phase, Phase::Merchant);
}

#[test]
fn boss_telegraphs_then_charges() {
	let data = common::data();
	let mut engine = warrior(&data);
	engine.state_mut().round = 9;
	let boss = fight(&mut engine);
	assert!(boss.is_boss);
	boss.hp = 1_000_000;
	boss.max_hp = 1_000_000;
	for attack in &mut boss.attacks {
		attack.status = None;
	}
	engine.state_mut().player.hp = 1_000_000;
	let mut sequence = Vec::new();
	for _ in 0..4 {
		let events = engine.step(&Command::Defend);
		let kind = events.iter().find_map(|event| match event {
			Event::BossTelegraph { .. } => Some("telegraph"),
			Event::MonsterAttacked { charged: true, .. } => Some("charged"),
			Event::MonsterAttacked { .. } | Event::AttackDodged { .. } | Event::AttackParried { .. } => Some("normal"),
			_ => None,
		});
		sequence.push(kind.unwrap_or("none"));
	}
	assert_eq!(sequence, ["normal", "normal", "telegraph", "charged"]);
}

#[test]
fn heal_is_capped_and_level_three_cleanses() {
	let data = common::data();
	let mut engine = warrior(&data);
	physical(fight(&mut engine), 1);
	let player = &mut engine.state_mut().player;
	player.spell_uses.insert("wound_cleansing".into(), 50);
	player.statuses.push(ActiveStatus::new("poison", 5, 1));
	player.hp = build_sheet(player, &data).max_hp - 3;
	let events = engine.step(&Command::cast("wound_cleansing"));
	let healed = events.iter().find_map(|event| match event {
		Event::SpellHealed { amount, mana, .. } => Some((*amount, *mana)),
		_ => None,
	});
	assert_eq!(healed, Some((3, 32)));
	assert!(events.contains(&Event::StatusExpired { target: Target::Player, status: "poison".into() }));
	assert_eq!(engine.state().player.spell_uses["wound_cleansing"], 51);
}

#[test]
fn spell_levels_up_after_twenty_uses() {
	let data = common::data();
	let mut engine = warrior(&data);
	physical(fight(&mut engine), 1);
	let player = &mut engine.state_mut().player;
	player.spell_uses.insert("brutal_strike".into(), 19);
	player.mp = 1000;
	let events = engine.step(&Command::cast("brutal_strike"));
	assert!(events.contains(&Event::SpellLevelUp { spell_id: "brutal_strike".into(), level: 2 }));
	let mana = events.iter().find_map(|event| match event {
		Event::SpellCast { mana, .. } => Some(*mana),
		_ => None,
	});
	assert_eq!(mana, Some(20));
}

#[test]
fn magic_level_grows_with_mana_spent() {
	let data = common::data();
	let mut engine = warrior(&data);
	physical(fight(&mut engine), 1);
	let player = &mut engine.state_mut().player;
	player.mana_spent = data.balance.magic_level.base - 1;
	player.mp = 1000;
	let events = engine.step(&Command::cast("brutal_strike"));
	assert!(events.contains(&Event::MagicLevelUp { magic_level: 2 }));
}

#[test]
fn immune_monster_takes_no_damage() {
	let data = common::data();
	let mut engine = new_engine(&data, "mage", "normal", 42);
	let monster = fight(&mut engine);
	physical(monster, 1);
	monster.creature_id = "fire_elemental".into();
	assert_eq!(data.creature("fire_elemental").resistance(Element::Fire), 0);
	engine.state_mut().player.mp = 1000;
	let events = engine.step(&Command::cast("flame_strike"));
	let damage = events.iter().find_map(|event| match event {
		Event::SpellCast { damage, .. } => Some(*damage),
		_ => None,
	});
	assert_eq!(damage, Some(0));
}

#[test]
fn immune_monster_resists_statuses_and_damage_over_time() {
	let data = common::data();
	let mut engine = new_engine(&data, "mage", "normal", 42);
	let monster = fight(&mut engine);
	physical(monster, 1);
	monster.creature_id = "fire_elemental".into();
	monster.statuses.push(ActiveStatus::new("burn", 2, 50));
	let player = &mut engine.state_mut().player;
	player.spell_uses.insert("flame_strike".into(), 50);
	player.mp = 10_000;
	let mut burns_applied = 0;
	for _ in 0..30 {
		engine.state_mut().player.hp = 1000;
		let events = engine.step(&Command::cast("flame_strike"));
		for event in &events {
			match event {
				Event::StatusTicked { target: Target::Monster, damage, .. } => assert_eq!(*damage, 0),
				Event::StatusApplied { target: Target::Monster, .. } => burns_applied += 1,
				_ => {}
			}
		}
	}
	assert_eq!(burns_applied, 0, "fire immunity blocks burn");
}

#[test]
fn items_add_crit_dodge_and_leech() {
	let base = common::data();
	let data = with_test_items(&base);
	let mut engine = new_engine(&data, "warrior", "normal", 42);
	engine.state_mut().player.equipment.insert(Slot::Ring, ItemInstance::new(99, "test_ring", "common", 0));
	let sheet = build_sheet(&engine.state().player, &data);
	assert_eq!(sheet.crit_chance, data.balance.caps.crit_chance);
	assert_eq!(sheet.dodge, data.balance.caps.dodge);
	physical(fight(&mut engine), 5);
	let (mut crits, mut dodges) = (0, 0);
	for _ in 0..60 {
		engine.state_mut().player.hp = 100;
		for event in engine.step(&Command::Attack) {
			match event {
				Event::PlayerAttacked { crit: true, .. } => crits += 1,
				Event::AttackDodged { .. } => dodges += 1,
				_ => {}
			}
		}
	}
	assert!(crits > 0);
	assert!(dodges > 0);
}

#[test]
fn leech_and_parry_come_from_affixes() {
	let base = common::data();
	let data = with_test_items(&base);
	let mut engine = new_engine(&data, "warrior", "normal", 42);
	let mut ring = ItemInstance::new(99, "test_helmet", "common", 0);
	ring.affixes = vec![
		rpg::domain::entities::AffixRoll { stat: rpg::domain::enums::Stat::LifeLeech, value: 25 },
		rpg::domain::entities::AffixRoll { stat: rpg::domain::enums::Stat::ManaLeech, value: 25 },
		rpg::domain::entities::AffixRoll { stat: rpg::domain::enums::Stat::Parry, value: 90 },
	];
	engine.state_mut().player.equipment.insert(Slot::Helmet, ring);
	physical(fight(&mut engine), 5);
	let (mut leeches, mut parries) = (0, 0);
	for _ in 0..40 {
		let player = &mut engine.state_mut().player;
		player.hp = 10;
		player.mp = 0;
		for event in engine.step(&Command::Attack) {
			match event {
				Event::Leeched { hp, mp } => {
					assert!(hp > 0 || mp > 0);
					leeches += 1;
				}
				Event::AttackParried { .. } => parries += 1,
				_ => {}
			}
		}
	}
	assert!(leeches > 0);
	assert!(parries > 0);
}

#[test]
fn potion_restores_and_is_consumed() {
	let data = common::data();
	let mut engine = warrior(&data);
	physical(fight(&mut engine), 1);
	let player = &mut engine.state_mut().player;
	player.hp = 10;
	let before = player.potion_count("mana_potion");
	player.mp = 0;
	let events = engine.step(&Command::use_potion("mana_potion"));
	let resource = events.iter().find_map(|event| match event {
		Event::PotionUsed { resource, .. } => Some(*resource),
		_ => None,
	});
	assert_eq!(resource, Some(Resource::Mp));
	let player = &engine.state().player;
	assert_eq!(player.potion_count("mana_potion"), before - 1);
	assert!(player.mp > 0 && player.mp <= build_sheet(player, &data).max_mp);
}

#[test]
fn every_vocation_spell_can_be_cast() {
	let data = common::data();
	for vocation in ["warrior", "archer", "mage"] {
		let mut engine = new_engine(&data, vocation, "normal", 42);
		physical(fight(&mut engine), 1);
		for spell_id in &data.vocation(vocation).spells {
			engine.state_mut().player.mp = 10_000;
			engine.state_mut().player.hp = 5;
			let events = engine.step(&Command::cast(spell_id));
			let cast = events.iter().any(|event| match event {
				Event::SpellCast { spell_id: id, .. } | Event::SpellHealed { spell_id: id, .. } => id == spell_id,
				_ => false,
			});
			assert!(cast, "{vocation}: {spell_id}");
		}
	}
}

#[test]
fn victory_grants_xp_gold_and_returns_to_the_merchant() {
	let data = common::data();
	let mut engine = warrior(&data);
	let monster = fight(&mut engine);
	monster.hp = 1;
	monster.xp = 100_000;
	let events = engine.step(&Command::Attack);
	let kinds = types(&events);
	for kind in ["monster_killed", "xp_gained", "level_up", "gold_looted", "merchant_entered"] {
		assert!(kinds.contains(&kind.to_owned()), "{kind} missing in {kinds:?}");
	}
	let state = engine.state();
	assert_eq!(state.phase, Phase::Merchant);
	assert!(state.player.level > 1);
	assert!(state.monster.is_none());
	assert_eq!(state.turn, 0);
	assert_eq!(state.stats.total_kills(), 1);
}

#[test]
fn boss_drops_and_full_bag_auto_sells() {
	let data = common::data();
	let mut engine = warrior(&data);
	engine.state_mut().round = 9;
	let capacity = data.balance.bag_capacity;
	for uid in 0..capacity {
		engine.state_mut().player.bag.push(ItemInstance::new(500 + uid, "sword", "common", 0));
	}
	let boss = fight(&mut engine);
	boss.hp = 1;
	let gold_before = engine.state().player.gold;
	let events = engine.step(&Command::Attack);
	let dropped = events.iter().filter(|event| matches!(event, Event::ItemDropped { .. })).count() as i64;
	let sold = events.iter().filter(|event| matches!(event, Event::ItemAutoSold { .. })).count() as i64;
	assert_eq!(dropped, data.balance.enemy_class(rpg::domain::enums::EnemyClass::Boss).drops);
	assert_eq!(sold, dropped);
	assert!(engine.state().player.gold > gold_before);
	assert_eq!(engine.state().player.bag.len() as i64, capacity);
	assert_eq!(engine.state().stats.items_sold, dropped);
}
