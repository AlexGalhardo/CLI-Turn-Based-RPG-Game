//! M8 rules (docs/game-design.md §3, §6, §8, §9): enemy classes, monster dodge/parry/heal/crit, parry reflects,
//! class drop tables, potion drops, spell level effects and the victory phase (port of
//! `tests/unit/test_arpg_rules.py`).

mod common;

use std::rc::Rc;

use common::{ClassOverrides, calm, new_engine, types, warrior, with_balance, with_enemy_class};
use rpg::application::commands::Command;
use rpg::application::engine::GameEngine;
use rpg::application::events::{ErrorCode, Event};
use rpg::application::spawner::spawn_monster;
use rpg::domain::character::build_sheet;
use rpg::domain::definitions::{GameData, ItemDef, MonsterAttack};
use rpg::domain::entities::{ItemInstance, MonsterInstance};
use rpg::domain::enums::{Element, EnemyClass, Phase, Slot, Stat};
use rpg::domain::formulas::pct;
use rpg::domain::rng::Rng;

const MAX_TURNS: usize = 300;

fn overrides(change: impl FnOnce(&mut ClassOverrides)) -> ClassOverrides {
	let mut overrides = ClassOverrides::default();
	change(&mut overrides);
	overrides
}

fn fight_with(engine: &mut GameEngine, damage: i64, element: Element) -> &mut MonsterInstance {
	engine.step(&Command::NextFight);
	let monster = engine.state_mut().monster.as_mut().expect("a monster");
	monster.attacks =
		vec![MonsterAttack { id: "test_hit".into(), element, min: damage, max: damage, weight: 1, status: None }];
	monster.hp = 10_000;
	monster.max_hp = 10_000;
	monster
}

fn fight(engine: &mut GameEngine) -> &mut MonsterInstance {
	fight_with(engine, 1, Element::Physical)
}

fn monster(engine: &GameEngine) -> &MonsterInstance {
	engine.state().monster.as_ref().expect("a monster")
}

/// Finishes the current fight with melee hits (the monster is left with 1 HP before each hit).
fn kill(engine: &mut GameEngine) -> Vec<Event> {
	for _ in 0..MAX_TURNS {
		let state = engine.state_mut();
		let Some(monster) = state.monster.as_mut() else {
			break;
		};
		monster.hp = 1;
		state.player.hp = 1_000_000;
		let events = engine.step(&Command::Attack);
		if engine.state().phase != Phase::Battle {
			return events;
		}
	}
	panic!("the fight did not end");
}

fn physical_resistant_100(data: &GameData) -> String {
	data.monsters.iter().find(|monster| monster.resistance(Element::Physical) == 100).unwrap().id.clone()
}

fn count(events: &[Event], kind: &str) -> usize {
	types(events).iter().filter(|candidate| *candidate == kind).count()
}

fn has(events: &[Event], kind: &str) -> bool {
	count(events, kind) > 0
}

// ── spawn ────────────────────────────────────────────────────────────────────

#[test]
fn elite_spawn_scales_stats_and_rewards() {
	let data = common::data();
	let normal_data = with_enemy_class(&calm(&data), EnemyClass::Normal, ClassOverrides::default());
	let elite_data = with_balance(&normal_data, |balance| balance.elite_chance_pct = 100);
	let difficulty = data.balance.difficulty("normal");
	let (normal, _) = spawn_monster(&normal_data, &mut Rng::new(5), 3, difficulty);
	let (elite, _) = spawn_monster(&elite_data, &mut Rng::new(5), 3, difficulty);
	let row = data.balance.enemy_class(EnemyClass::Elite);
	assert_eq!((normal.enemy_class, elite.enemy_class), (EnemyClass::Normal, EnemyClass::Elite));
	assert_eq!(elite.creature_id, normal.creature_id);
	assert_eq!(elite.max_hp, pct(normal.max_hp, row.stat_pct));
	assert_eq!(elite.attacks[0].max, pct(normal.attacks[0].max, row.stat_pct));
	assert_eq!(elite.xp, pct(normal.xp, row.reward_pct));
	assert_eq!(elite.gold_max, pct(normal.gold_max, row.reward_pct));
}

