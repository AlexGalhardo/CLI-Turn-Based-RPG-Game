import { describe, expect, test } from "bun:test";
import { NextFight } from "../../src/application/commands";
import type { Event } from "../../src/application/events";
import { MonsterInstance } from "../../src/domain/entities";
import { ArtLibrary, frameFor, parseArt } from "../../src/infrastructure/art";
import { Translator } from "../../src/infrastructure/i18n";
import { FileProfileRepository, SettingsRepository } from "../../src/infrastructure/repositories";
import { CliError, parseCli } from "../../src/presentation/cli";
import { Controller, PAGE_SIZE, type View } from "../../src/presentation/controller";
import { EventFormatter } from "../../src/presentation/event-text";
import { bar, hpColor, listIndex, listKey } from "../../src/presentation/render";
import { renderReport } from "../../src/presentation/simulator-report";
import { VERSION } from "../../src/version";
import { DATA, makeServices, newEngine, tempDir } from "../helpers";

const viewOf = (controller: Controller): View => controller.view;

function startRun(controller: Controller, name = "Zed", vocationKey = "1", autoEquipKey = "2"): void {
	controller.press("2");
	controller.press("2");
	for (const character of name) controller.press(character);
	controller.press("enter");
	controller.press(vocationKey);
	controller.press(autoEquipKey);
}

const rat = (): MonsterInstance => new MonsterInstance("rat", false, "normal", 10, 10, 1, 1, 1, []);

describe("event text", () => {
	const formatter = new EventFormatter(DATA, new Translator());
	test.each<[Event, string]>([
		[{ type: "player_attacked", damage: 12, crit: false, element: "fire" }, "You hit for 12 fire damage."],
		[
			{ type: "player_attacked", damage: 30, crit: true, element: "physical" },
			"CRITICAL! You hit for 30 physical damage.",
		],
		[
			{ type: "spell_cast", spellId: "flame_strike", damage: 9, crit: false, element: "fire", mana: 20 },
			"Flame Strike deals 9 fire damage.",
		],
		[{ type: "potion_used", potionId: "mana_potion", amount: 80, resource: "mp" }, "Mana Potion restores 80 MP."],
		[
			{ type: "status_applied", target: "player", status: "burn", turns: 3, perTurn: 2 },
			"You are burning (3 turns).",
		],
		[{ type: "monster_killed", monsterId: "dragon", isBoss: false }, "You defeated Dragon!"],
		[
			{ type: "round_started", round: 10, tier: 0, cycle: 0, monsterId: "munster", isBoss: true, hp: 5 },
			"Round 10: the boss Munster challenges you! (5 HP)",
		],
		[{ type: "item_sold", uid: 3, itemId: "sword", gold: 25 }, "You sold Sword for 25 gold."],
		[{ type: "error", code: "not_enough_mana" }, "Not enough mana."],
		[{ type: "error", code: "level_too_low" }, "Your level is too low for that item."],
		[
			{
				type: "round_started",
				round: 3,
				tier: 0,
				cycle: 0,
				monsterId: "rat",
				isBoss: false,
				enemyClass: "elite",
				hp: 90,
			},
			"Round 3: an ELITE Rat appears! (90 HP)",
		],
		[
			{ type: "monster_attacked", attackId: "bite", damage: 9, element: "physical", charged: false, crit: true },
			"CRITICAL! Rat hits you for 9 physical damage.",
		],
		[{ type: "monster_dodged" }, "Rat dodges your attack!"],
		[{ type: "monster_parried", reflected: 4 }, "Rat parries your attack: you take 4 damage!"],
		[{ type: "monster_healed", amount: 12 }, "Rat heals 12 HP."],
		[{ type: "attack_parried", attackId: "bite", reflected: 3 }, "You parry the attack and reflect 3 damage!"],
		[
			{ type: "item_auto_equipped", uid: 5, itemId: "sword", slot: "weapon", score: 60 },
			"Auto-equipped Sword (score 60).",
		],
		[{ type: "item_auto_sold", uid: 6, itemId: "bow", gold: 30 }, "Sold Bow for 30 gold (auto-sell)."],
		[{ type: "potion_dropped", potionId: "mana_potion" }, "Loot: Mana Potion!"],
		[{ type: "run_won", round: 100 }, "VICTORY! You defeated the final boss on round 100!"],
		[{ type: "run_ended", won: true }, "Your victory is recorded in the Hall of Fame."],
	])("%j", (evt, expected) => {
		const state = newEngine().state;
		state.monster = rat();
		expect(formatter.format(evt, state)).toBe(expected);
	});

	test("unknown ids, uid lookups and monster from state", () => {
		const engine = newEngine();
		expect(
			formatter.format(
				{ type: "spell_cast", spellId: "ghost", damage: 1, crit: false, element: "fire", mana: 1 },
				engine.state,
			),
		).toContain("ghost");
		engine.state.player.bag.push({ uid: 77, itemId: "bow", rarity: "rare", tier: 0, affixes: [] });
		expect(formatter.format({ type: "item_dropped", uid: 77, itemId: "bow", rarity: "rare" }, engine.state)).toBe(
			"Loot: Bow [Rare]!",
		);
		expect(formatter.format({ type: "item_equipped", uid: 999, slot: "ring" }, engine.state)).toContain("#999");
		engine.step(NextFight());
		const name = DATA.creature(engine.state.monster?.creatureId ?? "rat").name;
		expect(formatter.format({ type: "monster_stunned" }, engine.state).startsWith(name)).toBe(true);
	});

	test("translator", () => {
		expect(new Translator("pt-BR").t("event.gold_looted", { amount: 5 })).toBe("Você saqueou 5 de ouro.");
		const english = new Translator();
		expect(english.t("missing.key")).toBe("missing.key");
		expect(english.t("event.gold_looted")).toBe("You looted {amount} gold.");
		expect(english.has("menu.quit")).toBe(true);
		expect(() => new Translator("fr")).toThrow("unsupported");
	});
});

