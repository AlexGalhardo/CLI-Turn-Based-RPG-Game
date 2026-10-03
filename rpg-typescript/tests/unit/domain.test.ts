import { describe, expect, test } from "bun:test";
import { buildSheet, itemStats, itemValue } from "../../src/domain/character";
import { byId, UnknownIdError } from "../../src/domain/definitions";
import { Player } from "../../src/domain/entities";
import {
	armorMitigation,
	clamp,
	manaForMagicLevel,
	pct,
	roundInfo,
	scaleReward,
	scaleStat,
	scaling,
	spellLevelForUses,
	xpForLevel,
} from "../../src/domain/formulas";
import { jsonBool, jsonInt, jsonList, jsonObj, jsonStr } from "../../src/domain/json-types";
import { Rng } from "../../src/domain/rng";
import { DATA, withTestItems } from "../helpers";

describe("Rng", () => {
	test("seed is reduced modulo 2^32", () => {
		expect(new Rng(2 ** 32 + 42).nextU32()).toBe(new Rng(42).nextU32());
	});

	test("state round trip continues the sequence", () => {
		const rng = new Rng(7);
		rng.nextU32();
		const clone = new Rng(rng.state);
		expect([1, 2, 3].map(() => clone.nextU32())).toEqual([1, 2, 3].map(() => rng.nextU32()));
	});

	test("roll is inclusive and bounded", () => {
		const rng = new Rng(1);
		const values = new Set(Array.from({ length: 500 }, () => rng.roll(3, 5)));
		expect([...values].sort()).toEqual([3, 4, 5]);
		expect(() => rng.roll(5, 4)).toThrow("invalid range");
	});

	test.each([
		[0, false],
		[-5, false],
		[100, true],
		[150, true],
	])("chance(%p) is certain and consumes nothing", (percent, expected) => {
		const rng = new Rng(9);
		const state = rng.state;
		expect(rng.chance(percent)).toBe(expected);
		expect(rng.state).toBe(state);
	});

	test("chance consumes one roll", () => {
		const rng = new Rng(9);
		const reference = new Rng(9);
		expect(rng.chance(50)).toBe(reference.roll(1, 100) <= 50);
		expect(rng.state).toBe(reference.state);
	});

	test("weighted and pick", () => {
		const rng = new Rng(3);
		expect(new Set(Array.from({ length: 50 }, () => rng.weighted([0, 5, 0])))).toEqual(new Set([1]));
		expect(() => rng.weighted([0, 0])).toThrow("positive");
		expect(rng.pick(["a"])).toBe("a");
		expect(() => rng.pick([])).toThrow("empty");
	});
});

