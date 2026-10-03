//! Item factory: base item + rarity + affixes (docs/game-design.md §8).

use crate::domain::definitions::{DifficultyDef, GameData, ItemDef, VocationDef};
use crate::domain::entities::{AffixRoll, ItemInstance};
use crate::domain::enums::{Slot, Stat};
use crate::domain::formulas::pct;
use crate::domain::rng::Rng;

pub fn can_use(item: &ItemDef, vocation: &VocationDef) -> bool {
	match item.slot {
		Slot::Weapon => vocation.weapon_types.contains(&item.item_type),
		Slot::Shield => vocation.shield_types.contains(&item.item_type),
		_ => true,
	}
}

pub fn rarity_weights(data: &GameData, table: &str, difficulty: &DifficultyDef) -> Vec<i64> {
	let weights = &data.balance.rarity_weights[table];
	data.balance
		.rarities
		.iter()
		.map(|rarity| {
			let weight = weights.get(&rarity.id).copied().unwrap_or(0);
			if rarity.id == "common" { weight } else { pct(weight, difficulty.non_common_weight_pct) }
		})
		.collect()
}

/// Returns `None` (consuming no randomness) when no item fits the vocation and tier.
pub fn generate_item(
	data: &GameData,
	rng: &mut Rng,
	vocation: &VocationDef,
	tier: i64,
	table: &str,
	difficulty: &DifficultyDef,
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
	let rarity = &data.balance.rarities[rng.weighted(&rarity_weights(data, table, difficulty))];
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
