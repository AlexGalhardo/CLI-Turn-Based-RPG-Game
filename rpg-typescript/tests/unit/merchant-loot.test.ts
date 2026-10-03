import { describe, expect, test } from "bun:test";
import { BuyPotion, BuyStockItem, Equip, NextFight, SellItem, Unequip } from "../../src/application/commands";
import { canUse, generateItem, rollRarity } from "../../src/application/loot";
import { availablePotions, stockPrice } from "../../src/application/merchant";
import { buildSheet, itemScore, itemStats, itemValue, requiredLevel } from "../../src/domain/character";
import type { AffixDef, GameData } from "../../src/domain/definitions";
import type { ItemInstance } from "../../src/domain/entities";
import { Rng } from "../../src/domain/rng";
import { DATA, newEngine, withTestItems } from "../helpers";

function lootData(data: GameData): GameData {
	const affixes: AffixDef[] = [
		{ id: "of_power", stat: "attack", min: 1, max: 3, perTier: 2, slots: ["weapon"] },
		{ id: "of_the_bear", stat: "maxHp", min: 5, max: 10, perTier: 5, slots: ["weapon", "helmet"] },
		{ id: "of_speed", stat: "dodge", min: 1, max: 2, perTier: 0, slots: ["weapon", "ring"] },
		{ id: "of_speed_2", stat: "dodge", min: 1, max: 2, perTier: 0, slots: ["weapon"] },
	];
	return withTestItems(data).with({ affixes });
}

describe("merchant", () => {
	test("potion purchase rules", () => {
		const engine = newEngine();
		const gold = engine.state.player.gold;
		expect(engine.step(BuyPotion("health_potion", 2))).toEqual([
			{ type: "potion_bought", potionId: "health_potion", quantity: 2, gold: 100 },
		]);
		expect(engine.state.player.gold).toBe(gold - 100);
		expect(engine.step(BuyPotion("health_potion", 1))).toEqual([{ type: "error", code: "not_enough_gold" }]);
		expect(engine.step(BuyPotion("health_potion", 0))).toEqual([{ type: "error", code: "invalid_quantity" }]);
		expect(engine.step(BuyPotion("great_health_potion", 1))).toEqual([{ type: "error", code: "potion_locked" }]);
		expect(engine.step(BuyPotion("elixir", 1))).toEqual([{ type: "error", code: "unknown_potion" }]);
		expect(engine.state.stats.potionsBought.get("health_potion")).toBe(2);
		expect(availablePotions(engine.state, DATA)).toEqual(["health_potion", "mana_potion"]);
		engine.state.round = 80;
		expect(availablePotions(engine.state, DATA)).toHaveLength(DATA.potions.length);
	});

	test("equip, swap, sell and unequip", () => {
		const data = withTestItems(DATA);
		const engine = newEngine("warrior", "normal", 42, data);
		const player = engine.state.player;
		const rod = { uid: 51, itemId: "test_rod", rarity: "rare", tier: 0, affixes: [] };
		player.bag.push({ uid: 50, itemId: "test_axe", rarity: "common", tier: 0, affixes: [] }, rod);
		const starterUid = player.equipment.get("weapon")?.uid;
		expect(engine.step(Equip(51))).toEqual([{ type: "error", code: "cannot_equip" }]);
		expect(engine.step(Equip(50))).toEqual([
			{ type: "item_unequipped", uid: starterUid ?? 0, itemId: "sword", slot: "weapon" },
			{ type: "item_equipped", uid: 50, itemId: "test_axe", slot: "weapon" },
		]);
		expect(buildSheet(player, data).meleeMin).toBe(8 + 20);
		expect(engine.step(SellItem(51))).toEqual([
			{ type: "item_sold", uid: 51, itemId: "test_rod", gold: itemValue(rod, data) },
		]);
		expect(engine.step(SellItem(51))).toEqual([{ type: "error", code: "invalid_item" }]);
		expect(engine.step(Unequip("weapon"))).toEqual([
			{ type: "item_unequipped", uid: 50, itemId: "test_axe", slot: "weapon" },
		]);
		expect(engine.step(Unequip("weapon"))).toEqual([{ type: "error", code: "invalid_item" }]);
		expect(engine.step(Equip(999))).toEqual([{ type: "error", code: "invalid_item" }]);
	});

	test("unequip with a full bag, and HP clamps", () => {
		const data = withTestItems(DATA);
		const engine = newEngine("warrior", "normal", 42, data);
		const player = engine.state.player;
		player.equipment.set("helmet", { uid: 70, itemId: "test_helmet", rarity: "common", tier: 0, affixes: [] });
		player.hp = buildSheet(player, data).maxHp;
		for (let i = 0; i < data.balance.bagCapacity; i++) {
			player.bag.push({ uid: 100 + i, itemId: "test_ring", rarity: "common", tier: 0, affixes: [] });
		}
		expect(engine.step(Unequip("helmet"))).toEqual([{ type: "error", code: "bag_full" }]);
		player.bag.pop();
		engine.step(Unequip("helmet"));
		expect(player.hp).toBe(buildSheet(player, data).maxHp);
	});

	test("stock purchase", () => {
		const data = lootData(DATA);
		const engine = newEngine("warrior", "normal", 42, data);
		const state = engine.state;
		expect(state.merchantStock).toHaveLength(data.balance.merchantStockSize);
		const item = state.merchantStock[0];
		if (item === undefined) throw new Error("empty stock");
		const price = stockPrice(item, data);
		state.player.gold = price;
		expect(engine.step(BuyStockItem(0))).toEqual([
			{ type: "item_bought", uid: item.uid, itemId: item.itemId, gold: price },
		]);
		expect(engine.step(BuyStockItem(0))).toEqual([{ type: "error", code: "not_enough_gold" }]);
		expect(engine.step(BuyStockItem(9))).toEqual([{ type: "error", code: "invalid_item" }]);
		for (let i = 0; i < 30; i++)
			state.player.bag.push({ uid: 200 + i, itemId: "test_ring", rarity: "common", tier: 0, affixes: [] });
		expect(engine.step(BuyStockItem(0))).toEqual([{ type: "error", code: "bag_full" }]);
		engine.step(NextFight());
		expect(state.merchantStock).toEqual([]);
	});
});

