/** Immutable game definitions loaded from shared/data (see docs/data-format.md). */
import type { Element, Resource, Slot, SpellKind, Stat, StatusKind } from "./enums";

export class UnknownIdError extends Error {
	constructor(readonly id: string) {
		super(`unknown id: ${id}`);
		this.name = "UnknownIdError";
	}
}

export interface StatusOnHit {
	readonly status: string;
	readonly chance: number;
	readonly damagePct: number;
}

export interface MonsterAttack {
	readonly id: string;
	readonly element: Element;
	readonly min: number;
	readonly max: number;
	readonly weight: number;
	readonly status: StatusOnHit | null;
}

export class MonsterDef {
	constructor(
		readonly id: string,
		readonly name: string,
		readonly tier: number,
		readonly family: string,
		readonly hp: number,
		readonly xp: number,
		readonly goldMin: number,
		readonly goldMax: number,
		readonly attacks: readonly MonsterAttack[],
		readonly resistances: Readonly<Partial<Record<Element, number>>>,
		readonly isBoss: boolean,
		readonly chargeAttack: string | null,
	) {}

	resistance(element: Element): number {
		return this.resistances[element] ?? 100;
	}

	attack(attackId: string): MonsterAttack {
		const attack = this.attacks.find((candidate) => candidate.id === attackId);
		if (attack === undefined) throw new UnknownIdError(attackId);
		return attack;
	}
}

export interface Level3Bonus {
	readonly status: string | null;
	readonly chance: number;
	readonly cleanse: boolean;
}

export interface SpellDef {
	readonly id: string;
	readonly name: string;
	readonly words: string;
	readonly kind: SpellKind;
	readonly element: Element;
	readonly mana: number;
	readonly min: number;
	readonly max: number;
	readonly perLevel: number;
	readonly perMagicLevel: number;
	readonly level3Bonus: Level3Bonus;
}

export interface VocationDef {
	readonly id: string;
	readonly name: string;
	readonly startHp: number;
	readonly startMp: number;
	readonly hpPerLevel: number;
	readonly mpPerLevel: number;
	readonly hpRegen: number;
	readonly mpRegen: number;
	readonly meleeMin: number;
	readonly meleeMax: number;
	readonly meleePerLevel: number;
	readonly weaponTypes: readonly string[];
	readonly shieldTypes: readonly string[];
	readonly starterWeapon: string;
	readonly spells: readonly string[];
}

export interface PotionDef {
	readonly id: string;
	readonly name: string;
	readonly resource: Resource;
	readonly min: number;
	readonly max: number;
	readonly price: number;
	readonly unlockRound: number;
}

export interface StatusDef {
	readonly id: string;
	readonly kind: StatusKind;
	readonly element: Element;
	readonly turns: number;
}

export interface ItemDef {
	readonly id: string;
	readonly name: string;
	readonly slot: Slot;
	readonly type: string;
	readonly tier: number;
	readonly element: Element | null;
	/** Stats in file order (order never matters for sums). */
	readonly stats: ReadonlyArray<readonly [Stat, number]>;
	readonly value: number;
}

export interface AffixDef {
	readonly id: string;
	readonly stat: Stat;
	readonly min: number;
	readonly max: number;
	readonly perTier: number;
	readonly slots: readonly Slot[];
}

export interface AchievementDef {
	readonly id: string;
	readonly type: string;
	readonly value: number;
}

export interface DifficultyDef {
	readonly id: string;
	readonly hpPct: number;
	readonly damagePct: number;
	readonly goldPct: number;
	readonly xpPct: number;
	readonly nonCommonWeightPct: number;
}

export interface RarityDef {
	readonly id: string;
	readonly statPct: number;
	readonly valuePct: number;
	readonly affixMin: number;
	readonly affixMax: number;
}

export interface SpellLevelDef {
	readonly level: number;
	readonly uses: number;
	readonly effectPct: number;
	readonly manaPct: number;
}

export interface Caps {
	readonly critChance: number;
	readonly dodge: number;
	readonly parry: number;
	readonly leech: number;
	readonly protection: number;
}

export class Balance {
	constructor(
		readonly roundsPerTier: number,
		readonly cycleStatPct: number,
		readonly cycleRewardPct: number,
		readonly positionPct: number,
		readonly difficulties: readonly DifficultyDef[],
		readonly critMultiplierPct: number,
		readonly defendDamagePct: number,
		readonly bossTelegraphEvery: number,
		readonly bossChargeDamagePct: number,
		readonly caps: Caps,
		readonly magicLevelBase: number,
		readonly magicLevelGrowthPct: number,
		readonly spellLevels: readonly SpellLevelDef[],
		readonly startingGold: number,
		readonly startingPotions: ReadonlyArray<readonly [string, number]>,
		readonly bagCapacity: number,
		readonly dropChancePct: number,
		readonly bossDrops: number,
		readonly rarities: readonly RarityDef[],
		readonly rarityWeights: Readonly<Record<string, Readonly<Record<string, number>>>>,
		readonly merchantStockSize: number,
		readonly merchantMarkupPct: number,
		readonly spellStatusDamagePct: number,
	) {}

