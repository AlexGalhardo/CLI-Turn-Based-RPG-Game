//! Player commands. Each maps 1:1 to a JSON object used by golden files (`{"type": "cast", "spellId": "…"}`).

use serde::{Deserialize, Serialize};
use serde_json::Value;

use crate::domain::enums::Slot;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case", rename_all_fields = "camelCase")]
pub enum Command {
	Attack,
	Cast {
		spell_id: String,
	},
	#[serde(rename = "potion")]
	UsePotion {
		potion_id: String,
	},
	Defend,
	NextFight,
	BuyPotion {
		potion_id: String,
		quantity: i64,
	},
	SellItem {
		uid: i64,
	},
	Equip {
		uid: i64,
	},
	Unequip {
		slot: Slot,
	},
	BuyStockItem {
		index: i64,
	},
}

impl Command {
	/// Shorthand constructors keep tests and the bot close to the Python spelling (`Cast("…")`).
	pub fn cast(spell_id: &str) -> Command {
		Command::Cast { spell_id: spell_id.to_owned() }
	}

	pub fn use_potion(potion_id: &str) -> Command {
		Command::UsePotion { potion_id: potion_id.to_owned() }
	}

	pub fn buy_potion(potion_id: &str, quantity: i64) -> Command {
		Command::BuyPotion { potion_id: potion_id.to_owned(), quantity }
	}
}

pub fn command_to_json(command: &Command) -> Value {
	serde_json::to_value(command).expect("a command always serialises")
}

pub fn command_from_json(raw: &Value) -> Result<Command, String> {
	Command::deserialize(raw).map_err(|error| format!("unknown command: {error}"))
}

#[cfg(test)]
mod tests {
	use serde_json::json;

	use super::*;

	#[test]
	fn command_round_trip() {
		let all = [
			Command::Attack,
			Command::cast("brutal_strike"),
			Command::use_potion("health_potion"),
			Command::Defend,
			Command::NextFight,
			Command::buy_potion("mana_potion", 3),
			Command::SellItem { uid: 4 },
			Command::Equip { uid: 5 },
			Command::Unequip { slot: Slot::Ring },
			Command::BuyStockItem { index: 1 },
		];
		for command in all {
			let text = serde_json::to_string(&command_to_json(&command)).unwrap();
			let parsed: Value = serde_json::from_str(&text).unwrap();
			assert_eq!(command_from_json(&parsed).unwrap(), command);
		}
	}

	#[test]
	fn json_shapes_match_the_golden_files() {
		assert_eq!(command_to_json(&Command::use_potion("x")), json!({"type": "potion", "potionId": "x"}));
		assert_eq!(command_to_json(&Command::NextFight), json!({"type": "next_fight"}));
		assert_eq!(command_to_json(&Command::BuyStockItem { index: 2 }), json!({"type": "buy_stock_item", "index": 2}));
	}

	#[test]
	fn unknown_command_type() {
		let error = command_from_json(&json!({"type": "dance"})).unwrap_err();
		assert!(error.contains("unknown command"), "{error}");
	}
}
