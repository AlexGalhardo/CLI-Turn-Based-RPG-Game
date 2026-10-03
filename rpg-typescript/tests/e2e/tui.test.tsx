/** End-to-end tests: the real Ink app driven by key presses (no animation, fixed seed, temp data dir). */
import { describe, expect, test } from "bun:test";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { render } from "ink-testing-library";
import { GreedyBot } from "../../src/application/bot";
import type { Command } from "../../src/application/commands";
import { canUse } from "../../src/application/loot";
import { availablePotions } from "../../src/application/merchant";
import { ArtLibrary } from "../../src/infrastructure/art";
import { SettingsRepository } from "../../src/infrastructure/repositories";
import { Controller, type View } from "../../src/presentation/controller";
import { listKey } from "../../src/presentation/render";
import { App } from "../../src/presentation/tui/app";
import { DATA, makeServices, tempDir } from "../helpers";

const ENTER = "\r";
const ESCAPE = "\u001B";
const ESCAPE_WAIT_MS = 100;
const delay = (ms: number): Promise<void> => new Promise((resolve) => setTimeout(resolve, ms));
const tick = (): Promise<void> => delay(0);

function mount(dir: string, lang: string | null = "en", animate = false) {
	const controller = new Controller(makeServices(dir), { seed: 42, localeOverride: lang });
	const instance = render(
		<App controller={controller} art={new ArtLibrary()} animate={animate} columns={100} rows={30} />,
	);
	const press = async (...keys: string[]): Promise<void> => {
		for (const key of keys) {
			instance.stdin.write(key);
			// A lone ESC may start an escape sequence, so Ink waits briefly before emitting it.
			await (key === ESCAPE ? delay(ESCAPE_WAIT_MS) : tick());
		}
	};
	const screen = (): string => instance.lastFrame() ?? "";
	const view = (): View => controller.view;
	return { controller, instance, press, screen, view };
}

function keysFor(command: Command, controller: Controller): string[] {
	const state = controller.session?.state;
	if (state === undefined) throw new Error("no session");
	switch (command.type) {
		case "attack":
			return ["1"];
		case "defend":
			return ["4"];
		case "cast":
			return ["2", listKey(DATA.vocation(state.player.vocationId).spells.indexOf(command.spellId))];
		case "potion": {
			const owned = DATA.potions.filter((p) => state.player.potionCount(p.id) > 0).map((p) => p.id);
			return ["3", listKey(owned.indexOf(command.potionId))];
		}
		case "next_fight":
			return ["0"];
		case "buy_potion":
			return [
				"1",
				listKey(availablePotions(state, DATA).indexOf(command.potionId)),
				...String(command.quantity),
				ENTER,
				"0",
			];
		case "sell_item":
			return ["2", listKey(state.player.bag.findIndex((item) => item.uid === command.uid)), "0"];
		case "equip": {
			const vocation = DATA.vocation(state.player.vocationId);
			const usable = state.player.bag
				.filter((item) => canUse(DATA.item(item.itemId), vocation))
				.map((i) => i.uid);
			return ["3", listKey(usable.indexOf(command.uid)), "1", "0"];
		}
		case "buy_stock_item":
			return ["4", listKey(command.index), "0"];
		case "end_run":
			return ["1"];
		default:
			throw new Error(`unexpected command ${command.type}`);
	}
}

