//! Engine events (docs/cross-language-parity.md §3).
//!
//! Python and Go use loose maps; here every event is a variant of one enum, so a typo in a field is a compile
//! error. Serde's internally tagged representation produces exactly the flat JSON objects of the golden files:
//! `{"type": "spell_cast", "spellId": "…", "damage": 9, …}`.

use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

use crate::domain::enums::{Element, EnemyClass, Resource, Slot, Target};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ErrorCode {
	NotEnoughMana,
	NotEnoughGold,
	NoPotion,
	UnknownSpell,
	UnknownPotion,
	PotionLocked,
	InvalidPhase,
	InvalidQuantity,
	BagFull,
	CannotEquip,
	InvalidItem,
	LevelTooLow,
	UnknownCommand,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case", rename_all_fields = "camelCase")]
pub enum Event {
	RunStarted {
		seed: u64,
		vocation: String,
		difficulty: String,
	},
	RoundStarted {
		round: i64,
		tier: i64,
		cycle: i64,
		monster_id: String,
		is_boss: bool,
		enemy_class: EnemyClass,
		hp: i64,
	},
	PlayerAttacked {
		damage: i64,
		crit: bool,
		element: Element,
	},
	SpellCast {
		spell_id: String,
		damage: i64,
		crit: bool,
		element: Element,
		mana: i64,
	},
	SpellHealed {
		spell_id: String,
		amount: i64,
		mana: i64,
	},
	PotionUsed {
		potion_id: String,
		amount: i64,
		resource: Resource,
	},
	PlayerDefended,
	Leeched {
		hp: i64,
		mp: i64,
	},
	MonsterAttacked {
		attack_id: String,
		damage: i64,
		element: Element,
		charged: bool,
		crit: bool,
	},
	MonsterDodged,
	MonsterParried {
		reflected: i64,
	},
	MonsterHealed {
		amount: i64,
	},
	AttackDodged {
		attack_id: String,
	},
	AttackParried {
		attack_id: String,
		reflected: i64,
	},
	BossTelegraph {
		attack_id: String,
		element: Element,
	},
	StatusApplied {
		target: Target,
		status: String,
		turns: i64,
		per_turn: i64,
	},
	StatusTicked {
		target: Target,
		status: String,
		damage: i64,
	},
	StatusExpired {
		target: Target,
		status: String,
	},
	PlayerStunned,
	MonsterStunned,
	Regenerated {
		hp: i64,
		mp: i64,
	},
	MonsterKilled {
		monster_id: String,
		is_boss: bool,
		enemy_class: EnemyClass,
	},
	XpGained {
		amount: i64,
		total: i64,
	},
	LevelUp {
		level: i64,
		max_hp: i64,
		max_mp: i64,
	},
	MagicLevelUp {
		magic_level: i64,
	},
	SpellLevelUp {
		spell_id: String,
		level: i64,
	},
	GoldLooted {
		amount: i64,
	},
	ItemDropped {
		uid: i64,
		item_id: String,
		rarity: String,
	},
	ItemAutoSold {
		uid: i64,
		item_id: String,
		gold: i64,
	},
	ItemAutoEquipped {
		uid: i64,
		item_id: String,
		slot: Slot,
		score: i64,
	},
	PotionDropped {
		potion_id: String,
	},
	RunWon {
		round: i64,
	},
	RunEnded {
		won: bool,
	},
	MerchantEntered {
		round: i64,
	},
	PotionBought {
		potion_id: String,
		quantity: i64,
		gold: i64,
	},
	ItemBought {
		uid: i64,
		item_id: String,
		gold: i64,
	},
	ItemSold {
		uid: i64,
		item_id: String,
		gold: i64,
	},
	ItemEquipped {
		uid: i64,
		item_id: String,
		slot: Slot,
	},
	ItemUnequipped {
		uid: i64,
		item_id: String,
		slot: Slot,
	},
	PlayerDied {
		monster_id: String,
		round: i64,
	},
	Error {
		code: ErrorCode,
	},
}

impl Event {
	pub fn error(code: ErrorCode) -> Event {
		Event::Error { code }
	}

	/// The flat JSON object of the event (what golden files and the text formatter work with).
	pub fn to_json(&self) -> Map<String, Value> {
		match serde_json::to_value(self) {
			Ok(Value::Object(map)) => map,
			_ => unreachable!("an internally tagged enum always serialises to an object"),
		}
	}

	/// The `type` string, e.g. `"spell_cast"`.
	pub fn kind(&self) -> String {
		match self.to_json().get("type") {
			Some(Value::String(kind)) => kind.clone(),
			_ => unreachable!("every event has a type"),
		}
	}

	pub fn is_error(&self) -> bool {
		matches!(self, Event::Error { .. })
	}
}

#[cfg(test)]
mod tests {
	use serde_json::json;

	use super::*;

	#[test]
	fn events_serialise_to_flat_camel_case_objects() {
		let event = Event::SpellCast {
			spell_id: "flame_strike".into(),
			damage: 9,
			crit: false,
			element: Element::Fire,
			mana: 20,
		};
		assert_eq!(
			serde_json::to_value(&event).unwrap(),
			json!({"type": "spell_cast", "spellId": "flame_strike", "damage": 9, "crit": false, "element": "fire", "mana": 20})
		);
		assert_eq!(serde_json::to_value(Event::PlayerDefended).unwrap(), json!({"type": "player_defended"}));
		assert_eq!(
			serde_json::to_value(Event::error(ErrorCode::BagFull)).unwrap(),
			json!({"type": "error", "code": "bag_full"})
		);
		assert_eq!(event.kind(), "spell_cast");
		assert!(Event::error(ErrorCode::UnknownCommand).is_error());
		let back: Event =
			serde_json::from_value(json!({"type": "level_up", "level": 2, "maxHp": 10, "maxMp": 5})).unwrap();
		assert_eq!(back, Event::LevelUp { level: 2, max_hp: 10, max_mp: 5 });
	}
}