describe("controller", () => {
	test("name validation and editing", () => {
		const controller = new Controller(makeServices(tempDir()), { seed: 7, localeOverride: "en" });
		controller.press("2");
		controller.press("1");
		expect(viewOf(controller)).toBe("name");
		controller.press("enter");
		expect(controller.message).toBe(controller.t("new_run.name_invalid"));
		for (const c of "Abcdefghijklmnopqrstuvwxyz") controller.press(c);
		expect(controller.inputBuffer).toBe("Abcdefghijklmnop");
		controller.press("backspace");
		expect(controller.inputPrompt()).toBe("> Abcdefghijklmno_");
		controller.press("escape");
		expect(viewOf(controller)).toBe("difficulty");
		controller.press("0");
		expect(viewOf(controller)).toBe("title");
		expect(controller.options().map((o) => o.key)).not.toContain("1");
		controller.press("0");
		expect(controller.exitRequested).toBe(true);
	});

	test("first launch asks the language and saves it", () => {
		const dir = tempDir();
		const controller = new Controller(makeServices(dir));
		expect(viewOf(controller)).toBe("language");
		controller.press("2");
		expect(viewOf(controller)).toBe("title");
		expect(controller.options().some((o) => o.label === "Sair")).toBe(true);
		expect(new SettingsRepository(dir).load().locale).toBe("pt-BR");
		controller.press("6");
		expect(viewOf(controller)).toBe("settings");
		controller.press("1");
		expect(viewOf(controller)).toBe("language");
		controller.press("1");
		expect(viewOf(controller)).toBe("settings");
		expect(controller.locale).toBe("en");
		controller.press("0");
		expect(viewOf(controller)).toBe("title");
	});

	test("merchant menus", () => {
		const controller = new Controller(makeServices(tempDir()), { seed: 7, localeOverride: "en" });
		startRun(controller);
		const session = controller.session;
		if (session === null) throw new Error("no session");
		expect(controller.title()).toBe(controller.t("merchant.title_start"));
		const player = session.state.player;
		controller.press("2");
		expect(controller.bodyLines()).toEqual([controller.t("merchant.empty_bag")]);
		controller.press("0");
		player.bag.push({ uid: 900, itemId: "hand_axe", rarity: "rare", tier: 0, affixes: [] });
		player.bag.push({ uid: 901, itemId: "bow", rarity: "common", tier: 0, affixes: [] });
		controller.press("3");
		const labels = controller.options().map((o) => o.label);
		expect(labels.some((l) => l.includes("Hand Axe"))).toBe(true);
		expect(labels.some((l) => l.includes("Bow"))).toBe(false);
		controller.press("1");
		expect(viewOf(controller)).toBe("compare");
		controller.press("1");
		expect(viewOf(controller)).toBe("equipment");
		expect([900, 1]).toContain(player.equipment.get("weapon")?.uid ?? 0);
		controller.press("0");
		controller.press("2");
		controller.press(controller.options()[0]?.key ?? "0");
		controller.press("0");
		controller.press("4");
		expect(controller.options()).toHaveLength(session.state.merchantStock.length + 1);
		player.gold = 0;
		controller.press("1");
		expect(controller.message).toBe(controller.t("error.not_enough_gold"));
		controller.press("0");
		controller.press("1");
		controller.press("1");
		controller.press("x");
		controller.press("enter");
		expect(viewOf(controller)).toBe("buy_potions");
		controller.press("1");
		controller.press("escape");
		controller.press("0");
		controller.press("5");
		expect(controller.bodyLines().some((line) => line.includes("Equipment"))).toBe(true);
		controller.press("0");
		controller.press("0");
		expect(viewOf(controller)).toBe("battle");
		expect(controller.title()).toBe("Your turn");
		expect(controller.header()).toContain("Seed 7");
	});

	test("battle submenus and messages", () => {
		const controller = new Controller(makeServices(tempDir()), { seed: 7, localeOverride: "en" });
		startRun(controller, "Mia", "3");
		controller.press("0");
		expect(controller.monsterView()).not.toBeNull();
		expect(controller.playerView()).not.toBeNull();
		const session = controller.session;
		if (session === null) throw new Error("no session");
		session.state.player.potions.clear();
		controller.press("3");
		expect(controller.bodyLines()).toEqual([controller.t("battle.no_potions")]);
		controller.press("0");
		session.state.player.mp = 0;
		controller.press("2");
		controller.press(listKey(0));
		expect(controller.message).toBe(controller.t("error.not_enough_mana"));
		controller.press("escape");
		controller.press("4");
		controller.press("q");
		expect(viewOf(controller)).toBe("title");
		expect(controller.session).toBeNull();
		controller.press("1");
		expect(viewOf(controller)).toBe("merchant");
	});

	test("bestiary paging, hall of fame and achievements", () => {
		const dir = tempDir();
		const repository = new FileProfileRepository(dir);
		const profile = repository.load();
		profile.bestiary.set("rat", { kills: 9, firstKilledAt: "2026-01-01T00:00:00Z" });
		profile.bestiary.set("bat", { kills: 1, firstKilledAt: "2026-01-01T00:00:00Z" });
		repository.save(profile);
		const controller = new Controller(makeServices(dir), { localeOverride: "en" });
		controller.press("4");
		const lines = controller.bodyLines();
		expect(lines).toHaveLength(PAGE_SIZE + 2);
		expect(lines.some((line) => line.startsWith("Rat") && line.includes("weak"))).toBe(true);
		controller.press("n");
		expect(controller.bodyLines()).not.toEqual(lines);
		for (let i = 0; i < 50; i++) controller.press("n");
		expect(controller.bodyLines().at(-1)?.startsWith("Page 12/12")).toBe(true);
		controller.press("p");
		controller.press("0");
		controller.press("3");
		expect(controller.bodyLines()).toEqual([controller.t("hall.empty")]);
		controller.press("0");
		controller.press("5");
		expect(
			controller
				.bodyLines()
				.slice(0, PAGE_SIZE)
				.every((line) => line.startsWith("[ ]")),
		).toBe(true);
	});
});

