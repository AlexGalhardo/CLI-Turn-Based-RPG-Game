/** Parses shared/data documents (untrusted JSON) into immutable domain definitions. */
import {
	type AchievementDef,
	type AffixDef,
	AutoBattleDef,
	Balance,
	type EnemyClassDef,
	GameData,
	type ItemDef,
	type MonsterAttack,
	MonsterDef,
	type PotionDef,
	type SpellDef,
	type StatusDef,
	type VocationDef,
} from "../domain/definitions";
import { attackFromJson } from "../domain/entities";
import { ELEMENTS, type Element, ENEMY_CLASSES, parseEnum, SLOTS, STATS, type Stat } from "../domain/enums";
import {
	field,
	type JsonObject,
	type JsonValue,
	jsonBool,
	jsonInt,
	jsonList,
	jsonObj,
	jsonStr,
} from "../domain/json-types";
import { EMBEDDED, type SharedFiles } from "./embedded-shared";

export class DataError extends Error {
	constructor(message: string) {
		super(message);
		this.name = "DataError";
	}
}

function objects(document: JsonObject, key: string): JsonObject[] {
	return jsonList(field(document, key)).map(jsonObj);
}

function strs(value: JsonValue): string[] {
	return jsonList(value).map(jsonStr);
}

function creature(raw: JsonObject, isBoss: boolean): MonsterDef {
	const gold = jsonObj(field(raw, "gold"));
	const resistances: Partial<Record<Element, number>> = {};
	for (const [element, value] of Object.entries(jsonObj(field(raw, "resistances")))) {
		resistances[parseEnum(ELEMENTS, element, "element")] = jsonInt(value);
	}
	const charge = raw.chargeAttack;
	return new MonsterDef(
		jsonStr(field(raw, "id")),
		jsonStr(field(raw, "name")),
		jsonInt(field(raw, "tier")),
		jsonStr(field(raw, "family")),
		jsonInt(field(raw, "hp")),
		jsonInt(field(raw, "xp")),
		jsonInt(field(gold, "min")),
		jsonInt(field(gold, "max")),
		jsonList(field(raw, "attacks")).map((attack): MonsterAttack => attackFromJson(attack)),
		resistances,
		isBoss,
		charge === undefined || charge === null ? null : jsonStr(charge),
	);
}

function spell(raw: JsonObject): SpellDef {
	const bonus = jsonObj(field(raw, "level3Bonus"));
	return {
		id: jsonStr(field(raw, "id")),
		name: jsonStr(field(raw, "name")),
		words: jsonStr(field(raw, "words")),
		kind: jsonStr(field(raw, "kind")) === "heal" ? "heal" : "attack",
		element: parseEnum(ELEMENTS, jsonStr(field(raw, "element")), "element"),
		mana: jsonInt(field(raw, "mana")),
		min: jsonInt(field(raw, "min")),
		max: jsonInt(field(raw, "max")),
		perLevel: jsonInt(field(raw, "perLevel")),
		perMagicLevel: jsonInt(field(raw, "perMagicLevel")),
		level3Bonus: {
			status: bonus.status === undefined ? null : jsonStr(bonus.status),
			chance: bonus.chance === undefined ? 0 : jsonInt(bonus.chance),
			cleanse: bonus.cleanse === undefined ? false : jsonBool(bonus.cleanse),
		},
	};
}

function vocation(raw: JsonObject): VocationDef {
	return {
		id: jsonStr(field(raw, "id")),
		name: jsonStr(field(raw, "name")),
		startHp: jsonInt(field(raw, "startHp")),
		startMp: jsonInt(field(raw, "startMp")),
		hpPerLevel: jsonInt(field(raw, "hpPerLevel")),
		mpPerLevel: jsonInt(field(raw, "mpPerLevel")),
		hpRegen: jsonInt(field(raw, "hpRegen")),
		mpRegen: jsonInt(field(raw, "mpRegen")),
		meleeMin: jsonInt(field(raw, "meleeMin")),
		meleeMax: jsonInt(field(raw, "meleeMax")),
		meleePerLevel: jsonInt(field(raw, "meleePerLevel")),
		weaponTypes: strs(field(raw, "weaponTypes")),
		shieldTypes: strs(field(raw, "shieldTypes")),
		starterWeapon: jsonStr(field(raw, "starterWeapon")),
		spells: strs(field(raw, "spells")),
	};
}

function weights(raw: JsonValue): Record<string, number> {
	return Object.fromEntries(Object.entries(jsonObj(raw)).map(([rarity, weight]) => [rarity, jsonInt(weight)]));
}

