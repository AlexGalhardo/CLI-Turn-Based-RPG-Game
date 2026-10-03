/** M8 screens of the UI controller: settings, auto-equip step, auto-battle, victory and the equipment screen. */
import { describe, expect, test } from "bun:test";
import { itemScore } from "../../src/domain/character";
import type { GameData } from "../../src/domain/definitions";
import type { ItemInstance } from "../../src/domain/entities";
import { AUTO_BATTLE_BASE_MS, Controller, type View } from "../../src/presentation/controller";
import { formatDelta, STYLE_DIM, STYLE_GAIN, STYLE_LOSS, STYLE_WARNING } from "../../src/presentation/render";
import { calm, DATA, makeServices, tempDir, withTestItems } from "../helpers";

const viewOf = (controller: Controller): View => controller.view;

function makeController(data: GameData = DATA, dir = tempDir()): Controller {
	return new Controller(makeServices(dir, data), { seed: 7, localeOverride: "en" });
}

function startRun(controller: Controller, vocationKey = "1", autoEquipKey = "2"): void {
	controller.press("2");
	controller.press("2");
	for (const character of "Zed") controller.press(character);
	controller.press("enter");
	controller.press(vocationKey);
	controller.press(autoEquipKey);
}

const item = (
	uid: number,
	itemId: string,
	rarity: string,
	tier: number,
	affixes: ItemInstance["affixes"] = [],
): ItemInstance => ({ uid, itemId, rarity, tier, affixes });

function keyOf(controller: Controller, prefix: string): string {
	const found = controller.options().find((option) => option.label.startsWith(prefix));
	if (found === undefined) throw new Error(`no option ${prefix}`);
	return found.key;
}

describe("settings and new run", () => {
	test("settings toggle and persist", () => {
		const controller = makeController();
		controller.press("6");
		const labels = controller.options().map((o) => o.label);
		expect(labels[1]).toBe("Auto-equip on new runs: Off");
		expect(labels[2]).toBe("Auto-battle speed: 1x");
		controller.press("2");
		controller.press("3");
		expect(controller.settings).toEqual({ locale: null, autoEquip: true, battleSpeed: 2 });
		expect(controller.autoBattleIntervalMs()).toBe(AUTO_BATTLE_BASE_MS / 2);
		expect(controller.services.settings.load()).toEqual({ locale: null, autoEquip: true, battleSpeed: 2 });
		controller.press("3");
		expect(controller.settings.battleSpeed).toBe(1);
		controller.press("1");
		controller.press("1");
		expect(viewOf(controller)).toBe("settings");
		expect(controller.services.settings.load()).toEqual({ locale: "en", autoEquip: true, battleSpeed: 1 });
	});

	test("the new-run auto-equip step marks the default", () => {
		const dir = tempDir();
		makeController(DATA, dir).services.settings.save({ locale: "en", autoEquip: true, battleSpeed: 1 });
		const controller = makeController(DATA, dir);
		startRun(controller, "1", "0");
		expect(viewOf(controller)).toBe("vocation");
		controller.press("1");
		expect(viewOf(controller)).toBe("auto_equip");
		expect(controller.title()).toBe(controller.t("new_run.auto_equip"));
		const labels = controller.options().map((o) => o.label);
		expect(labels[0]?.endsWith("(default)")).toBe(true);
		expect(labels[1]?.endsWith("(default)")).toBe(false);
		controller.press("1");
		expect(viewOf(controller)).toBe("merchant");
		expect(controller.session?.state.config.autoEquip).toBe(true);
	});
});

