/** Auto-battle policy (§13) and auto-equip (§8.1). Port of tests/unit/test_auto_battle.py and test_auto_equip.py. */
import { describe, expect, test } from "bun:test";
import { AUTO_BATTLE_MODES, type AutoBattleMode, AutoBattlePolicy } from "../../src/application/auto-battle";
import { autoEquip } from "../../src/application/auto-equip";
import { Attack, BuyStockItem, Cast, type Command, Defend, NextFight, UsePotion } from "../../src/application/commands";
import { GameEngine } from "../../src/application/engine";
import { RunConfig } from "../../src/application/run-state";
import { buildSheet, itemScore, itemValue } from "../../src/domain/character";
import type { GameData } from "../../src/domain/definitions";
import type { ItemInstance } from "../../src/domain/entities";
import { calm, DATA, withEnemyClass, withTestItems } from "../helpers";

const OFFENSE_TURN = 1;

const item = (uid: number, itemId: string, rarity = "common", tier = 0): ItemInstance => ({
	uid,
	itemId,
	rarity,
	tier,
	affixes: [],
});

describe("auto-battle policy", () => {
	function battle(data: GameData, vocation = "warrior", roundNumber = 0): GameEngine {
		const [engine] = GameEngine.newRun(data, new RunConfig("Auto", vocation, "normal"), 11);
		engine.state.round = roundNumber;
		engine.step(NextFight());
		return engine;
	}

	function choose(engine: GameEngine, mode: AutoBattleMode, turn: number): Command {
		engine.state.turn = turn;
		return new AutoBattlePolicy(DATA, mode).choose(engine.state);
	}

	const supportTurn = (mode: AutoBattleMode): number => DATA.balance.autoBattle.mode(mode).supportEvery - 1;

	test("offensive actions per mode", () => {
		const engine = battle(DATA);
		engine.state.player.mp = 10_000;
		expect(choose(engine, "melee", OFFENSE_TURN + 1)).toEqual(Attack());
		expect(choose(engine, "spells", OFFENSE_TURN)).toEqual(Cast("annihilation"));
		expect(choose(engine, "balanced", 2)).toEqual(Cast("annihilation"));
		engine.state.player.mp = DATA.spell("brutal_strike").mana;
		expect(choose(engine, "spells", OFFENSE_TURN)).toEqual(Cast("brutal_strike"));
		engine.state.player.mp = 0;
		expect(choose(engine, "spells", OFFENSE_TURN)).toEqual(Attack());
	});

	test("support turn cadence per mode", () => {
		expect(AUTO_BATTLE_MODES.map(supportTurn)).toEqual([4, 4, 1]);
	});

	for (const mode of AUTO_BATTLE_MODES) {
		test(`${mode}: support turn heals below half HP`, () => {
			const engine = battle(DATA);
			const player = engine.state.player;
			player.hp = Math.floor((buildSheet(player, DATA).maxHp * 40) / 100);
			player.mp = 10_000;
			expect(choose(engine, mode, supportTurn(mode))).toEqual(Cast("wound_cleansing"));
			player.mp = 0;
			expect(choose(engine, mode, supportTurn(mode))).toEqual(UsePotion("health_potion"));
			player.potions.clear();
			expect(choose(engine, mode, supportTurn(mode))).toEqual(Attack());
		});
	}

	test("heal waits for the support turn above the emergency line", () => {
		const engine = battle(DATA);
		const player = engine.state.player;
		player.hp = Math.floor((buildSheet(player, DATA).maxHp * 40) / 100);
		expect(choose(engine, "melee", OFFENSE_TURN)).toEqual(Attack());
	});

	test("emergency heal on any turn", () => {
		const engine = battle(DATA);
		const player = engine.state.player;
		player.hp = Math.floor((buildSheet(player, DATA).maxHp * 20) / 100);
		player.mp = 10_000;
		expect(choose(engine, "melee", OFFENSE_TURN)).toEqual(Cast("wound_cleansing"));
	});

	test("best potion is the strongest owned", () => {
		const engine = battle(DATA);
		const player = engine.state.player;
		player.hp = 1;
		player.mp = 0;
		player.potions.set("strong_health_potion", 1);
		expect(choose(engine, "melee", OFFENSE_TURN)).toEqual(UsePotion("strong_health_potion"));
	});

	test("support turn drinks mana when low", () => {
		const engine = battle(DATA, "mage");
		engine.state.player.mp = 0;
		const turn = supportTurn("spells");
		expect(choose(engine, "spells", turn)).toEqual(UsePotion("mana_potion"));
		engine.state.player.potions.clear();
		expect(choose(engine, "spells", turn)).toEqual(Attack());
	});

	test("support turn defends against a telegraphed charge", () => {
		const engine = battle(DATA, "warrior", 9);
		const boss = engine.state.monster;
		if (boss === null) throw new Error("no boss");
		expect(boss.isBoss).toBe(true);
		boss.bossActions = DATA.balance.bossTelegraphEvery;
		const turn = supportTurn("melee");
		expect(choose(engine, "melee", turn)).toEqual(Defend());
		expect(choose(engine, "melee", turn + 1)).toEqual(Attack());
		boss.bossActions = 0;
		expect(choose(engine, "melee", turn)).toEqual(Attack());
		engine.state.monster = null;
		expect(choose(engine, "melee", turn)).toEqual(Attack());
	});

	for (const mode of AUTO_BATTLE_MODES) {
		test(`${mode}: the policy finishes fights with valid commands`, () => {
			const engine = battle(DATA, "archer");
			const policy = new AutoBattlePolicy(DATA, mode);
			for (let i = 0; i < 500 && engine.state.phase === "battle"; i++) {
				const events = engine.step(policy.choose(engine.state));
				expect(events.every((e) => e.type !== "error")).toBe(true);
			}
			expect(engine.state.phase).not.toBe("battle");
		});
	}

	test("the policy is deterministic", () => {
		const engine = battle(DATA, "mage");
		const rngState = engine.rngState;
		const all = (): Command[] =>
			AUTO_BATTLE_MODES.flatMap((mode) => Array.from({ length: 6 }, (_, turn) => choose(engine, mode, turn)));
		expect(all()).toEqual(all());
		expect(engine.rngState).toBe(rngState);
	});
});