#[test]
fn elite_roll_follows_the_monster_pick() {
	let data = common::data();
	let difficulty = data.balance.difficulty("normal");
	let mut rng = Rng::new(77);
	let classes: Vec<EnemyClass> =
		(0..300).map(|index| spawn_monster(&data, &mut rng, 1 + index % 9, difficulty).0.enemy_class).collect();
	let elites = classes.iter().filter(|class| **class == EnemyClass::Elite).count();
	assert!(classes.iter().all(|class| matches!(class, EnemyClass::Normal | EnemyClass::Elite)));
	assert!(classes.contains(&EnemyClass::Normal));
	assert!((30..=90).contains(&elites), "{elites}");
	let (boss, _) = spawn_monster(&data, &mut Rng::new(1), 10, difficulty);
	assert_eq!(boss.enemy_class, EnemyClass::Boss);
}

#[test]
fn round_started_reports_the_enemy_class() {
	let data = common::data();
	let elite_data = with_balance(&data, |balance| balance.elite_chance_pct = 100);
	let mut engine = warrior(&elite_data);
	match &engine.step(&Command::NextFight)[0] {
		Event::RoundStarted { enemy_class, is_boss, .. } => {
			assert_eq!(*enemy_class, EnemyClass::Elite);
			assert!(!is_boss);
		}
		other => panic!("unexpected {other:?}"),
	}
}

// ── monster dodge, parry, heal and crit ──────────────────────────────────────

#[test]
fn monster_dodge_stops_melee_and_spells() {
	let data = common::data();
	let mut engine = warrior(&with_enemy_class(&calm(&data), EnemyClass::Normal, overrides(|o| o.dodge = Some(100))));
	fight(&mut engine);
	let events = engine.step(&Command::Attack);
	assert_eq!(events[0], Event::MonsterDodged);
	assert!(!has(&events, "player_attacked"));
	assert_eq!(monster(&engine).hp, monster(&engine).max_hp);
	engine.state_mut().player.mp = 1000;
	let events = engine.step(&Command::cast("brutal_strike"));
	assert_eq!(events[0], Event::MonsterDodged);
	let player = &engine.state().player;
	assert!(player.mp < 1000);
	assert_eq!(player.spell_use_count("brutal_strike"), 1);
}

#[test]
fn monster_parry_reflects_physical_hits() {
	let data = common::data();
	let mut engine = warrior(&with_enemy_class(&calm(&data), EnemyClass::Normal, overrides(|o| o.parry = Some(100))));
	fight(&mut engine);
	let events = engine.step(&Command::Defend);
	assert!(!has(&events, "monster_parried"));
	let hp = engine.state().player.hp;
	let events = engine.step(&Command::Attack);
	let Event::MonsterParried { reflected } = events[0] else {
		panic!("expected a parry: {events:?}");
	};
	assert!(reflected >= 1);
	assert!(!has(&events, "leeched"));
	assert_eq!(monster(&engine).hp, monster(&engine).max_hp);
	let hits: i64 =
		events.iter().map(|event| if let Event::MonsterAttacked { damage, .. } = event { *damage } else { 0 }).sum();
	let regen: i64 = events.iter().map(|event| if let Event::Regenerated { hp, .. } = event { *hp } else { 0 }).sum();
	assert_eq!(engine.state().player.hp, hp - reflected - hits + regen);
}

#[test]
fn monster_parry_reflect_can_kill_the_player() {
	let data = common::data();
	let mut engine = warrior(&with_enemy_class(&calm(&data), EnemyClass::Normal, overrides(|o| o.parry = Some(100))));
	fight(&mut engine);
	engine.state_mut().player.hp = 1;
	let events = engine.step(&Command::Attack);
	assert_eq!(types(&events), ["monster_parried", "player_died"]);
	assert_eq!(engine.state().phase, Phase::GameOver);
	assert_eq!(monster(&engine).hp, monster(&engine).max_hp);
}

#[test]
fn monster_parry_ignores_non_physical_spells() {
	let data = common::data();
	let game_data = with_enemy_class(&calm(&data), EnemyClass::Normal, overrides(|o| o.parry = Some(100)));
	let mut engine = new_engine(&game_data, "mage", "normal", 42);
	fight(&mut engine);
	engine.state_mut().player.mp = 1000;
	let events = engine.step(&Command::cast("flame_strike"));
	assert!(!has(&events, "monster_parried"));
	assert!(has(&events, "spell_cast"));
}