	difficulty(difficultyId: string): DifficultyDef {
		const difficulty = this.difficulties.find((d) => d.id === difficultyId);
		if (difficulty === undefined) throw new UnknownIdError(difficultyId);
		return difficulty;
	}

	rarity(rarityId: string): RarityDef {
		const rarity = this.rarities.find((r) => r.id === rarityId);
		if (rarity === undefined) throw new UnknownIdError(rarityId);
		return rarity;
	}
}

/** Code-point order, like Python's default string comparison (never localeCompare). */
export function byId<T extends { readonly id: string }>(a: T, b: T): number {
	return a.id < b.id ? -1 : a.id > b.id ? 1 : 0;
}

function index<T extends { readonly id: string }>(items: readonly T[]): Map<string, T> {
	return new Map(items.map((item) => [item.id, item]));
}

function lookup<T>(map: ReadonlyMap<string, T>, key: string): T {
	const value = map.get(key);
	if (value === undefined) throw new UnknownIdError(key);
	return value;
}

export interface GameDataInit {
	readonly balance: Balance;
	readonly vocations: readonly VocationDef[];
	readonly spells: readonly SpellDef[];
	readonly monsters: readonly MonsterDef[];
	readonly bosses: readonly MonsterDef[];
	readonly potions: readonly PotionDef[];
	readonly statuses: readonly StatusDef[];
	readonly items: readonly ItemDef[];
	readonly affixes?: readonly AffixDef[];
	readonly achievements?: readonly AchievementDef[];
	readonly families?: readonly string[];
}

export class GameData {
	readonly balance: Balance;
	readonly vocations: readonly VocationDef[];
	readonly spells: readonly SpellDef[];
	readonly monsters: readonly MonsterDef[];
	readonly bosses: readonly MonsterDef[];
	readonly potions: readonly PotionDef[];
	readonly statuses: readonly StatusDef[];
	readonly items: readonly ItemDef[];
	readonly affixes: readonly AffixDef[];
	readonly achievements: readonly AchievementDef[];
	readonly families: readonly string[];
	readonly #vocations: Map<string, VocationDef>;
	readonly #spells: Map<string, SpellDef>;
	readonly #creatures: Map<string, MonsterDef>;
	readonly #potions: Map<string, PotionDef>;
	readonly #statuses: Map<string, StatusDef>;
	readonly #items: Map<string, ItemDef>;
	readonly #monstersByTier = new Map<number, MonsterDef[]>();
	readonly #bossesByTier = new Map<number, MonsterDef>();

	constructor(init: GameDataInit) {
		this.balance = init.balance;
		this.vocations = init.vocations;
		this.spells = init.spells;
		this.monsters = init.monsters;
		this.bosses = init.bosses;
		this.potions = init.potions;
		this.statuses = init.statuses;
		this.items = init.items;
		this.affixes = init.affixes ?? [];
		this.achievements = init.achievements ?? [];
		this.families = init.families ?? [];
		this.#vocations = index(this.vocations);
		this.#spells = index(this.spells);
		this.#creatures = index([...this.monsters, ...this.bosses]);
		this.#potions = index(this.potions);
		this.#statuses = index(this.statuses);
		this.#items = index(this.items);
		for (const monster of this.monsters) {
			const group = this.#monstersByTier.get(monster.tier) ?? [];
			group.push(monster);
			this.#monstersByTier.set(monster.tier, group);
		}
		for (const group of this.#monstersByTier.values()) group.sort(byId);
		for (const boss of this.bosses) this.#bossesByTier.set(boss.tier, boss);
	}

	/** Returns a copy with some fields replaced (used by tests, like Python's dataclasses.replace). */
	with(overrides: Partial<GameDataInit>): GameData {
		return new GameData({
			balance: this.balance,
			vocations: this.vocations,
			spells: this.spells,
			monsters: this.monsters,
			bosses: this.bosses,
			potions: this.potions,
			statuses: this.statuses,
			items: this.items,
			affixes: this.affixes,
			achievements: this.achievements,
			families: this.families,
			...overrides,
		});
	}

	get tierCount(): number {
		return this.bosses.length;
	}

	vocation(id: string): VocationDef {
		return lookup(this.#vocations, id);
	}

	spell(id: string): SpellDef {
		return lookup(this.#spells, id);
	}

	creature(id: string): MonsterDef {
		return lookup(this.#creatures, id);
	}

	potion(id: string): PotionDef {
		return lookup(this.#potions, id);
	}

	status(id: string): StatusDef {
		return lookup(this.#statuses, id);
	}

	item(id: string): ItemDef {
		return lookup(this.#items, id);
	}

	monstersInTier(tier: number): readonly MonsterDef[] {
		return this.#monstersByTier.get(tier) ?? [];
	}

	bossOfTier(tier: number): MonsterDef {
		const boss = this.#bossesByTier.get(tier);
		if (boss === undefined) throw new UnknownIdError(`boss of tier ${tier}`);
		return boss;
	}
}
