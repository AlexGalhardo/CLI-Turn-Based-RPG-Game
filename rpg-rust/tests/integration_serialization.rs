//! JSON shapes shared with the other implementations (port of `tests/unit/test_serialization.py`).

#![recursion_limit = "256"]

mod common;

use common::{fight, warrior};
use rpg::application::commands::Command;
use rpg::application::engine::GameEngine;
use rpg::application::run_state::RunState;
use rpg::application::save_game::SaveGame;
use rpg::domain::entities::{ActiveStatus, AffixRoll, ItemInstance};
use rpg::domain::enums::{Phase, Slot, Stat};
use serde_json::{Value, json};

#[test]
fn run_state_round_trip_through_json() {
	let data = common::data();
	let mut engine = warrior(&data);
	fight(&mut engine);
	for _ in 0..3 {
		engine.step(&Command::Attack);
	}
	let state = engine.state_mut();
	let mut item = ItemInstance::new(90, "sword", "epic", 2);
	item.affixes = vec![AffixRoll { stat: Stat::Dodge, value: 3 }];
	state.player.bag.push(item);
	state.player.statuses.push(ActiveStatus::new("burn", 2, 4));
	if let Some(monster) = &mut state.monster {
		monster.statuses.push(ActiveStatus::new("stun", 1, 0));
	}
	let text = serde_json::to_string(&state.to_json()).unwrap();
	let restored = RunState::from_json(&serde_json::from_str(&text).unwrap()).unwrap();
	assert_eq!(&restored, engine.state());
	assert_eq!(restored.to_json(), engine.state().to_json());
}

#[test]
fn maps_serialise_with_sorted_keys_like_the_reference() {
	let data = common::data();
	let mut engine = warrior(&data);
	let player = &mut engine.state_mut().player;
	player.equipment.insert(Slot::Ring, ItemInstance::new(7, "sword", "common", 0));
	player.equipment.insert(Slot::Amulet, ItemInstance::new(8, "sword", "common", 0));
	let text = serde_json::to_string(&engine.state().to_json()["player"]).unwrap();
	let amulet = text.find("\"amulet\"").unwrap();
	let ring = text.find("\"ring\"").unwrap();
	let weapon = text.find("\"weapon\"").unwrap();
	assert!(amulet < ring && ring < weapon);
	assert!(text.find("\"health_potion\"").unwrap() < text.find("\"mana_potion\"").unwrap());
}

/// A save written by the Python reference (same shape as `SaveGame.to_dict()`), including a monster attack with a
/// status and a `null` monster/deathCause: the Rust port must load it and continue the run.
#[test]
fn loads_a_save_produced_by_the_python_reference() {
	let data = common::data();
	let raw = json!({
		"schemaVersion": 1,
		"gameVersion": "1.0.0",
		"implementation": "python",
		"savedAt": "2026-09-27T21:04:11Z",
		"rngState": 2_891_336_453_u32,
		"session": {"runId": "20260927T210411Z-42", "startedAt": "2026-09-27T21:04:11Z", "playTimeSeconds": 1520, "sessions": 2},
		"run": {
			"seed": 42,
			"config": {"name": "Alex", "vocation": "warrior", "difficulty": "normal"},
			"player": {
				"name": "Alex", "vocationId": "warrior", "hp": 150, "mp": 30, "gold": 340, "level": 4, "xp": 520,
				"magicLevel": 2, "manaSpent": 140,
				"potions": {"health_potion": 3, "mana_potion": 5},
				"equipment": {"weapon": {"uid": 1, "itemId": "sword", "rarity": "common", "tier": 0, "affixes": []}},
				"bag": [{"uid": 5, "itemId": "hand_axe", "rarity": "rare", "tier": 0, "affixes": [{"stat": "dodge", "value": 2}]}],
				"spellUses": {"brutal_strike": 7},
				"statuses": [],
				"stunCooldown": 0,
				"defending": false
			},
			"phase": "merchant",
			"round": 3,
			"turn": 0,
			"monster": null,
			"merchantStock": [{"uid": 6, "itemId": "mace", "rarity": "common", "tier": 0, "affixes": []}],
			"nextItemUid": 7,
			"deathCause": null,
			"stats": {
				"damageDealt": 210, "damageTaken": 95, "healingDone": 40, "highestHit": 31, "normalAttacks": 12,
				"crits": 1, "dodges": 0, "parries": 1, "defends": 0, "goldLooted": 240, "goldSpent": 0,
				"goldEarned": 0, "itemsSold": 0, "bossesKilled": 0,
				"spellsCast": {"brutal_strike": 7}, "potionsUsed": {"health_potion": 2}, "potionsBought": {},
				"itemsDropped": {"rare": 1}, "kills": {"rat": 2, "bat": 1}, "statusesApplied": {},
				"droppedItems": [{"itemId": "hand_axe", "rarity": "rare", "round": 2}]
			}
		}
	});
	let save = SaveGame::from_json(&raw).expect("python save loads");
	assert_eq!(save.implementation, "python");
	assert_eq!(save.session.sessions, 2);
	assert_eq!(save.run.player.equipment[&Slot::Weapon].item_id, "sword");
	assert_eq!(serde_json::to_value(&save).unwrap(), raw, "re-serialises to the same document");

	let mut engine = GameEngine::restore(data, save.run, save.rng_state);
	assert_eq!(engine.state().phase, Phase::Merchant);
	let events = engine.step(&Command::NextFight);
	assert_eq!(events.len(), 1);
	assert_eq!(engine.state().round, 4);

	let mut monster_json = engine.state().to_json()["monster"].clone();
	assert!(monster_json["attacks"].as_array().is_some_and(|attacks| !attacks.is_empty()));
	monster_json["statuses"] = json!([{"statusId": "poison", "turns": 2, "perTurn": 3}]);
	let monster: rpg::domain::entities::MonsterInstance = serde_json::from_value(monster_json.clone()).unwrap();
	assert_eq!(serde_json::to_value(&monster).unwrap(), monster_json);
	assert_eq!(monster.statuses[0].per_turn, 3);
}

#[test]
fn invalid_documents_are_rejected() {
	assert!(RunState::from_json(&json!({"seed": 1})).is_err());
	assert!(SaveGame::from_json(&json!({"schemaVersion": 1})).is_err());
	assert!(SaveGame::from_json(&Value::Null).is_err());
}
