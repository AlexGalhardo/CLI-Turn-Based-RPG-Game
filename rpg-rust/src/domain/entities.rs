//! Mutable run state. Serialised with camelCase keys: the save format is shared with the other implementations.
//!
//! Field order follows the reference `to_dict()`, and maps are `BTreeMap`s, so keys come out sorted like
//! Python's `dict(sorted(...))`.

use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};

use crate::domain::definitions::MonsterAttack;
use crate::domain::enums::{Slot, Stat};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ActiveStatus {
	pub status_id: String,
	pub turns: i64,
	pub per_turn: i64,
}

impl ActiveStatus {
	pub fn new(status_id: &str, turns: i64, per_turn: i64) -> ActiveStatus {
		ActiveStatus { status_id: status_id.to_owned(), turns, per_turn }
	}
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct AffixRoll {
	pub stat: Stat,
	pub value: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ItemInstance {
	pub uid: i64,
	pub item_id: String,
	pub rarity: String,
	pub tier: i64,
	pub affixes: Vec<AffixRoll>,
}

impl ItemInstance {
	pub fn new(uid: i64, item_id: &str, rarity: &str, tier: i64) -> ItemInstance {
		ItemInstance { uid, item_id: item_id.to_owned(), rarity: rarity.to_owned(), tier, affixes: Vec::new() }
	}
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Player {
	pub name: String,
	pub vocation_id: String,
	pub hp: i64,
	pub mp: i64,
	pub gold: i64,
	pub level: i64,
	pub xp: i64,
	pub magic_level: i64,
	pub mana_spent: i64,
	pub potions: BTreeMap<String, i64>,
	pub equipment: BTreeMap<Slot, ItemInstance>,
	pub bag: Vec<ItemInstance>,
	pub spell_uses: BTreeMap<String, i64>,
	pub statuses: Vec<ActiveStatus>,
	pub stun_cooldown: i64,
	pub defending: bool,
}

impl Player {
	/// A level 1 character with the given resources (the defaults of the Python dataclass).
	pub fn new(name: &str, vocation_id: &str, hp: i64, mp: i64, gold: i64) -> Player {
		Player {
			name: name.to_owned(),
			vocation_id: vocation_id.to_owned(),
			hp,
			mp,
			gold,
			level: 1,
			xp: 0,
			magic_level: 1,
			mana_spent: 0,
			potions: BTreeMap::new(),
			equipment: BTreeMap::new(),
			bag: Vec::new(),
			spell_uses: BTreeMap::new(),
			statuses: Vec::new(),
			stun_cooldown: 0,
			defending: false,
		}
	}

	pub fn potion_count(&self, potion_id: &str) -> i64 {
		self.potions.get(potion_id).copied().unwrap_or(0)
	}

	pub fn spell_use_count(&self, spell_id: &str) -> i64 {
		self.spell_uses.get(spell_id).copied().unwrap_or(0)
	}
}

/// A spawned monster: definition id plus stats already scaled for the round and difficulty.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MonsterInstance {
	pub creature_id: String,
	pub is_boss: bool,
	pub hp: i64,
	pub max_hp: i64,
	pub xp: i64,
	pub gold_min: i64,
	pub gold_max: i64,
	pub attacks: Vec<MonsterAttack>,
	pub statuses: Vec<ActiveStatus>,
	pub stun_cooldown: i64,
	pub boss_actions: i64,
}

impl MonsterInstance {
	pub fn attack(&self, attack_id: &str) -> &MonsterAttack {
		self.attacks
			.iter()
			.find(|attack| attack.id == attack_id)
			.unwrap_or_else(|| panic!("unknown attack: {attack_id}"))
	}
}