const instance = (
	uid: number,
	itemId: string,
	rarity: string,
	tier: number,
	affixes: ItemInstance["affixes"] = [],
) => ({
	uid,
	itemId,
	rarity,
	tier,
	affixes,
});

describe("loot", () => {
	test("generation is deterministic with unique affix stats", () => {
		const data = lootData(DATA);
		const vocation = data.vocation("warrior");
		const weights = data.balance.enemyClass("boss").rarityWeights;
		const make = () =>
			Array.from({ length: 20 }, (_, uid) => generateItem(data, new Rng(5), { vocation, tier: 0, weights, uid }));
		expect(make()).toEqual(make());
		const rng = new Rng(11);
		for (let uid = 0; uid < 200; uid++) {
			const item = generateItem(data, rng, { vocation, tier: 1, weights, uid });
			if (item === null) throw new Error("no item");
			expect(["legendary", "mythic"]).toContain(item.rarity);
			const stats = item.affixes.map((affix) => affix.stat);
			expect(new Set(stats).size).toBe(stats.length);
			expect(canUse(data.item(item.itemId), vocation)).toBe(true);
		}
	});

	test("no candidates consumes nothing", () => {
		const rng = new Rng(3);
		const weights = DATA.balance.enemyClass("normal").rarityWeights;
		const result = generateItem(DATA.with({ items: [] }), rng, {
			vocation: DATA.vocation("mage"),
			tier: 9,
			weights,
			uid: 1,
		});
		expect(result).toBeNull();
		expect(rng.state).toBe(3);
	});

	test("roll rarity skips zero weights and single options", () => {
		const rng = new Rng(1);
		expect(rollRarity(DATA, rng, { rare: 5 }).id).toBe("rare");
		expect(rollRarity(DATA, rng, { common: 0, mythic: 3 }).id).toBe("mythic");
		expect(rng.state).toBe(1);
		const rolled = new Set(Array.from({ length: 40 }, () => rollRarity(DATA, rng, { common: 1, legendary: 1 }).id));
		expect(rolled).toEqual(new Set(["common", "legendary"]));
		expect(rng.state).not.toBe(1);
		expect(() => rollRarity(DATA, rng, { common: 0 })).toThrow("positive");
	});

	for (const [rarity, attack, affixes] of [
		["common", 20, 0],
		["rare", 30, 1],
		["legendary", 40, 2],
		["mythic", 60, 2],
	] as const) {
		test(`${rarity} scales base stats and affix counts`, () => {
			const data = withTestItems(DATA);
			expect(Object.fromEntries(itemStats(instance(1, "test_axe", rarity, 0), data))).toEqual({ attack });
			const definition = data.balance.rarity(rarity);
			expect([definition.affixMin, definition.affixMax]).toEqual([affixes, affixes]);
		});
	}

	test("item stats apply rarity and affixes", () => {
		const data = withTestItems(DATA);
		const item = instance(1, "test_helmet", "legendary", 0, [{ stat: "maxHp", value: 7 }]);
		expect(Object.fromEntries(itemStats(item, data))).toEqual({ armor: 20, maxHp: 107 });
		expect(itemValue(item, data)).toBe(600);
	});

	test("item score weights final stats", () => {
		const data = withTestItems(DATA);
		const weights = data.balance.itemScoreWeights;
		const weight = (stat: Parameters<typeof weights.get>[0]): number => weights.get(stat) ?? 0;
		const common = instance(1, "test_helmet", "common", 0);
		expect(itemScore(common, data)).toBe(10 * weight("armor") + 50 * weight("maxHp"));
		const scores = ["common", "rare", "legendary"].map((r) => itemScore(instance(1, "test_helmet", r, 0), data));
		expect(scores).toEqual([...scores].sort((a, b) => a - b));
		expect(new Set(scores).size).toBe(3);
		const withAffix = instance(1, "test_helmet", "common", 0, [{ stat: "dodge", value: 2 }]);
		expect(itemScore(withAffix, data)).toBe(itemScore(common, data) + 2 * weight("dodge"));
	});

	test("required level grows with the item tier", () => {
		const perTier = DATA.balance.itemLevelPerTier;
		expect(requiredLevel(instance(1, "sword", "common", 0), DATA)).toBe(1);
		expect(requiredLevel(instance(1, "sword", "common", 3), DATA)).toBe(1 + 3 * perTier);
	});

	test("equip rejects items above the player level", () => {
		const data = withTestItems(DATA);
		const engine = newEngine("warrior", "normal", 42, data);
		const player = engine.state.player;
		const axe = instance(60, "test_axe", "common", 5);
		player.bag.push(axe);
		const rngState = engine.rngState;
		expect(engine.step(Equip(60))).toEqual([{ type: "error", code: "level_too_low" }]);
		expect(player.bag).toContain(axe);
		expect(engine.rngState).toBe(rngState);
		player.level = requiredLevel(axe, data);
		expect(engine.step(Equip(60)).at(-1)).toEqual({
			type: "item_equipped",
			uid: 60,
			itemId: "test_axe",
			slot: "weapon",
		});
	});

	test("selling an equipped uid is rejected", () => {
		const engine = newEngine();
		const player = engine.state.player;
		const weapon = player.equipment.get("weapon");
		if (weapon === undefined) throw new Error("no weapon");
		const gold = player.gold;
		expect(engine.step(SellItem(weapon.uid))).toEqual([{ type: "error", code: "invalid_item" }]);
		expect(player.equipment.get("weapon")).toBe(weapon);
		expect(player.gold).toBe(gold);
	});
});