describe("auto-equip", () => {
	const engineOf = (data: GameData, auto = true): GameEngine =>
		GameEngine.newRun(data, new RunConfig("Auto", "warrior", "normal", auto), 42)[0];

	test("a better item is equipped and the old one sold", () => {
		const data = withTestItems(DATA);
		const engine = engineOf(data);
		const state = engine.state;
		const starter = state.player.equipment.get("weapon");
		if (starter === undefined) throw new Error("no starter");
		const axe = item(50, "test_axe");
		state.player.bag.push(axe);
		const gold = state.player.gold;
		const rngState = engine.rngState;
		expect(autoEquip(state, data)).toEqual([
			{ type: "item_auto_equipped", uid: 50, itemId: "test_axe", slot: "weapon", score: itemScore(axe, data) },
			{ type: "item_auto_sold", uid: starter.uid, itemId: "sword", gold: itemValue(starter, data) },
		]);
		expect(state.player.equipment.get("weapon")).toBe(axe);
		expect(state.player.gold).toBe(gold + itemValue(starter, data));
		expect(state.player.bag).toEqual([]);
		expect(engine.rngState).toBe(rngState);
	});

	test("empty slots are filled without selling", () => {
		const data = withTestItems(DATA);
		const state = engineOf(data).state;
		state.player.bag.push(item(60, "test_helmet"));
		expect(autoEquip(state, data).map((e) => e.type)).toEqual(["item_auto_equipped"]);
		expect(state.player.equipment.get("helmet")?.uid).toBe(60);
	});

	test("ties go to the lowest uid and worse items stay", () => {
		const data = withTestItems(DATA);
		const state = engineOf(data).state;
		state.player.bag.push(item(72, "test_helmet"), item(71, "test_helmet"), item(73, "test_rod"));
		autoEquip(state, data);
		expect(state.player.equipment.get("helmet")?.uid).toBe(71);
		expect(state.player.bag.map((i) => i.uid)).toEqual([72, 73]);
		expect(autoEquip(state, data)).toEqual([]);
	});

	test("items above the player level are skipped", () => {
		const data = withTestItems(DATA);
		const state = engineOf(data).state;
		state.player.bag.push(item(80, "test_axe", "mythic", 9));
		expect(autoEquip(state, data)).toEqual([]);
		state.player.level = 1 + 9 * data.balance.itemLevelPerTier;
		expect(autoEquip(state, data)[0]?.uid).toBe(80);
	});

	test("a victory triggers auto-equip only when enabled", () => {
		let data = withEnemyClass(calm(withTestItems(DATA)), "normal", { dropChancePct: 100 });
		const weakSword = { ...data.item("sword"), stats: [] };
		data = data.with({ items: data.items.map((i) => (i.id === "sword" ? weakSword : i)) });
		for (const enabled of [true, false]) {
			const engine = engineOf(data, enabled);
			engine.step(NextFight());
			const monster = engine.state.monster;
			if (monster === null) throw new Error("no monster");
			monster.hp = 1;
			const events = engine.step(Attack());
			expect(engine.state.phase).toBe("merchant");
			expect(events.some((e) => e.type === "item_auto_equipped")).toBe(enabled);
			expect(engine.state.stats.itemsAutoEquipped).toBe(enabled ? 1 : 0);
		}
	});

	test("buying a stock item triggers auto-equip", () => {
		const data = withTestItems(DATA);
		const engine = engineOf(data);
		const state = engine.state;
		const axe = item(90, "test_axe");
		state.merchantStock = [axe];
		state.player.gold = 10_000;
		expect(engine.step(BuyStockItem(0)).map((e) => e.type)).toEqual([
			"item_bought",
			"item_auto_equipped",
			"item_auto_sold",
		]);
		expect(state.player.equipment.get("weapon")).toBe(axe);
		const manual = engineOf(data, false);
		manual.state.merchantStock = [axe];
		manual.state.player.gold = 10_000;
		expect(manual.step(BuyStockItem(0)).map((e) => e.type)).toEqual(["item_bought"]);
	});
});
