/** Pure integer formulas from docs/game-design.md. No randomness, no state. */
import type { Balance, DifficultyDef, SpellLevelDef } from "./definitions";

/** floor(value * percent / 100) for non-negative operands — the only rounding rule of the engine. */
export function pct(value: number, percent: number): number {
	if (value < 0 || percent < 0) {
		throw new RangeError("pct operands must be non-negative");
	}
	return Math.floor((value * percent) / 100);
}

export function clamp(value: number, minimum: number, maximum: number): number {
	return Math.max(minimum, Math.min(maximum, value));
}

/** Total experience needed to reach `level` (Tibia formula). */
export function xpForLevel(level: number): number {
	if (level <= 1) return 0;
	return Math.floor((50 * (level ** 3 - 6 * level ** 2 + 17 * level - 12)) / 3);
}

/** Total mana spent (since level 1) needed to advance from `magicLevel` to the next one. */
export function manaForMagicLevel(magicLevel: number, balance: Balance): number {
	let step = balance.magicLevelBase;
	let total = step;
	for (let level = 1; level < magicLevel; level++) {
		step = pct(step, balance.magicLevelGrowthPct);
		total += step;
	}
	return total;
}

export function spellLevelForUses(uses: number, levels: readonly SpellLevelDef[]): SpellLevelDef {
	let current = levels[0];
	for (const level of levels) {
		if (uses >= level.uses) current = level;
	}
	if (current === undefined) throw new RangeError("no spell levels configured");
	return current;
}

export function armorMitigation(damage: number, armor: number): number {
	return Math.floor((damage * 100) / (100 + armor));
}

export interface RoundInfo {
	readonly round: number;
	readonly tier: number;
	readonly cycle: number;
	readonly position: number;
	readonly isBoss: boolean;
}

export function roundInfo(roundNumber: number, balance: Balance, tierCount: number): RoundInfo {
	const index = roundNumber - 1;
	const perTier = balance.roundsPerTier;
	const position = index % perTier;
	return {
		round: roundNumber,
		tier: Math.floor(index / perTier) % tierCount,
		cycle: Math.floor(index / (perTier * tierCount)),
		position,
		isBoss: position === perTier - 1,
	};
}

export interface Scaling {
	readonly hpPctProduct: number;
	readonly damagePctProduct: number;
	readonly rewardXpPctProduct: number;
	readonly rewardGoldPctProduct: number;
}

export function scaling(info: RoundInfo, balance: Balance, difficulty: DifficultyDef): Scaling {
	const cyclePct = 100 + info.cycle * balance.cycleStatPct;
	const rewardPct = 100 + info.cycle * balance.cycleRewardPct;
	const positionPct = info.isBoss ? 100 : 100 + info.position * balance.positionPct;
	return {
		hpPctProduct: difficulty.hpPct * cyclePct * positionPct,
		damagePctProduct: difficulty.damagePct * cyclePct * positionPct,
		rewardXpPctProduct: difficulty.xpPct * rewardPct,
		rewardGoldPctProduct: difficulty.goldPct * rewardPct,
	};
}

/** Applies three chained percentages in one floor: value * a * b * c / 1_000_000. */
export function scaleStat(value: number, pctProduct: number): number {
	return Math.floor((value * pctProduct) / 1_000_000);
}

/** Applies two chained percentages in one floor: value * a * b / 10_000. */
export function scaleReward(value: number, pctProduct: number): number {
	return Math.floor((value * pctProduct) / 10_000);
}
