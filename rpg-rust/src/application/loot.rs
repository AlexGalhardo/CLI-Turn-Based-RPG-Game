//! Item factory: base item + rarity + affixes (docs/game-design.md §8).

use std::collections::BTreeMap;

use crate::domain::definitions::{GameData, ItemDef, RarityDef, VocationDef};
use crate::domain::entities::{AffixRoll, ItemInstance};
use crate::domain::enums::{Slot, Stat};
use crate::domain::rng::Rng;

pub fn can_use(item: &ItemDef, vocation: &VocationDef) -> bool {
	match item.slot {
		Slot::Weapon => vocation.weapon_types.contains(&item.item_type),
		Slot::Shield => vocation.shield_types.contains(&item.item_type),
		_ => true,
	}
}

/// Weighted roll in the order of `balance.rarities`; zero weights are skipped and a single option is not rolled.
pub fn roll_rarity<'a>(data: &'a GameData, rng: &mut Rng, weights: &BTreeMap<String, i64>) -> &'a RarityDef {
	let options: Vec<(&RarityDef, i64)> = data
		.balance
		.rarities
		.iter()
		.map(|rarity| (rarity, weights.get(&rarity.id).copied().unwrap_or(0)))
		.filter(|(_, weight)| *weight > 0)
		.collect();
	match options.as_slice() {
		[] => panic!("rarity table without a positive weight"),
		[(single, _)] => single,
		_ => {
			let weights: Vec<i64> = options.iter().map(|(_, weight)| *weight).collect();
			options[rng.weighted(&weights)].0
		}
	}
}

/// Returns `None` (consuming no randomness) when no item fits the vocation and tier.
pub fn generate_item(
	data: &GameData,
	rng: &mut Rng,
	vocation: &VocationDef,
	tier: i64,
	weights: &BTreeMap<String, i64>,
	uid: i64,
) -> Option<ItemInstance> {
	let lowest_tier = 0.max(tier - 1);
	let mut candidates: Vec<&ItemDef> =
		data.items.iter().filter(|item| (lowest_tier..=tier).contains(&item.tier) && can_use(item, vocation)).collect();
	if candidates.is_empty() {
		return None;
	}
	// `str` ordering is byte-wise, which is code-point order for UTF-8: the same as Python's sorted().
	candidates.sort_by(|a, b| a.id.cmp(&b.id));
	let base = *rng.pick(&candidates);
	let rarity = roll_rarity(data, rng, weights);
	let affix_count = rng.roll(rarity.affix_min, rarity.affix_max);

	let mut rolls: Vec<AffixRoll> = Vec::new();
	let mut used_stats: Vec<Stat> = Vec::new();
	for _ in 0..affix_count {
		let mut pool: Vec<_> = data
			.affixes
			.iter()
			.filter(|affix| affix.slots.contains(&base.slot) && !used_stats.contains(&affix.stat))
			.collect();
		if pool.is_empty() {
			break;
		}
		pool.sort_by(|a, b| a.id.cmp(&b.id));
		let affix = *rng.pick(&pool);
		used_stats.push(affix.stat);
		rolls.push(AffixRoll { stat: affix.stat, value: rng.roll(affix.min, affix.max) + tier * affix.per_tier });
	}
	Some(ItemInstance { uid, item_id: base.id.clone(), rarity: rarity.id.clone(), tier, affixes: rolls })
}