describe("render, art and cli", () => {
	test("bars, colours and list keys", () => {
		expect(bar(0, 100, 10)).toBe("░".repeat(10));
		expect(bar(1, 100, 10)).toBe(`█${"░".repeat(9)}`);
		expect(bar(100, 100, 10)).toBe("█".repeat(10));
		expect(bar(5, 0, 4)).toBe("░".repeat(4));
		expect([hpColor(60, 100), hpColor(30, 100), hpColor(10, 100)]).toEqual(["green", "yellow", "red"]);
		expect([listKey(0), listKey(9)]).toEqual(["1", "a"]);
		expect(listIndex("a")).toBe(9);
		expect(listIndex("!")).toBeNull();
		expect(() => listKey(99)).toThrow();
	});

	test("art parsing and every creature has art", () => {
		const animations = parseArt("@idle\n a\n%%\n b\n@hurt\n x\n");
		expect(frameFor(animations, "idle", 3)).toEqual([" b"]);
		expect(frameFor(animations, "attack", 0)).toEqual([" a"]);
		expect(frameFor(new Map(), "idle", 0)).toEqual([]);
		expect(() => parseArt("oops")).toThrow("before");
		expect(() => parseArt("%%")).toThrow("separator");
		const library = new ArtLibrary();
		for (const creature of [...DATA.monsters, ...DATA.bosses]) {
			expect(frameFor(library.forCreature(creature), "idle", 0).length).toBeGreaterThan(0);
		}
		expect(library.loadFile("families", "missing").size).toBe(0);
	});

	test("cli flags", () => {
		expect(parseCli([])).toMatchObject({ kind: "run", options: { seed: null, noAnim: false } });
		expect(parseCli(["--seed", "42", "--lang", "pt-BR", "--no-anim", "--data-dir", "x"])).toMatchObject({
			options: { seed: 42, lang: "pt-BR", noAnim: true, dataDir: "x" },
		});
		expect(parseCli(["--version"])).toEqual({ kind: "exit", output: `rpg ${VERSION} (typescript)` });
		expect(parseCli(["--help"])).toMatchObject({ kind: "exit" });
		expect(() => parseCli(["--seed", "-1"])).toThrow(CliError);
		expect(() => parseCli(["--simulate", "0"])).toThrow(CliError);
		expect(() => parseCli(["--lang", "fr"])).toThrow(CliError);
		expect(() => parseCli(["--bogus"])).toThrow();
	});

	test("simulator report", () => {
		const report = renderReport(
			[
				{
					vocation: "mage",
					difficulty: "hard",
					runs: 1,
					wins: 0,
					minRound: 3,
					p10Round: 3,
					medianRound: 3,
					p90Round: 3,
					maxRound: 3,
					meanLevel: 2,
					topKillers: [["rat", 1]],
				},
			],
			DATA,
		);
		expect(report).toContain("median");
		expect(report).toContain("0%");
		expect(report).toContain("Rat (1)");
	});
});
