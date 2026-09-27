/** Item factory: base item + rarity + affixes (docs/game-design.md §8). */
import { byId, type DifficultyDef, type GameData, type ItemDef, type VocationDef } from "../domain/definitions";
import type { AffixRoll, ItemInstance } from "../domain/entities";
import type { Stat } from "../domain/enums";
import { pct } from "../domain/formulas";
import type { Rng } from "../domain/rng";

export function canUse(item: ItemDef, vocation: VocationDef): boolean {
	if (item.slot === "weapon") return vocation.weaponTypes.includes(item.type);
	if (item.slot === "shield") return vocation.shieldTypes.includes(item.type);
	return true;
}

export function rarityWeights(data: GameData, table: string, difficulty: DifficultyDef): number[] {
	const weights = data.balance.rarityWeights[table] ?? {};
	return data.balance.rarities.map((rarity) => {
		const weight = weights[rarity.id] ?? 0;
		return rarity.id === "common" ? weight : pct(weight, difficulty.nonCommonWeightPct);
	});
}

export interface ItemRequest {
	readonly vocation: VocationDef;
	readonly tier: number;
	readonly table: string;
	readonly difficulty: DifficultyDef;
	readonly uid: number;
}

/** Returns null (consuming no randomness) when no item fits the vocation and tier. */
export function generateItem(data: GameData, rng: Rng, request: ItemRequest): ItemInstance | null {
	const { vocation, tier, table, difficulty, uid } = request;
	const lowestTier = Math.max(0, tier - 1);
	const candidates = data.items
		.filter((item) => lowestTier <= item.tier && item.tier <= tier && canUse(item, vocation))
		.sort(byId);
	if (candidates.length === 0) return null;
	const base = rng.pick(candidates);
	const rarity = data.balance.rarities[rng.weighted(rarityWeights(data, table, difficulty))];
	if (rarity === undefined) throw new Error("rarity index out of range");
	const affixCount = rng.roll(rarity.affixMin, rarity.affixMax);

	const rolls: AffixRoll[] = [];
	const usedStats = new Set<Stat>();
	for (let i = 0; i < affixCount; i++) {
		const pool = data.affixes
			.filter((affix) => affix.slots.includes(base.slot) && !usedStats.has(affix.stat))
			.sort(byId);
		if (pool.length === 0) break;
		const affix = rng.pick(pool);
		usedStats.add(affix.stat);
		rolls.push({ stat: affix.stat, value: rng.roll(affix.min, affix.max) + tier * affix.perTier });
	}
	return { uid, itemId: base.id, rarity: rarity.id, tier, affixes: rolls };
}
