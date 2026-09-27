import { describe, expect, test } from "bun:test";
import { BuyPotion, BuyStockItem, Equip, NextFight, SellItem, Unequip } from "../../src/application/commands";
import { canUse, generateItem, rarityWeights } from "../../src/application/loot";
import { availablePotions, stockPrice } from "../../src/application/merchant";
import { buildSheet, itemValue } from "../../src/domain/character";
import type { AffixDef, GameData } from "../../src/domain/definitions";
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

describe("loot", () => {
	test("generation is deterministic with unique affix stats", () => {
		const data = lootData(DATA);
		const vocation = data.vocation("warrior");
		const difficulty = data.balance.difficulty("normal");
		const make = () =>
			Array.from({ length: 20 }, (_, uid) =>
				generateItem(data, new Rng(5), { vocation, tier: 0, table: "boss", difficulty, uid }),
			);
		expect(make()).toEqual(make());
		const rng = new Rng(11);
		for (let uid = 0; uid < 200; uid++) {
			const item = generateItem(data, rng, { vocation, tier: 1, table: "boss", difficulty, uid });
			if (item === null) throw new Error("no item");
			expect(item.rarity).not.toBe("common");
			const stats = item.affixes.map((affix) => affix.stat);
			expect(new Set(stats).size).toBe(stats.length);
			expect(canUse(data.item(item.itemId), vocation)).toBe(true);
		}
	});

	test("no candidates consumes nothing; HARD boosts non-common weights", () => {
		const rng = new Rng(3);
		const difficulty = DATA.balance.difficulty("normal");
		const result = generateItem(DATA.with({ items: [] }), rng, {
			vocation: DATA.vocation("mage"),
			tier: 9,
			table: "monster",
			difficulty,
			uid: 1,
		});
		expect(result).toBeNull();
		expect(rng.state).toBe(3);
		const normal = rarityWeights(DATA, "monster", difficulty);
		const hard = rarityWeights(DATA, "monster", DATA.balance.difficulty("hard"));
		expect(hard[0]).toBe(normal[0] ?? -1);
		hard.slice(1).forEach((weight, i) => {
			expect(weight).toBeGreaterThanOrEqual(normal[i + 1] ?? 0);
		});
		expect(rarityWeights(DATA, "unknown_table", difficulty)).toEqual([0, 0, 0, 0]);
	});
});