#[test]
fn monster_heals_instead_of_attacking() {
	let data = common::data();
	let mut engine = warrior(&with_enemy_class(&calm(&data), EnemyClass::Normal, overrides(|o| o.heal = Some(100))));
	fight(&mut engine);
	let events = engine.step(&Command::Defend);
	assert!(!has(&events, "monster_healed"));
	assert!(has(&events, "monster_attacked"));
	let max_hp = monster(&engine).max_hp;
	engine.state_mut().monster.as_mut().unwrap().hp = max_hp - 5;
	let events = engine.step(&Command::Defend);
	assert!(events.contains(&Event::MonsterHealed { amount: 5 }));
	assert!(!has(&events, "monster_attacked"));
	engine.state_mut().monster.as_mut().unwrap().hp = 100;
	let events = engine.step(&Command::Defend);
	assert!(events.contains(&Event::MonsterHealed { amount: pct(max_hp, data.balance.monster_heal_pct) }));
}

#[test]
fn healing_boss_does_not_advance_its_pattern() {
	let data = common::data();
	let mut engine = warrior(&with_enemy_class(&calm(&data), EnemyClass::Boss, overrides(|o| o.heal = Some(100))));
	engine.state_mut().round = 9;
	engine.step(&Command::NextFight);
	let state = engine.state_mut();
	let boss = state.monster.as_mut().unwrap();
	boss.hp = boss.max_hp - 1;
	state.player.hp = 1_000_000;
	engine.step(&Command::Defend);
	assert_eq!(monster(&engine).boss_actions, 0);
}

#[test]
fn monster_crit_multiplies_raw_damage() {
	let data = common::data();
	for (crit, expected) in [(0, 100), (100, 150)] {
		let mut engine =
			warrior(&with_enemy_class(&calm(&data), EnemyClass::Normal, overrides(|o| o.crit = Some(crit))));
		fight_with(&mut engine, 100, Element::Fire);
		engine.state_mut().player.hp = 1000;
		let events = engine.step(&Command::Attack);
		let hit = events.iter().find_map(|event| match event {
			Event::MonsterAttacked { damage, crit, .. } => Some((*damage, *crit)),
			_ => None,
		});
		assert_eq!(hit, Some((expected, crit == 100)));
	}
}

fn parry_data(data: &GameData) -> Rc<GameData> {
	let shield = ItemDef {
		id: "test_parry_shield".into(),
		name: "Test Shield".into(),
		slot: Slot::Shield,
		item_type: "shield".into(),
		tier: 0,
		element: None,
		stats: [(Stat::Parry, 100)].into(),
		value: 10,
	};
	let mut content = calm(data).content();
	content.items.push(shield);
	content.balance.caps.dodge = 0;
	content.balance.caps.parry = 100;
	Rc::new(GameData::new(content))
}

#[test]
fn player_parry_reflects_damage_to_the_monster() {
	let data = common::data();
	let game_data = parry_data(&data);
	let mut engine = warrior(&game_data);
	engine.state_mut().player.equipment.insert(Slot::Shield, ItemInstance::new(90, "test_parry_shield", "common", 0));
	assert_eq!(build_sheet(&engine.state().player, &game_data).parry, 100);
	fight_with(&mut engine, 50, Element::Physical);
	let events = engine.step(&Command::Defend);
	assert!(events.contains(&Event::AttackParried { attack_id: "test_hit".into(), reflected: 10 }));
	assert_eq!(monster(&engine).hp, monster(&engine).max_hp - 10);
	assert_eq!(engine.state().stats.parries, 1);
	engine.state_mut().monster.as_mut().unwrap().hp = 5;
	let events = engine.step(&Command::Defend);
	assert!(has(&events, "monster_killed"));
	assert_eq!(engine.state().phase, Phase::Merchant);
}

// ── victory, drops and potions ───────────────────────────────────────────────

fn drop_rarities(events: &[Event]) -> Vec<String> {
	events
		.iter()
		.filter_map(|event| match event {
			Event::ItemDropped { rarity, .. } => Some(rarity.clone()),
			_ => None,
		})
		.collect()
}

