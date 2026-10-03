/** Item factory: base item + rarity + affixes (docs/game-design.md §8). */
import { byId, type GameData, type ItemDef, type RarityDef, type VocationDef } from "../domain/definitions";
import type { AffixRoll, ItemInstance } from "../domain/entities";
import type { Stat } from "../domain/enums";
import type { Rng } from "../domain/rng";

export function canUse(item: ItemDef, vocation: VocationDef): boolean {
	if (item.slot === "weapon") return vocation.weaponTypes.includes(item.type);
	if (item.slot === "shield") return vocation.shieldTypes.includes(item.type);
	return true;
}

/** Weighted roll in the order of `balance.rarities`; zero weights are skipped and a single option is not rolled. */
export function rollRarity(data: GameData, rng: Rng, weights: Readonly<Record<string, number>>): RarityDef {
	const options = data.balance.rarities
		.map((rarity) => [rarity, weights[rarity.id] ?? 0] as const)
		.filter(([, weight]) => weight > 0);
	const only = options[0];
	if (only === undefined) throw new RangeError("rarity table without a positive weight");
	if (options.length === 1) return only[0];
	const picked = options[rng.weighted(options.map(([, weight]) => weight))];
	if (picked === undefined) throw new Error("rarity index out of range");
	return picked[0];
}

export interface ItemRequest {
	readonly vocation: VocationDef;
	readonly tier: number;
	readonly weights: Readonly<Record<string, number>>;
	readonly uid: number;
}

/** Returns null (consuming no randomness) when no item fits the vocation and tier. */
export function generateItem(data: GameData, rng: Rng, request: ItemRequest): ItemInstance | null {
	const { vocation, tier, weights, uid } = request;
	const lowestTier = Math.max(0, tier - 1);
	const candidates = data.items
		.filter((item) => lowestTier <= item.tier && item.tier <= tier && canUse(item, vocation))
		.sort(byId);
	if (candidates.length === 0) return null;
	const base = rng.pick(candidates);
	const rarity = rollRarity(data, rng, weights);
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
