//! Pure integer formulas from docs/game-design.md. No randomness, no state.

use crate::domain::definitions::{Balance, DifficultyDef, SpellLevelDef};

/// `floor(value * percent / 100)` for non-negative operands — the only rounding rule of the engine.
///
/// Integer division truncates toward zero in Rust, which equals `floor` because both operands are non-negative.
pub fn pct(value: i64, percent: i64) -> i64 {
	assert!(value >= 0 && percent >= 0, "pct operands must be non-negative");
	value * percent / 100
}

pub fn clamp(value: i64, minimum: i64, maximum: i64) -> i64 {
	minimum.max(maximum.min(value))
}

/// Total experience needed to reach `level` (Tibia formula).
pub fn xp_for_level(level: i64) -> i64 {
	if level <= 1 {
		return 0;
	}
	50 * (level.pow(3) - 6 * level.pow(2) + 17 * level - 12) / 3
}

/// Total mana spent (since level 1) needed to advance from `magic_level` to the next one.
pub fn mana_for_magic_level(magic_level: i64, balance: &Balance) -> i64 {
	let mut step = balance.magic_level.base;
	let mut total = step;
	for _ in 1..magic_level {
		step = pct(step, balance.magic_level.growth_pct);
		total += step;
	}
	total
}

pub fn spell_level_for_uses(uses: i64, levels: &[SpellLevelDef]) -> &SpellLevelDef {
	let mut current = &levels[0];
	for level in levels {
		if uses >= level.uses {
			current = level;
		}
	}
	current
}

pub fn armor_mitigation(damage: i64, armor: i64) -> i64 {
	damage * 100 / (100 + armor)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RoundInfo {
	pub round: i64,
	pub tier: i64,
	pub cycle: i64,
	pub position: i64,
	pub is_boss: bool,
}

pub fn round_info(round_number: i64, balance: &Balance, tier_count: i64) -> RoundInfo {
	let index = round_number - 1;
	let per_tier = balance.rounds_per_tier;
	let position = index % per_tier;
	RoundInfo {
		round: round_number,
		tier: (index / per_tier) % tier_count,
		cycle: index / (per_tier * tier_count),
		position,
		is_boss: position == per_tier - 1,
	}
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Scaling {
	pub hp_pct_product: i64,
	pub damage_pct_product: i64,
	pub reward_xp_pct_product: i64,
	pub reward_gold_pct_product: i64,
}

pub fn scaling(info: &RoundInfo, balance: &Balance, difficulty: &DifficultyDef) -> Scaling {
	let cycle_pct = 100 + info.cycle * balance.cycle_stat_pct;
	let reward_pct = 100 + info.cycle * balance.cycle_reward_pct;
	let position_pct = if info.is_boss { 100 } else { 100 + info.position * balance.position_pct };
	Scaling {
		hp_pct_product: difficulty.hp_pct * cycle_pct * position_pct,
		damage_pct_product: difficulty.damage_pct * cycle_pct * position_pct,
		reward_xp_pct_product: difficulty.xp_pct * reward_pct,
		reward_gold_pct_product: difficulty.gold_pct * reward_pct,
	}
}

/// Applies three chained percentages in one floor: `value * a * b * c / 1_000_000`.
pub fn scale_stat(value: i64, pct_product: i64) -> i64 {
	value * pct_product / 1_000_000
}

/// Applies two chained percentages in one floor: `value * a * b / 10_000`.
pub fn scale_reward(value: i64, pct_product: i64) -> i64 {
	value * pct_product / 10_000
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn pct_floors() {
		assert_eq!(pct(99, 50), 49);
		assert_eq!(pct(0, 150), 0);
		assert_eq!(pct(7, 100), 7);
	}

	#[test]
	#[should_panic(expected = "non-negative")]
	fn pct_rejects_negative() {
		pct(-1, 10);
	}

	#[test]
	fn clamp_bounds() {
		assert_eq!(clamp(5, 0, 3), 3);
		assert_eq!(clamp(-1, 0, 3), 0);
		assert_eq!(clamp(2, 0, 3), 2);
	}

	#[test]
	fn xp_for_level_matches_tibia_table() {
		for (level, xp) in [(1, 0), (2, 100), (3, 200), (4, 400), (5, 800), (10, 9300), (20, 98800), (100, 15_694_800)]
		{
			assert_eq!(xp_for_level(level), xp, "level {level}");
		}
	}

	#[test]
	fn armor_mitigation_values() {
		assert_eq!(armor_mitigation(100, 0), 100);
		assert_eq!(armor_mitigation(100, 100), 50);
		assert_eq!(armor_mitigation(10, 3), 9);
	}

	#[test]
	fn scale_helpers_floor_once() {
		assert_eq!(scale_stat(1000, 125 * 160 * 108), 1000 * 125 * 160 * 108 / 1_000_000);
		assert_eq!(scale_reward(99, 125 * 150), 99 * 125 * 150 / 10_000);
	}
}