#[test]
fn normal_drop_table() {
	let data = common::data();
	let always = with_enemy_class(&calm(&data), EnemyClass::Normal, overrides(|o| o.drop_chance_pct = Some(100)));
	let mut engine = warrior(&always);
	fight(&mut engine);
	let events = kill(&mut engine);
	let drops = drop_rarities(&events);
	assert_eq!(drops.len(), 1);
	assert!(["common", "rare"].contains(&drops[0].as_str()));
	assert!(!has(&events, "potion_dropped"));
	let never = with_enemy_class(&calm(&data), EnemyClass::Normal, overrides(|o| o.drop_chance_pct = Some(0)));
	let mut engine = warrior(&never);
	fight(&mut engine);
	assert!(!has(&kill(&mut engine), "item_dropped"));
}

#[test]
fn elite_drops_a_good_item_and_maybe_a_potion() {
	let data = common::data();
	let game_data = with_enemy_class(&calm(&data), EnemyClass::Elite, overrides(|o| o.potion_drop_pct = Some(100)));
	let game_data = with_balance(&game_data, |balance| balance.elite_chance_pct = 100);
	let mut engine = warrior(&game_data);
	fight(&mut engine);
	let before = engine.state().player.potions.clone();
	let events = kill(&mut engine);
	let drops = drop_rarities(&events);
	assert_eq!(drops.len(), 1);
	assert!(["rare", "legendary"].contains(&drops[0].as_str()));
	let potion_id = events
		.iter()
		.find_map(|event| match event {
			Event::PotionDropped { potion_id } => Some(potion_id.clone()),
			_ => None,
		})
		.expect("a potion drop");
	assert!(data.potion(&potion_id).unlock_round <= 1);
	let state = engine.state();
	assert_eq!(state.player.potion_count(&potion_id), before.get(&potion_id).copied().unwrap_or(0) + 1);
	assert_eq!(state.stats.potions_dropped[&potion_id], 1);
	assert_eq!(state.stats.elites_killed, 1);
	assert!(events.iter().any(|event| matches!(event, Event::MonsterKilled { enemy_class: EnemyClass::Elite, .. })));
}

#[test]
fn potion_drop_without_unlocked_potions_consumes_nothing() {
	let data = common::data();
	let classes = overrides(|o| {
		o.drop_chance_pct = Some(0);
		o.potion_drop_pct = Some(100);
	});
	let mut content = with_enemy_class(&calm(&data), EnemyClass::Normal, classes).content();
	for potion in &mut content.potions {
		potion.unlock_round = 99;
	}
	let mut engine = warrior(&Rc::new(GameData::new(content)));
	fight(&mut engine);
	assert!(!has(&kill(&mut engine), "potion_dropped"));
}

#[test]
fn boss_drops_several_top_items() {
	let data = common::data();
	let mut engine = warrior(&calm(&data));
	engine.state_mut().round = 9;
	fight(&mut engine);
	let drops = drop_rarities(&kill(&mut engine));
	assert_eq!(drops.len() as i64, data.balance.enemy_class(EnemyClass::Boss).drops);
	assert!(drops.iter().all(|rarity| ["legendary", "mythic"].contains(&rarity.as_str())));
}

#[test]
fn full_bag_auto_sells_drops() {
	let data = common::data();
	let mut engine =
		warrior(&with_enemy_class(&calm(&data), EnemyClass::Normal, overrides(|o| o.drop_chance_pct = Some(100))));
	let capacity = data.balance.bag_capacity;
	for index in 0..capacity {
		engine.state_mut().player.bag.push(ItemInstance::new(500 + index, "sword", "common", 0));
	}
	fight(&mut engine);
	let events = kill(&mut engine);
	assert!(has(&events, "item_auto_sold"));
	assert_eq!(engine.state().player.bag.len() as i64, capacity);
}

fn final_victory(engine: &mut GameEngine) -> Vec<Event> {
	engine.state_mut().round = engine.data().balance.final_round - 1;
	fight(engine);
	kill(engine)
}

