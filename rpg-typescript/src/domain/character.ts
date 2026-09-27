/** Derived character stats: vocation base + equipment (docs/game-design.md §4). */
import type { GameData } from "./definitions";
import type { ItemInstance, Player } from "./entities";
import { type Element, PROTECTION_BY_ELEMENT, type Stat } from "./enums";
import { pct } from "./formulas";

export function itemStats(item: ItemInstance, data: GameData): Map<Stat, number> {
	const definition = data.item(item.itemId);
	const rarity = data.balance.rarity(item.rarity);
	const stats = new Map<Stat, number>();
	for (const [stat, value] of definition.stats) {
		stats.set(stat, (stats.get(stat) ?? 0) + pct(value, rarity.statPct));
	}
	for (const affix of item.affixes) {
		stats.set(affix.stat, (stats.get(affix.stat) ?? 0) + affix.value);
	}
	return stats;
}

export function itemValue(item: ItemInstance, data: GameData): number {
	return pct(data.item(item.itemId).value, data.balance.rarity(item.rarity).valuePct);
}

export interface CharacterSheet {
	readonly maxHp: number;
	readonly maxMp: number;
	readonly hpRegen: number;
	readonly mpRegen: number;
	readonly meleeMin: number;
	readonly meleeMax: number;
	readonly weaponElement: Element;
	readonly armor: number;
	readonly critChance: number;
	readonly critDamage: number;
	readonly spellPower: number;
	readonly physicalDamage: number;
	readonly dodge: number;
	readonly parry: number;
	readonly lifeLeech: number;
	readonly manaLeech: number;
	readonly protections: Readonly<Record<Element, number>>;
}

export function protection(sheet: CharacterSheet, element: Element): number {
	return sheet.protections[element];
}

export function buildSheet(player: Player, data: GameData): CharacterSheet {
	const vocation = data.vocation(player.vocationId);
	const caps = data.balance.caps;
	const totals = new Map<Stat, number>();
	for (const item of player.equipment.values()) {
		for (const [stat, value] of itemStats(item, data)) {
			totals.set(stat, (totals.get(stat) ?? 0) + value);
		}
	}
	const total = (stat: Stat): number => totals.get(stat) ?? 0;

	const weapon = player.equipment.get("weapon");
	const weaponElement: Element = weapon === undefined ? "physical" : (data.item(weapon.itemId).element ?? "physical");
	const levelBonus = (player.level - 1) * vocation.meleePerLevel;
	const attack = total("attack");
	const protections = Object.fromEntries(
		Object.entries(PROTECTION_BY_ELEMENT).map(([element, stat]) => [
			element,
			Math.min(total(stat), caps.protection),
		]),
	) as Record<Element, number>;

	return {
		maxHp: vocation.startHp + (player.level - 1) * vocation.hpPerLevel + total("maxHp"),
		maxMp: vocation.startMp + (player.level - 1) * vocation.mpPerLevel + total("maxMp"),
		hpRegen: vocation.hpRegen + total("hpRegen"),
		mpRegen: vocation.mpRegen + total("mpRegen"),
		meleeMin: vocation.meleeMin + levelBonus + attack,
		meleeMax: vocation.meleeMax + levelBonus + attack,
		weaponElement,
		armor: total("armor"),
		critChance: Math.min(total("critChance"), caps.critChance),
		critDamage: total("critDamage"),
		spellPower: total("spellPower"),
		physicalDamage: total("physicalDamage"),
		dodge: Math.min(total("dodge"), caps.dodge),
		parry: Math.min(total("parry"), caps.parry),
		lifeLeech: Math.min(total("lifeLeech"), caps.leech),
		manaLeech: Math.min(total("manaLeech"), caps.leech),
		protections,
	};
}