describe("formulas", () => {
	test("pct floors and rejects negatives", () => {
		expect(pct(99, 50)).toBe(49);
		expect(pct(0, 150)).toBe(0);
		expect(() => pct(-1, 10)).toThrow("non-negative");
		expect(clamp(5, 0, 3)).toBe(3);
		expect(clamp(-1, 0, 3)).toBe(0);
	});

	test.each([
		[1, 0],
		[2, 100],
		[5, 800],
		[10, 9300],
		[20, 98800],
		[100, 15694800],
	])("xpForLevel(%p) = %p", (level, xp) => {
		expect(xpForLevel(level)).toBe(xp);
	});

	test("magic level and spell levels", () => {
		const balance = DATA.balance;
		expect(manaForMagicLevel(1, balance)).toBe(balance.magicLevelBase);
		expect(manaForMagicLevel(2, balance)).toBe(
			balance.magicLevelBase + pct(balance.magicLevelBase, balance.magicLevelGrowthPct),
		);
		const levels = balance.spellLevels;
		expect([0, 19, 20, 49, 50, 5000].map((uses) => spellLevelForUses(uses, levels).level)).toEqual([
			1, 1, 2, 2, 3, 3,
		]);
		expect(() => spellLevelForUses(0, [])).toThrow();
		expect(armorMitigation(100, 100)).toBe(50);
		expect(armorMitigation(10, 3)).toBe(9);
	});

	test.each([
		[1, [0, 0, 0, false]],
		[10, [0, 0, 9, true]],
		[11, [1, 0, 0, false]],
		[100, [9, 0, 9, true]],
		[101, [0, 1, 0, false]],
		[250, [4, 2, 9, true]],
	])("roundInfo(%p)", (round, expected) => {
		const info = roundInfo(round, DATA.balance, DATA.tierCount);
		expect([info.tier, info.cycle, info.position, info.isBoss]).toEqual(expected);
	});

	test("scaling combines difficulty, cycle and position; bosses ignore position", () => {
		const balance = DATA.balance;
		const hard = balance.difficulty("hard");
		const factors = scaling(roundInfo(103, balance, DATA.tierCount), balance, hard);
		const cyclePct = 100 + balance.cycleStatPct;
		const positionPct = 100 + 2 * balance.positionPct;
		expect(factors.hpPctProduct).toBe(hard.hpPct * cyclePct * positionPct);
		expect(scaleStat(1000, factors.hpPctProduct)).toBe(Math.floor((1000 * factors.hpPctProduct) / 1_000_000));
		expect(scaleReward(100, factors.rewardXpPctProduct)).toBe(
			Math.floor((100 * hard.xpPct * (100 + balance.cycleRewardPct)) / 10_000),
		);
		const normal = balance.difficulty("normal");
		expect(scaling(roundInfo(10, balance, DATA.tierCount), balance, normal).hpPctProduct).toBe(
			normal.hpPct * 100 * 100,
		);
	});
});

describe("definitions and character", () => {
	test("lookups and errors", () => {
		expect(() => DATA.spell("avada_kedavra")).toThrow(UnknownIdError);
		expect(() => DATA.balance.difficulty("nightmare")).toThrow(UnknownIdError);
		expect(() => DATA.balance.rarity("epic")).toThrow(UnknownIdError);
		expect(() => DATA.balance.enemyClass("champion")).toThrow(UnknownIdError);
		expect(() => DATA.balance.autoBattle.mode("berserk")).toThrow(UnknownIdError);
		expect(() => DATA.creature("rat").attack("laser")).toThrow(UnknownIdError);
		expect(() => DATA.bossOfTier(99)).toThrow(UnknownIdError);
		expect(DATA.monstersInTier(99)).toEqual([]);
		expect([{ id: "b" }, { id: "a" }, { id: "B" }].sort(byId).map((x) => x.id)).toEqual(["B", "a", "b"]);
	});

	test("item stats apply rarity and affixes; caps apply", () => {
		const data = withTestItems(DATA);
		const helmet = {
			uid: 1,
			itemId: "test_helmet",
			rarity: "legendary",
			tier: 0,
			affixes: [{ stat: "maxHp", value: 7 }],
		} as const;
		expect(Object.fromEntries(itemStats(helmet, data))).toEqual({ armor: 20, maxHp: 107 });
		expect(itemValue(helmet, data)).toBe(600);
		const player = new Player("A", "warrior", 10, 10, 0);
		player.equipment.set("ring", { uid: 2, itemId: "test_ring", rarity: "common", tier: 0, affixes: [] });
		const sheet = buildSheet(player, data);
		expect(sheet.critChance).toBe(data.balance.caps.critChance);
		expect(sheet.dodge).toBe(data.balance.caps.dodge);
		expect(sheet.weaponElement).toBe("physical");
	});

	test("json readers reject wrong types", () => {
		expect(() => jsonObj([])).toThrow(TypeError);
		expect(() => jsonObj(null)).toThrow(TypeError);
		expect(() => jsonList({})).toThrow(TypeError);
		expect(() => jsonInt(true)).toThrow(TypeError);
		expect(() => jsonInt(1.5)).toThrow(TypeError);
		expect(() => jsonStr(1)).toThrow(TypeError);
		expect(() => jsonBool(0)).toThrow(TypeError);
	});
});