#[test]
fn beating_the_final_boss_enters_the_victory_phase() {
	let data = common::data();
	let mut engine = warrior(&calm(&data));
	let events = final_victory(&mut engine);
	assert_eq!(events.last(), Some(&Event::RunWon { round: data.balance.final_round }));
	assert!(!has(&events, "merchant_entered"));
	assert_eq!(engine.state().phase, Phase::Victory);
	assert!(engine.state().won);
	let rng_state = engine.rng_state();
	let stock = engine.state().merchant_stock.clone();
	for command in [
		Command::Attack,
		Command::Defend,
		Command::NextFight,
		Command::buy_potion("health_potion", 1),
		Command::Equip { uid: 1 },
		Command::SellItem { uid: 1 },
	] {
		assert_eq!(engine.step(&command), [Event::error(ErrorCode::InvalidPhase)], "{command:?}");
	}
	assert_eq!(engine.rng_state(), rng_state);
	assert_eq!(engine.state().merchant_stock, stock);
	assert_eq!(engine.step(&Command::EndRun), [Event::RunEnded { won: true }]);
	assert_eq!(engine.state().phase, Phase::GameOver);
	assert_eq!(engine.state().death_cause, None);
	assert_eq!(engine.step(&Command::ContinueRun), [Event::error(ErrorCode::InvalidPhase)]);
}

#[test]
fn continue_run_enters_the_merchant() {
	let data = common::data();
	let mut engine = warrior(&calm(&data));
	final_victory(&mut engine);
	let events = engine.step(&Command::ContinueRun);
	assert_eq!(events, [Event::MerchantEntered { round: data.balance.final_round }]);
	assert_eq!(engine.state().phase, Phase::Merchant);
	assert_eq!(engine.state().merchant_stock.len() as i64, data.balance.merchant_stock_size);
	engine.step(&Command::NextFight);
	assert_eq!(engine.state().round, data.balance.final_round + 1);
	assert!(engine.state().won);
}

#[test]
fn victory_commands_are_rejected_outside_the_victory_phase() {
	let data = common::data();
	let mut engine = warrior(&data);
	assert_eq!(engine.step(&Command::EndRun), [Event::error(ErrorCode::InvalidPhase)]);
	assert_eq!(engine.step(&Command::ContinueRun), [Event::error(ErrorCode::InvalidPhase)]);
}

// ── spell levels ─────────────────────────────────────────────────────────────

#[test]
fn spell_level_effect_scales_damage() {
	let data = common::data();
	let game_data = calm(&data);
	for (uses, effect) in [(0, 100), (20, 150), (50, 200)] {
		let mut engine = warrior(&game_data);
		fight(&mut engine);
		let state = engine.state_mut();
		state.monster.as_mut().unwrap().creature_id = physical_resistant_100(&data);
		state.player.spell_uses.insert("brutal_strike".into(), uses);
		state.player.mp = 1000;
		let player = &engine.state().player;
		let mut rng = Rng::new(u64::from(engine.rng_state()));
		let spell = data.spell("brutal_strike");
		let bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level;
		let base = rng.roll(spell.min + bonus, spell.max + bonus);
		let events = engine.step(&Command::cast("brutal_strike"));
		let damage = events.iter().find_map(|event| match event {
			Event::SpellCast { damage, .. } => Some(*damage),
			_ => None,
		});
		assert_eq!(damage, Some(1.max(pct(base, effect))), "{uses} uses");
	}
}

#[test]
fn spell_level_effect_scales_healing() {
	let data = common::data();
	let mut engine = warrior(&calm(&data));
	fight(&mut engine);
	let player = &mut engine.state_mut().player;
	player.spell_uses.insert("wound_cleansing".into(), 20);
	player.mp = 1000;
	player.hp = 1;
	let player = &engine.state().player;
	let mut rng = Rng::new(u64::from(engine.rng_state()));
	let spell = data.spell("wound_cleansing");
	let bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level;
	let expected = pct(rng.roll(spell.min + bonus, spell.max + bonus), 150);
	let room = build_sheet(player, &data).max_hp - player.hp;
	let events = engine.step(&Command::cast("wound_cleansing"));
	let healed = events.iter().find_map(|event| match event {
		Event::SpellHealed { amount, .. } => Some(*amount),
		_ => None,
	});
	assert_eq!(healed, Some(expected.min(room)));
}