function enemyClass(classId: string, raw: JsonObject): EnemyClassDef {
	return {
		id: classId,
		statPct: jsonInt(field(raw, "statPct")),
		rewardPct: jsonInt(field(raw, "rewardPct")),
		dodge: jsonInt(field(raw, "dodge")),
		parry: jsonInt(field(raw, "parry")),
		crit: jsonInt(field(raw, "crit")),
		heal: jsonInt(field(raw, "heal")),
		dropChancePct: jsonInt(field(raw, "dropChancePct")),
		drops: jsonInt(field(raw, "drops")),
		potionDropPct: jsonInt(field(raw, "potionDropPct")),
		rarityWeights: weights(field(raw, "rarityWeights")),
	};
}

function autoBattle(raw: JsonObject): AutoBattleDef {
	const modes = jsonObj(field(raw, "modes"));
	return new AutoBattleDef(
		jsonInt(field(raw, "healBelowPct")),
		jsonInt(field(raw, "manaBelowPct")),
		jsonInt(field(raw, "emergencyHealBelowPct")),
		Object.entries(modes).map(([modeId, mode]) => ({
			id: modeId,
			offense: jsonStr(field(jsonObj(mode), "offense")),
			supportEvery: jsonInt(field(jsonObj(mode), "supportEvery")),
		})),
	);
}

function balance(raw: JsonObject): Balance {
	const caps = jsonObj(field(raw, "caps"));
	const magic = jsonObj(field(raw, "magicLevel"));
	const classes = jsonObj(field(raw, "enemyClasses"));
	const rarityWeights: Record<string, Record<string, number>> = {};
	for (const [table, tableWeights] of Object.entries(jsonObj(field(raw, "rarityWeights")))) {
		rarityWeights[table] = weights(tableWeights);
	}
	return new Balance({
		roundsPerTier: jsonInt(field(raw, "roundsPerTier")),
		cycleStatPct: jsonInt(field(raw, "cycleStatPct")),
		cycleRewardPct: jsonInt(field(raw, "cycleRewardPct")),
		positionPct: jsonInt(field(raw, "positionPct")),
		finalRound: jsonInt(field(raw, "finalRound")),
		eliteChancePct: jsonInt(field(raw, "eliteChancePct")),
		difficulties: objects(raw, "difficulties").map((d) => ({
			id: jsonStr(field(d, "id")),
			hpPct: jsonInt(field(d, "hpPct")),
			damagePct: jsonInt(field(d, "damagePct")),
			goldPct: jsonInt(field(d, "goldPct")),
			xpPct: jsonInt(field(d, "xpPct")),
		})),
		enemyClasses: ENEMY_CLASSES.map((id) => enemyClass(id, jsonObj(field(classes, id)))),
		critMultiplierPct: jsonInt(field(raw, "critMultiplierPct")),
		defendDamagePct: jsonInt(field(raw, "defendDamagePct")),
		parryReflectPct: jsonInt(field(raw, "parryReflectPct")),
		monsterHealPct: jsonInt(field(raw, "monsterHealPct")),
		bossTelegraphEvery: jsonInt(field(raw, "bossTelegraphEvery")),
		bossChargeDamagePct: jsonInt(field(raw, "bossChargeDamagePct")),
		caps: {
			critChance: jsonInt(field(caps, "critChance")),
			dodge: jsonInt(field(caps, "dodge")),
			parry: jsonInt(field(caps, "parry")),
			leech: jsonInt(field(caps, "leech")),
			protection: jsonInt(field(caps, "protection")),
		},
		magicLevelBase: jsonInt(field(magic, "base")),
		magicLevelGrowthPct: jsonInt(field(magic, "growthPct")),
		spellLevels: objects(raw, "spellLevels").map((s) => ({
			level: jsonInt(field(s, "level")),
			uses: jsonInt(field(s, "uses")),
			effectPct: jsonInt(field(s, "effectPct")),
			manaPct: jsonInt(field(s, "manaPct")),
		})),
		startingGold: jsonInt(field(raw, "startingGold")),
		startingPotions: objects(raw, "startingPotions").map(
			(p) => [jsonStr(field(p, "potionId")), jsonInt(field(p, "quantity"))] as const,
		),
		bagCapacity: jsonInt(field(raw, "bagCapacity")),
		itemLevelPerTier: jsonInt(field(raw, "itemLevelPerTier")),
		itemScoreWeights: new Map(
			Object.entries(jsonObj(field(raw, "itemScoreWeights"))).map(
				([stat, weight]) => [parseEnum(STATS, stat, "stat"), jsonInt(weight)] as const,
			),
		),
		rarities: objects(raw, "rarities").map((r) => ({
			id: jsonStr(field(r, "id")),
			statPct: jsonInt(field(r, "statPct")),
			valuePct: jsonInt(field(r, "valuePct")),
			affixMin: jsonInt(field(r, "affixMin")),
			affixMax: jsonInt(field(r, "affixMax")),
		})),
		rarityWeights,
		merchantStockSize: jsonInt(field(raw, "merchantStockSize")),
		merchantMarkupPct: jsonInt(field(raw, "merchantMarkupPct")),
		spellStatusDamagePct: jsonInt(field(raw, "spellStatusDamagePct")),
		autoBattle: autoBattle(jsonObj(field(raw, "autoBattle"))),
	});
}

