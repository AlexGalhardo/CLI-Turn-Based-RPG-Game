/** Mutable run state. Serialised with camelCase keys: the save format is shared with Python and Go. */
import type { MonsterAttack, StatusOnHit } from "./definitions";
import { ELEMENTS, parseEnum, SLOTS, type Slot, STATS, type Stat } from "./enums";
import { field, type JsonObject, type JsonValue, jsonBool, jsonInt, jsonList, jsonObj, jsonStr } from "./json-types";

export interface ActiveStatus {
	statusId: string;
	turns: number;
	perTurn: number;
}

export function statusToJson(status: ActiveStatus): JsonObject {
	return { statusId: status.statusId, turns: status.turns, perTurn: status.perTurn };
}

export function statusFromJson(raw: JsonValue): ActiveStatus {
	const data = jsonObj(raw);
	return {
		statusId: jsonStr(field(data, "statusId")),
		turns: jsonInt(field(data, "turns")),
		perTurn: jsonInt(field(data, "perTurn")),
	};
}

export interface AffixRoll {
	readonly stat: Stat;
	readonly value: number;
}

export interface ItemInstance {
	readonly uid: number;
	readonly itemId: string;
	readonly rarity: string;
	readonly tier: number;
	readonly affixes: readonly AffixRoll[];
}

export function itemToJson(item: ItemInstance): JsonObject {
	return {
		uid: item.uid,
		itemId: item.itemId,
		rarity: item.rarity,
		tier: item.tier,
		affixes: item.affixes.map((affix) => ({ stat: affix.stat, value: affix.value })),
	};
}

export function itemFromJson(raw: JsonValue): ItemInstance {
	const data = jsonObj(raw);
	return {
		uid: jsonInt(field(data, "uid")),
		itemId: jsonStr(field(data, "itemId")),
		rarity: jsonStr(field(data, "rarity")),
		tier: jsonInt(field(data, "tier")),
		affixes: jsonList(field(data, "affixes")).map((entry) => {
			const affix = jsonObj(entry);
			return {
				stat: parseEnum(STATS, jsonStr(field(affix, "stat")), "stat"),
				value: jsonInt(field(affix, "value")),
			};
		}),
	};
}

/** Sorted-key copy of a string→number record (Python sorts dict keys in to_dict). */
function sortedRecord(record: ReadonlyMap<string, number>): JsonObject {
	const result: JsonObject = {};
	for (const key of [...record.keys()].sort()) {
		result[key] = record.get(key) ?? 0;
	}
	return result;
}

function intMap(raw: JsonValue): Map<string, number> {
	return new Map(Object.entries(jsonObj(raw)).map(([key, value]) => [key, jsonInt(value)]));
}

export class Player {
	level = 1;
	xp = 0;
	magicLevel = 1;
	manaSpent = 0;
	potions = new Map<string, number>();
	equipment = new Map<Slot, ItemInstance>();
	bag: ItemInstance[] = [];
	spellUses = new Map<string, number>();
	statuses: ActiveStatus[] = [];
	stunCooldown = 0;
	defending = false;

	constructor(
		public name: string,
		public vocationId: string,
		public hp: number,
		public mp: number,
		public gold: number,
	) {}

	potionCount(potionId: string): number {
		return this.potions.get(potionId) ?? 0;
	}

	toJson(): JsonObject {
		const equipment: JsonObject = {};
		for (const slot of [...this.equipment.keys()].sort()) {
			const item = this.equipment.get(slot);
			if (item !== undefined) equipment[slot] = itemToJson(item);
		}
		return {
			name: this.name,
			vocationId: this.vocationId,
			hp: this.hp,
			mp: this.mp,
			gold: this.gold,
			level: this.level,
			xp: this.xp,
			magicLevel: this.magicLevel,
			manaSpent: this.manaSpent,
			potions: sortedRecord(this.potions),
			equipment,
			bag: this.bag.map(itemToJson),
			spellUses: sortedRecord(this.spellUses),
			statuses: this.statuses.map(statusToJson),
			stunCooldown: this.stunCooldown,
			defending: this.defending,
		};
	}

