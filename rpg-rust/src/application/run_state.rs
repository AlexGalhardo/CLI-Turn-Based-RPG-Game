//! Everything needed to continue a run, except the PRNG state (kept by the engine).

use serde::{Deserialize, Serialize};

use crate::application::statistics::RunStatistics;
use crate::domain::entities::{ItemInstance, MonsterInstance, Player};
use crate::domain::enums::Phase;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RunConfig {
	pub name: String,
	#[serde(rename = "vocation")]
	pub vocation_id: String,
	#[serde(rename = "difficulty")]
	pub difficulty_id: String,
	#[serde(rename = "autoEquip")]
	pub auto_equip: bool,
}

impl RunConfig {
	pub fn new(name: &str, vocation_id: &str, difficulty_id: &str) -> RunConfig {
		RunConfig {
			name: name.to_owned(),
			vocation_id: vocation_id.to_owned(),
			difficulty_id: difficulty_id.to_owned(),
			auto_equip: false,
		}
	}

	/// The same config with the auto-equip option (docs/game-design.md §8.1).
	#[must_use]
	pub fn with_auto_equip(mut self, auto_equip: bool) -> RunConfig {
		self.auto_equip = auto_equip;
		self
	}
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RunState {
	pub seed: u64,
	pub config: RunConfig,
	pub player: Player,
	pub phase: Phase,
	pub round: i64,
	pub turn: i64,
	pub monster: Option<MonsterInstance>,
	pub merchant_stock: Vec<ItemInstance>,
	pub next_item_uid: i64,
	pub death_cause: Option<String>,
	pub won: bool,
	pub stats: RunStatistics,
}

impl RunState {
	/// A fresh run in the merchant phase (the defaults of the Python dataclass).
	pub fn new(seed: u64, config: RunConfig, player: Player) -> RunState {
		RunState {
			seed,
			config,
			player,
			phase: Phase::Merchant,
			round: 0,
			turn: 0,
			monster: None,
			merchant_stock: Vec::new(),
			next_item_uid: 1,
			death_cause: None,
			won: false,
			stats: RunStatistics::default(),
		}
	}

	pub fn take_item_uid(&mut self) -> i64 {
		let uid = self.next_item_uid;
		self.next_item_uid += 1;
		uid
	}

	pub fn to_json(&self) -> serde_json::Value {
		serde_json::to_value(self).expect("a run state always serialises")
	}

	pub fn from_json(raw: &serde_json::Value) -> Result<RunState, serde_json::Error> {
		RunState::deserialize(raw)
	}
}