function item(raw: JsonObject): ItemDef {
	const element = raw.element;
	return {
		id: jsonStr(field(raw, "id")),
		name: jsonStr(field(raw, "name")),
		slot: parseEnum(SLOTS, jsonStr(field(raw, "slot")), "slot"),
		type: jsonStr(field(raw, "type")),
		tier: jsonInt(field(raw, "tier")),
		element: element === undefined || element === null ? null : parseEnum(ELEMENTS, jsonStr(element), "element"),
		stats: Object.entries(jsonObj(field(raw, "stats"))).map(
			([stat, value]) => [parseEnum(STATS, stat, "stat"), jsonInt(value)] as readonly [Stat, number],
		),
		value: jsonInt(field(raw, "value")),
	};
}

function document(files: SharedFiles, name: string): JsonObject {
	const raw = files.data[name];
	if (raw === undefined) throw new DataError(`missing shared/data/${name}.json`);
	try {
		return jsonObj(raw);
	} catch (exc) {
		throw new DataError(`${name}.json: ${(exc as Error).message}`);
	}
}

function optional(files: SharedFiles, name: string): JsonObject {
	return files.data[name] === undefined ? { [name]: [] } : document(files, name);
}

export function loadGameData(files: SharedFiles = EMBEDDED): GameData {
	try {
		return new GameData({
			balance: balance(document(files, "balance")),
			vocations: objects(document(files, "vocations"), "vocations").map(vocation),
			spells: objects(document(files, "spells"), "spells").map(spell),
			monsters: objects(document(files, "monsters"), "monsters").map((m) => creature(m, false)),
			bosses: objects(document(files, "bosses"), "bosses").map((b) => creature(b, true)),
			potions: objects(document(files, "potions"), "potions").map(
				(p): PotionDef => ({
					id: jsonStr(field(p, "id")),
					name: jsonStr(field(p, "name")),
					resource: jsonStr(field(p, "resource")) === "mp" ? "mp" : "hp",
					min: jsonInt(field(p, "min")),
					max: jsonInt(field(p, "max")),
					price: jsonInt(field(p, "price")),
					unlockRound: jsonInt(field(p, "unlockRound")),
				}),
			),
			statuses: objects(document(files, "statuses"), "statuses").map(
				(s): StatusDef => ({
					id: jsonStr(field(s, "id")),
					kind: jsonStr(field(s, "kind")) === "stun" ? "stun" : "dot",
					element: parseEnum(ELEMENTS, jsonStr(field(s, "element")), "element"),
					turns: jsonInt(field(s, "turns")),
				}),
			),
			items: objects(document(files, "items"), "items").map(item),
			affixes: objects(optional(files, "affixes"), "affixes").map(
				(a): AffixDef => ({
					id: jsonStr(field(a, "id")),
					stat: parseEnum(STATS, jsonStr(field(a, "stat")), "stat"),
					min: jsonInt(field(a, "min")),
					max: jsonInt(field(a, "max")),
					perTier: jsonInt(field(a, "perTier")),
					slots: strs(field(a, "slots")).map((slot) => parseEnum(SLOTS, slot, "slot")),
				}),
			),
			achievements: objects(optional(files, "achievements"), "achievements").map(
				(a): AchievementDef => ({
					id: jsonStr(field(a, "id")),
					type: jsonStr(field(a, "type")),
					value: jsonInt(field(a, "value")),
				}),
			),
			families: strs(field(document(files, "families"), "families")),
		});
	} catch (exc) {
		if (exc instanceof DataError) throw exc;
		throw new DataError(`invalid game data: ${(exc as Error).message}`);
	}
}