	static fromJson(raw: JsonValue): Player {
		const data = jsonObj(raw);
		const player = new Player(
			jsonStr(field(data, "name")),
			jsonStr(field(data, "vocationId")),
			jsonInt(field(data, "hp")),
			jsonInt(field(data, "mp")),
			jsonInt(field(data, "gold")),
		);
		player.level = jsonInt(field(data, "level"));
		player.xp = jsonInt(field(data, "xp"));
		player.magicLevel = jsonInt(field(data, "magicLevel"));
		player.manaSpent = jsonInt(field(data, "manaSpent"));
		player.potions = intMap(field(data, "potions"));
		player.equipment = new Map(
			Object.entries(jsonObj(field(data, "equipment"))).map(([slot, item]) => [
				parseEnum(SLOTS, slot, "slot"),
				itemFromJson(item),
			]),
		);
		player.bag = jsonList(field(data, "bag")).map(itemFromJson);
		player.spellUses = intMap(field(data, "spellUses"));
		player.statuses = jsonList(field(data, "statuses")).map(statusFromJson);
		player.stunCooldown = jsonInt(field(data, "stunCooldown"));
		player.defending = jsonBool(field(data, "defending"));
		return player;
	}
}

function attackToJson(attack: MonsterAttack): JsonObject {
	const result: JsonObject = {
		id: attack.id,
		element: attack.element,
		min: attack.min,
		max: attack.max,
		weight: attack.weight,
	};
	if (attack.status !== null) {
		result.status = { id: attack.status.status, chance: attack.status.chance, damagePct: attack.status.damagePct };
	}
	return result;
}

export function attackFromJson(raw: JsonValue): MonsterAttack {
	const data = jsonObj(raw);
	const statusRaw = data.status;
	let status: StatusOnHit | null = null;
	if (statusRaw !== undefined && statusRaw !== null) {
		const statusData = jsonObj(statusRaw);
		status = {
			status: jsonStr(field(statusData, "id")),
			chance: jsonInt(field(statusData, "chance")),
			damagePct: jsonInt(field(statusData, "damagePct")),
		};
	}
	return {
		id: jsonStr(field(data, "id")),
		element: parseEnum(ELEMENTS, jsonStr(field(data, "element")), "element"),
		min: jsonInt(field(data, "min")),
		max: jsonInt(field(data, "max")),
		weight: jsonInt(field(data, "weight")),
		status,
	};
}

/** A spawned monster: definition id plus stats already scaled for the round and difficulty. */
export class MonsterInstance {
	statuses: ActiveStatus[] = [];
	stunCooldown = 0;
	bossActions = 0;

	constructor(
		public creatureId: string,
		public isBoss: boolean,
		public enemyClass: string,
		public hp: number,
		public maxHp: number,
		public xp: number,
		public goldMin: number,
		public goldMax: number,
		public attacks: readonly MonsterAttack[],
	) {}

	attack(attackId: string): MonsterAttack {
		const attack = this.attacks.find((candidate) => candidate.id === attackId);
		if (attack === undefined) throw new RangeError(`unknown attack: ${attackId}`);
		return attack;
	}

	toJson(): JsonObject {
		return {
			creatureId: this.creatureId,
			isBoss: this.isBoss,
			enemyClass: this.enemyClass,
			hp: this.hp,
			maxHp: this.maxHp,
			xp: this.xp,
			goldMin: this.goldMin,
			goldMax: this.goldMax,
			attacks: this.attacks.map(attackToJson),
			statuses: this.statuses.map(statusToJson),
			stunCooldown: this.stunCooldown,
			bossActions: this.bossActions,
		};
	}

	static fromJson(raw: JsonValue): MonsterInstance {
		const data = jsonObj(raw);
		const monster = new MonsterInstance(
			jsonStr(field(data, "creatureId")),
			jsonBool(field(data, "isBoss")),
			jsonStr(field(data, "enemyClass")),
			jsonInt(field(data, "hp")),
			jsonInt(field(data, "maxHp")),
			jsonInt(field(data, "xp")),
			jsonInt(field(data, "goldMin")),
			jsonInt(field(data, "goldMax")),
			jsonList(field(data, "attacks")).map(attackFromJson),
		);
		monster.statuses = jsonList(field(data, "statuses")).map(statusFromJson);
		monster.stunCooldown = jsonInt(field(data, "stunCooldown"));
		monster.bossActions = jsonInt(field(data, "bossActions"));
		return monster;
	}
}