describe("auto-battle", () => {
	function battleController(): Controller {
		const controller = makeController();
		startRun(controller);
		controller.press("0");
		expect(viewOf(controller)).toBe("battle");
		return controller;
	}

	test("auto-battle menu and instant run", () => {
		const controller = battleController();
		controller.press("5");
		expect(viewOf(controller)).toBe("auto_battle");
		expect(controller.title()).toBe(controller.t("auto_battle.title"));
		expect(controller.options().map((o) => o.key)).toEqual(["1", "2", "3", "0"]);
		controller.press("0");
		expect(viewOf(controller)).toBe("battle");
		controller.press("5");
		controller.press("3");
		expect(controller.autoBattleActive).toBe(true);
		expect(controller.log.at(-1)).toBe(
			controller.t("auto_battle.started", { mode: controller.t("auto_battle.balanced") }),
		);
		const session = controller.session;
		if (session === null) throw new Error("no session");
		const turn = session.state.turn;
		controller.press("1");
		expect(session.state.turn).toBe(turn);
		controller.runAutoBattle();
		expect(controller.autoBattleActive).toBe(false);
		expect(session.state.phase).not.toBe("battle");
		expect(["merchant", "game_over"]).toContain(viewOf(controller));
		expect(controller.autoBattleStep()).toBe(false);
	});

	test("auto-battle steps one turn at a time", () => {
		const controller = battleController();
		const session = controller.session;
		const monster = session?.state.monster ?? null;
		if (session === null || monster === null) throw new Error("no fight");
		monster.hp = 1_000_000;
		monster.maxHp = 1_000_000;
		controller.press("5");
		controller.press("1");
		const turn = session.state.turn;
		session.state.player.hp = 1_000_000;
		expect(controller.autoBattleStep()).toBe(true);
		expect(session.state.turn).toBe(turn + 1);
	});
});

describe("victory screen", () => {
	function victoryController(dir = tempDir()): Controller {
		const controller = makeController(calm(DATA), dir);
		startRun(controller);
		const session = controller.session;
		if (session === null) throw new Error("no session");
		session.state.round = DATA.balance.finalRound - 1;
		controller.press("0");
		for (let i = 0; i < 50; i++) {
			const monster = session.state.monster;
			if (monster === null) break;
			monster.hp = 1;
			controller.press("1");
		}
		expect(viewOf(controller)).toBe("victory");
		return controller;
	}

	test("end run", () => {
		const controller = victoryController();
		expect(controller.title()).toBe(controller.t("victory.title"));
		expect(controller.bodyLines()[0]).toContain("Ferumbras");
		controller.press("9");
		expect(viewOf(controller)).toBe("victory");
		controller.press("1");
		expect(viewOf(controller)).toBe("game_over");
		expect(controller.title()).toBe(controller.t("gameover.title_won"));
		expect(controller.bodyLines()[0]).toContain("won the run");
		controller.press("2");
		controller.press("3");
		expect(controller.bodyLines()[0]).toContain("WON");
	});

	test("continue", () => {
		const controller = victoryController();
		controller.press("2");
		expect(viewOf(controller)).toBe("merchant");
		expect(controller.session?.state.won).toBe(true);
	});

	test("continuing a saved victory returns to the victory screen", () => {
		const controller = victoryController();
		controller.session = null;
		controller.view = "title";
		controller.press("1");
		expect(viewOf(controller)).toBe("victory");
	});
});