describe("Ink TUI", () => {
	test("first launch: language, then a new run", async () => {
		const dir = tempDir();
		const { press, screen, view } = mount(dir, null);
		expect(view()).toBe("language");
		await press("2");
		expect(view()).toBe("title");
		expect(screen()).toContain("Nova jornada");
		await press("2", "3", "A", "n", "a", ENTER);
		expect(view()).toBe("vocation");
		await press("3");
		expect(view()).toBe("auto_equip");
		await press("2");
		expect(view()).toBe("merchant");
		expect(screen()).toContain("Ana");
		expect(screen()).toContain("Mago");
		expect(JSON.parse(readFileSync(join(dir, "settings.json"), "utf-8")).locale).toBe("pt-BR");
		expect(existsSync(join(dir, "save.json"))).toBe(true);
	});

	test("battle, merchant, save & quit and continue", async () => {
		const { controller, press, screen, view } = mount(tempDir());
		await press("2", "2", "B", "o", ENTER, "1", "2");
		expect(view()).toBe("merchant");
		await press("1", "1", "1", ENTER);
		expect(controller.session?.state.player.potionCount("health_potion")).toBe(6);
		await press("0", "5");
		expect(screen()).toContain("Equipment");
		await press("0", "0");
		expect(view()).toBe("battle");
		expect(screen()).toContain("HP");
		expect(screen()).toContain("Round 1");
		const monster = controller.session?.state.monster ?? null;
		if (monster === null) throw new Error("no monster");
		monster.hp = 1_000_000;
		monster.maxHp = 1_000_000;
		await press("1", "2");
		expect(view()).toBe("spells");
		await press(listKey(0), "3");
		expect(view()).toBe("potions");
		await press(ESCAPE);
		expect(view()).toBe("battle");
		await press("q");
		expect(view()).toBe("title");
		expect(screen()).toContain("Continue");
		await press("1");
		expect(view()).toBe("merchant");
		expect(controller.session?.info.sessions).toBe(2);
	});

	test("a whole run played by keys until game over", async () => {
		const dir = tempDir();
		const { controller, instance, press, screen, view } = mount(dir);
		const bot = new GreedyBot(DATA);
		await press("2", "3", "H", "e", "r", "o", ENTER, "1", "2");
		let jumped = false;
		for (let i = 0; i < 5000 && controller.session?.state.phase !== "game_over"; i++) {
			const session = controller.session;
			if (session === null) throw new Error("session lost");
			if (!jumped && session.state.phase === "merchant" && session.state.stats.kills.total() > 0) {
				// After the first kill, skip ahead so the run ends quickly (each fight costs many key presses).
				session.state.round = 95;
				jumped = true;
			}
			await press(...keysFor(bot.choose(session.state), controller));
		}
		expect(view()).toBe("game_over");
		expect(screen()).toContain(controller.session?.state.won === true ? "RUN COMPLETE" : "GAME OVER");
		await press("2", "3");
		expect(screen()).toContain("Hero");
		await press("0", "5");
		expect(screen()).toContain("[x] First Blood");
		await press("0", "4", "n", "p", "0");
		expect(view()).toBe("title");
		await press("0");
		expect(controller.exitRequested).toBe(true);
		instance.unmount();
		expect(existsSync(join(dir, "save.json"))).toBe(false);
		expect(readdirSync(join(dir, "history"))).toHaveLength(1);
	}, 120_000);

	test("a whole run with auto-battle (instant without animation)", async () => {
		const dir = tempDir();
		const { controller, press, screen, view } = mount(dir);
		await press("2", "1", "A", "u", "t", "o", ENTER, "2", "1");
		const session = controller.session;
		if (session === null) throw new Error("no session");
		expect(session.state.config.autoEquip).toBe(true);
		const modes = ["1", "2", "3"];
		for (let fight = 0; fight < 5000 && session.state.phase !== "game_over"; fight++) {
			if (session.state.phase === "victory") {
				expect(screen()).toContain("VICTORY");
				await press("1");
				continue;
			}
			await press("0", "5", modes[fight % modes.length] ?? "1");
			expect(controller.autoBattleActive).toBe(false);
		}
		expect(view()).toBe("game_over");
		expect(session.state.round).toBeGreaterThanOrEqual(1);
		expect(readdirSync(join(dir, "history"))).toHaveLength(1);
	}, 120_000);

	test("auto-battle is paced by a timer", async () => {
		const dir = tempDir();
		new SettingsRepository(dir).save({ locale: "en", autoEquip: false, battleSpeed: 2 });
		const { controller, instance, press, view } = mount(dir, null, true);
		await press("2", "1", "T", "i", "m", ENTER, "1", "2", "0");
		expect(view()).toBe("battle");
		const session = controller.session;
		const monster = session?.state.monster ?? null;
		if (session === null || monster === null) throw new Error("no fight");
		monster.hp = 1_000_000;
		monster.maxHp = 1_000_000;
		session.state.player.hp = 1_000_000;
		await press("5", "1");
		expect(controller.autoBattleActive).toBe(true);
		const turn = session.state.turn;
		await delay(controller.autoBattleIntervalMs() * 3);
		expect(session.state.turn).toBeGreaterThan(turn);
		await press("4");
		monster.hp = 1;
		for (let i = 0; i < 20 && controller.autoBattleActive; i++) await delay(controller.autoBattleIntervalMs());
		expect(controller.autoBattleActive).toBe(false);
		expect(view()).not.toBe("battle");
		instance.unmount();
	}, 30_000);

	test("small terminals get a resize message", () => {
		const controller = new Controller(makeServices(tempDir()), { localeOverride: "en" });
		const { lastFrame } = render(
			<App controller={controller} art={new ArtLibrary()} animate={false} columns={60} rows={20} />,
		);
		expect(lastFrame()).toContain("resize");
	});
});
