//! Deterministic run counters, derived only from engine events (docs/game-design.md §11).

use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};

use crate::application::events::Event;
use crate::domain::enums::{EnemyClass, Resource, Target};

/// A `collections.Counter`: keys serialise sorted, missing keys count as zero.
pub type Counter = BTreeMap<String, i64>;

fn increment(counter: &mut Counter, key: &str, amount: i64) {
	*counter.entry(key.to_owned()).or_insert(0) += amount;
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DroppedItem {
	pub item_id: String,
	pub rarity: String,
	pub round: i64,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RunStatistics {
	pub damage_dealt: i64,
	pub damage_taken: i64,
	pub healing_done: i64,
	pub highest_hit: i64,
	pub normal_attacks: i64,
	pub crits: i64,
	pub dodges: i64,
	pub parries: i64,
	pub defends: i64,
	pub gold_looted: i64,
	pub gold_spent: i64,
	pub gold_earned: i64,
	pub items_sold: i64,
	pub items_auto_equipped: i64,
	pub bosses_killed: i64,
	pub elites_killed: i64,
	pub spells_cast: Counter,
	pub potions_used: Counter,
	pub potions_bought: Counter,
	pub potions_dropped: Counter,
	pub items_dropped: Counter,
	pub kills: Counter,
	pub statuses_applied: Counter,
	pub dropped_items: Vec<DroppedItem>,
}

impl RunStatistics {
	pub fn record(&mut self, events: &[Event], current_round: i64) {
		for event in events {
			self.record_one(event, current_round);
		}
	}

	fn record_one(&mut self, event: &Event, current_round: i64) {
		match event {
			Event::PlayerAttacked { damage, crit, .. } => {
				self.normal_attacks += 1;
				self.dealt(*damage, *crit);
			}
			Event::SpellCast { spell_id, damage, crit, .. } => {
				increment(&mut self.spells_cast, spell_id, 1);
				self.dealt(*damage, *crit);
			}
			Event::SpellHealed { spell_id, amount, .. } => {
				increment(&mut self.spells_cast, spell_id, 1);
				self.healing_done += amount;
			}
			Event::PotionUsed { potion_id, amount, resource } => {
				increment(&mut self.potions_used, potion_id, 1);
				if *resource == Resource::Hp {
					self.healing_done += amount;
				}
			}
			Event::PlayerDefended => self.defends += 1,
			Event::MonsterAttacked { damage, .. } => self.damage_taken += damage,
			Event::AttackDodged { .. } => self.dodges += 1,
			Event::AttackParried { reflected, .. } => {
				self.parries += 1;
				self.damage_dealt += reflected;
			}
			Event::MonsterParried { reflected } => self.damage_taken += reflected,
			Event::StatusTicked { target, damage, .. } => {
				if *target == Target::Player {
					self.damage_taken += damage;
				} else {
					self.damage_dealt += damage;
				}
			}
			Event::StatusApplied { target: Target::Monster, status, .. } => {
				increment(&mut self.statuses_applied, status, 1);
			}
			Event::MonsterKilled { monster_id, is_boss, enemy_class } => {
				increment(&mut self.kills, monster_id, 1);
				if *is_boss {
					self.bosses_killed += 1;
				}
				if *enemy_class == EnemyClass::Elite {
					self.elites_killed += 1;
				}
			}
			Event::GoldLooted { amount } => self.gold_looted += amount,
			Event::ItemDropped { item_id, rarity, .. } => {
				increment(&mut self.items_dropped, rarity, 1);
				self.dropped_items.push(DroppedItem {
					item_id: item_id.clone(),
					rarity: rarity.clone(),
					round: current_round,
				});
			}
			Event::PotionBought { potion_id, quantity, gold } => {
				increment(&mut self.potions_bought, potion_id, *quantity);
				self.gold_spent += gold;
			}
			Event::ItemBought { gold, .. } => self.gold_spent += gold,
			Event::PotionDropped { potion_id } => increment(&mut self.potions_dropped, potion_id, 1),
			Event::ItemAutoEquipped { .. } => self.items_auto_equipped += 1,
			Event::ItemSold { gold, .. } | Event::ItemAutoSold { gold, .. } => {
				self.items_sold += 1;
				self.gold_earned += gold;
			}
			_ => {}
		}
	}

	fn dealt(&mut self, damage: i64, crit: bool) {
		self.damage_dealt += damage;
		self.highest_hit = self.highest_hit.max(damage);
		if crit {
			self.crits += 1;
		}
	}

	pub fn total_kills(&self) -> i64 {
		self.kills.values().sum()
	}
}