describe("equipment screen", () => {
	function equipmentController(): Controller {
		const controller = makeController(withTestItems(DATA));
		startRun(controller);
		controller.press("3");
		expect(viewOf(controller)).toBe("equipment");
		return controller;
	}

	test("it lists every slot and the bag", () => {
		const controller = equipmentController();
		const data = controller.services.data;
		const player = controller.session?.state.player;
		if (player === undefined) throw new Error("no session");
		player.bag.push(item(900, "test_axe", "rare", 0), item(901, "test_rod", "common", 0));
		player.bag.push(item(902, "test_helmet", "legendary", 9));
		const lines = controller.bodyLines();
		const colors = controller.bodyColors();
		const starter = player.equipment.get("weapon");
		if (starter === undefined) throw new Error("no starter");
		expect(lines[0]).toBe(`EQUIPPED · total score ${itemScore(starter, data)}`);
		expect(lines[1]?.startsWith("Weapon: Sword [Common] · Lv 1")).toBe(true);
		expect(lines[2]).toBe("Shield: - empty -");
		expect(colors[2]).toBe(STYLE_WARNING);
		expect(lines.filter((line) => line.includes("- empty -"))).toHaveLength(7);
		expect(lines.at(-1)).toBe("BAG (usable)");
		const options = controller.options();
		expect(options.map((o) => o.key)).toEqual(["1", "2", "3", "0"]);
		const [axe, helmet, slot] = options;
		const delta = itemScore(item(900, "test_axe", "rare", 0), data) - itemScore(starter, data);
		expect(axe?.detail).toBe(formatDelta(delta));
		expect(axe?.detailColor).toBe(STYLE_GAIN);
		expect(axe?.color).toBe("rare");
		expect(helmet?.color).toBe(STYLE_DIM);
		expect(helmet?.label.endsWith("requires Lv 37")).toBe(true);
		expect(slot?.label).toBe("Weapon: Sword [Common]");
	});

	test("the comparison shows stat and score deltas", () => {
		const controller = equipmentController();
		const player = controller.session?.state.player;
		if (player === undefined) throw new Error("no session");
		player.equipment.set("helmet", item(800, "test_helmet", "common", 0, [{ stat: "dodge", value: 3 }]));
		player.bag.push(item(801, "test_helmet", "rare", 0, [{ stat: "critChance", value: 2 }]));
		controller.press("1");
		expect(viewOf(controller)).toBe("compare");
		expect(controller.title()).toBe("Helmet: Test Helmet → Test Helmet");
		const lines = controller.bodyLines();
		const bodyColors = controller.bodyColors();
		const colors = new Map(lines.map((line, i) => [line, bodyColors[i]]));
		expect(colors.get("Armor: 10 → 15 (+5)")).toBe(STYLE_GAIN);
		expect(colors.get("Max HP: 50 → 75 (+25)")).toBe(STYLE_GAIN);
		expect(colors.get("Critical chance: 0 → 2 (+2)")).toBe(STYLE_GAIN);
		expect(colors.get("Dodge: 3 → 0 (-3)")).toBe(STYLE_LOSS);
		expect(colors.get("Affixes gained: +2 Critical chance")).toBe(STYLE_GAIN);
		expect(colors.get("Affixes lost: +3 Dodge")).toBe(STYLE_LOSS);
		expect(lines.some((line) => line.startsWith("Score: "))).toBe(true);
		expect(controller.options().map((o) => o.key)).toEqual(["1", "0"]);
		controller.press("1");
		expect(viewOf(controller)).toBe("equipment");
		expect(player.equipment.get("helmet")?.uid).toBe(801);
	});

	test("the comparison warns about the required level; slots can be unequipped", () => {
		const controller = equipmentController();
		const session = controller.session;
		if (session === null) throw new Error("no session");
		session.state.player.bag.push(item(810, "test_axe", "common", 3));
		controller.press("1");
		expect(controller.bodyColors().at(-1)).toBe(STYLE_LOSS);
		expect(controller.bodyLines().at(-1)).toBe("Requires level 13 (you are level 1).");
		controller.press("1");
		expect(controller.message).toBe(controller.t("error.level_too_low"));
		expect(viewOf(controller)).toBe("equipment");
		controller.press("0");
		controller.press("3");
		controller.press(keyOf(controller, "Weapon"));
		expect(viewOf(controller)).toBe("equipped_slot");
		expect(controller.title()).toBe("Weapon");
		expect(controller.bodyLines()[1]).toBe("Attack: 6");
		controller.press("1");
		expect(viewOf(controller)).toBe("equipment");
		expect(session.state.player.equipment.has("weapon")).toBe(false);
		controller.press("0");
	});

	test("compare and slot views survive missing items", () => {
		const controller = equipmentController();
		const session = controller.session;
		if (session === null) throw new Error("no session");
		expect(controller.bodyLines().at(-1)).toBe(controller.t("equipment.bag_empty"));
		controller.view = "compare";
		expect(controller.bodyLines()).toEqual([]);
		expect(controller.title()).toBe(controller.t("merchant.equipment"));
		controller.view = "equipped_slot";
		controller.press("0");
		controller.view = "equipped_slot";
		session.state.player.equipment.clear();
		expect(controller.bodyLines()).toEqual([controller.t("equipment.empty")]);
	});

	test("monster view and hall of fame markers", () => {
		const eliteData = DATA.with({ balance: DATA.balance.with({ eliteChancePct: 100 }) });
		const controller = makeController(eliteData);
		const repository = controller.services.repositories.profile;
		const profile = repository.load();
		profile.hallOfFame.push({
			runId: "r",
			name: "Ana",
			vocation: "mage",
			difficulty: "hard",
			round: 100,
			level: 50,
			endedAt: "2026-01-01T00:00:00Z",
			won: true,
		});
		repository.save(profile);
		controller.press("3");
		expect(controller.bodyLines()[0]).toContain("WON");
		controller.press("0");
		startRun(controller);
		controller.press("0");
		expect(controller.monsterView()?.enemyClass).toBe("elite");
		expect(controller.log.some((line) => line.includes("ELITE"))).toBe(true);
	});
});
