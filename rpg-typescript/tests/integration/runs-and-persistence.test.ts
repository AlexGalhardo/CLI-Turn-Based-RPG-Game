import { describe, expect, test } from "bun:test";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { GreedyBot } from "../../src/application/bot";
import { Attack, BuyPotion, NextFight } from "../../src/application/commands";
import { GameEngine, InvalidRunConfigError } from "../../src/application/engine";
import type { Event } from "../../src/application/events";
import { GameSession, type Repositories, type SessionContext } from "../../src/application/game-session";
import { ProfileService } from "../../src/application/profile";
import { RunConfig, RunState } from "../../src/application/run-state";
import { makeRunId, NewerSchemaError, parseTimestamp } from "../../src/application/save-game";
import { simulate, winRatePct } from "../../src/application/simulator";
import { STATS } from "../../src/domain/enums";
import { DataError, loadGameData } from "../../src/infrastructure/data-loader";
import { EMBEDDED } from "../../src/infrastructure/embedded-shared";
import { resolveDataDir } from "../../src/infrastructure/paths";
import {
	DEFAULT_SETTINGS,
	FileHistoryRepository,
	FileProfileRepository,
	FileSaveRepository,
	SettingsRepository,
	SystemClock,
} from "../../src/infrastructure/repositories";
import { DATA, tempDir } from "../helpers";

const MAX_STEPS = 50_000;

function playToDeath(engine: GameEngine, bot: GreedyBot): Event[][] {
	const log: Event[][] = [];
	for (let i = 0; i < MAX_STEPS; i++) {
		if (engine.state.phase === "game_over") return log;
		const events = engine.step(bot.choose(engine.state));
		expect(events.some((e) => e.type === "error")).toBe(false);
		log.push(events);
	}
	throw new Error("run did not finish");
}

class FakeClock {
	current = Date.UTC(2026, 8, 27, 12, 0, 0);
	now(): Date {
		this.current += 10_000;
		return new Date(this.current);
	}
}

function context(dir: string, clock = new FakeClock(), gameVersion = "1"): SessionContext {
	const repositories: Repositories = {
		saves: new FileSaveRepository(dir),
		history: new FileHistoryRepository(dir),
		profile: new FileProfileRepository(dir),
	};
	return { repositories, clock, gameVersion };
}

describe("full runs", () => {
	for (const vocation of ["warrior", "archer", "mage"]) {
		for (const difficulty of ["easy", "normal", "hard"]) {
			test(`${vocation}/${difficulty} bot plays until the run ends`, () => {
				const [engine] = GameEngine.newRun(DATA, new RunConfig("Bot", vocation, difficulty), 1234);
				const log = playToDeath(engine, new GreedyBot(DATA));
				const state = engine.state;
				expect(state.phase).toBe("game_over");
				expect(state.round).toBeGreaterThanOrEqual(1);
				expect(state.stats.damageDealt).toBeGreaterThan(0);
				if (state.won) {
					expect(state.round).toBe(DATA.balance.finalRound);
					expect(state.deathCause).toBeNull();
					expect(log.at(-2)?.at(-1)).toEqual({ type: "run_won", round: state.round });
					expect(log.at(-1)).toEqual([{ type: "run_ended", won: true }]);
					expect(state.stats.kills.total()).toBe(state.round);
				} else {
					expect(state.deathCause).toBeTruthy();
					expect(log.at(-1)?.at(-1)?.type).toBe("player_died");
					expect(state.stats.kills.total()).toBe(state.round - 1);
				}
			});
		}
	}

	test("some bot runs are won", () => {
		let won = 0;
		for (let seed = 2002; seed < 2006; seed++) {
			const [engine] = GameEngine.newRun(DATA, new RunConfig("Bot", "archer", "easy"), seed);
			playToDeath(engine, new GreedyBot(DATA));
			if (engine.state.won) won += 1;
		}
		expect(won).toBeGreaterThan(0);
	});

	test("restore mid-run continues identically", () => {
		const bot = new GreedyBot(DATA);
		const [reference] = GameEngine.newRun(DATA, new RunConfig("Bot", "mage", "hard"), 99);
		const referenceLog = playToDeath(reference, bot);
		let [engine] = GameEngine.newRun(DATA, new RunConfig("Bot", "mage", "hard"), 99);
		const log: Event[][] = [];
		while (engine.state.phase !== "game_over") {
			if (engine.state.phase === "merchant" && engine.state.round % 3 === 0) {
				engine = GameEngine.restore(DATA, engine.state.clone(), engine.rngState);
			}
			log.push(engine.step(bot.choose(engine.state)));
		}
		expect(log).toEqual(referenceLog);
	});

	test("invalid configs are rejected", () => {
		expect(() => GameEngine.newRun(DATA, new RunConfig("X", "knight", "normal"), 1)).toThrow(InvalidRunConfigError);
		expect(() => GameEngine.newRun(DATA, new RunConfig("X", "mage", "nightmare"), 1)).toThrow(
			InvalidRunConfigError,
		);
	});

	test("simulator summary", () => {
		const summary = simulate(DATA, "warrior", "normal", 3, 10);
		expect(summary.runs).toBe(3);
		expect(summary.wins).toBeGreaterThanOrEqual(0);
		expect(summary.wins).toBeLessThanOrEqual(3);
		expect(winRatePct(summary)).toBe(Math.floor((summary.wins * 100) / 3));
		expect(summary.minRound).toBeLessThanOrEqual(summary.medianRound);
		expect(() => simulate(DATA, "warrior", "normal", 0)).toThrow("positive");
	});

	test("simulator counts won runs", () => {
		const summary = simulate(DATA, "archer", "easy", 2, 2002);
		expect(summary.wins).toBeGreaterThanOrEqual(1);
		expect(summary.maxRound).toBe(DATA.balance.finalRound);
	});
});

describe("game data", () => {
	test("content requirements and cross references", () => {
		expect(DATA.monsters.length).toBeGreaterThanOrEqual(100);
		expect(DATA.tierCount).toBe(10);
		for (const creature of [...DATA.monsters, ...DATA.bosses]) {
			expect(DATA.families).toContain(creature.family);
			if (creature.isBoss) creature.attack(creature.chargeAttack ?? "");
		}
		for (const vocation of DATA.vocations) {
			DATA.item(vocation.starterWeapon);
			for (const spell of vocation.spells) DATA.spell(spell);
		}
	});

	test("invalid data raises DataError", () => {
		const vocations = structuredClone(EMBEDDED.data.vocations) as { vocations: Array<Record<string, unknown>> };
		delete vocations.vocations[0]?.startHp;
		expect(() => loadGameData({ ...EMBEDDED, data: { ...EMBEDDED.data, vocations } })).toThrow(DataError);
		expect(() => loadGameData({ ...EMBEDDED, data: { ...EMBEDDED.data, balance: [] } })).toThrow(DataError);
		const { vocations: _, ...rest } = EMBEDDED.data;
		expect(() => loadGameData({ ...EMBEDDED, data: rest })).toThrow("missing shared/data/vocations.json");
		const { affixes: __, achievements: ___, ...noOptional } = EMBEDDED.data;
		expect(loadGameData({ ...EMBEDDED, data: noOptional }).affixes).toEqual([]);
	});

	test("balance M8 tables", () => {
		const balance = DATA.balance;
		expect(balance.rarities.map((r) => r.id)).toEqual(["common", "rare", "legendary", "mythic"]);
		expect(balance.rarities.map((r) => r.statPct)).toEqual([100, 150, 200, 300]);
		expect(balance.spellLevels.map((level) => level.effectPct)).toEqual([100, 150, 200]);
		expect(Object.keys(balance.rarityWeights)).toEqual(["merchant"]);
		const rarityIds = new Set(balance.rarities.map((r) => r.id));
		for (const enemyClass of balance.enemyClasses) {
			for (const rarity of Object.keys(enemyClass.rarityWeights)) expect(rarityIds.has(rarity)).toBe(true);
		}
		expect(balance.enemyClass("elite").statPct).toBeGreaterThan(balance.enemyClass("normal").statPct);
		expect(new Set(balance.itemScoreWeights.keys())).toEqual(new Set(STATS));
		expect(DATA.bossOfTier(DATA.tierCount - 1).id).toBe("ferumbras");
		expect(balance.finalRound).toBe(balance.roundsPerTier * DATA.tierCount);
	});
});

describe("persistence", () => {
	test("new session autosaves at the merchant", () => {
		const dir = tempDir();
		const [session, events] = GameSession.start(
			DATA,
			new RunConfig("Alex", "archer", "normal"),
			5,
			context(dir, new FakeClock(), "9.9.9"),
		);
		expect(events[0]?.type).toBe("run_started");
		const save = JSON.parse(readFileSync(join(dir, "save.json"), "utf-8"));
		expect(save).toMatchObject({ schemaVersion: 2, implementation: "typescript", gameVersion: "9.9.9" });
		expect(save.run.config.autoEquip).toBe(false);
		expect(save.run.won).toBe(false);
		expect(save.session.runId).toBe(session.info.runId);
		session.step(BuyPotion("health_potion", 1));
		expect(JSON.parse(readFileSync(join(dir, "save.json"), "utf-8")).run.player.potions.health_potion).toBe(6);
	});

	test("quit mid-battle resumes from the last merchant", () => {
		const dir = tempDir();
		const clock = new FakeClock();
		const [session] = GameSession.start(DATA, new RunConfig("Alex", "warrior", "normal"), 5, context(dir, clock));
		session.step(NextFight());
		const monster = session.state.monster;
		if (monster === null) throw new Error("no monster");
		monster.hp = 1_000_000;
		monster.maxHp = 1_000_000;
		session.step(Attack());
		expect(session.state.phase).toBe("battle");
		session.saveAndQuit();
		const resumed = GameSession.resume(DATA, context(dir, clock));
		expect(resumed?.state.phase).toBe("merchant");
		expect(resumed?.state.round).toBe(0);
		expect(resumed?.info.sessions).toBe(2);
		expect(resumed?.info.playTimeSeconds).toBeGreaterThan(0);
		expect(GameSession.resume(DATA, context(tempDir()))).toBeNull();
	});

	test("death writes history and profile and deletes the save", () => {
		const dir = tempDir();
		const ctx = context(dir);
		const [session] = GameSession.start(DATA, new RunConfig("Bot", "mage", "hard"), 3, ctx);
		const bot = new GreedyBot(DATA);
		const unlocked: string[] = [];
		while (session.state.phase !== "game_over") {
			unlocked.push(...session.step(bot.choose(session.state)).achievements.map((a) => a.id));
		}
		expect(existsSync(join(dir, "save.json"))).toBe(false);
		const records = ctx.repositories.history.list();
		expect(records).toHaveLength(1);
		expect(parseTimestamp(records[0]?.endedAt ?? "").getTime()).toBeGreaterThan(
			parseTimestamp(records[0]?.startedAt ?? "").getTime(),
		);
		const profile = ctx.repositories.profile.load();
		expect(profile.hallOfFame[0]?.runId).toBe(records[0]?.runId ?? "");
		expect(unlocked).toContain("first_blood");
		session.saveAndQuit();
		expect(existsSync(join(dir, "save.json"))).toBe(false);
	});

	test("a save written by Python-format JSON loads (schema checks, settings, run ids)", () => {
		const dir = tempDir();
		writeFileSync(join(dir, "save.json"), JSON.stringify({ schemaVersion: 99 }));
		expect(() => new FileSaveRepository(dir).load()).toThrow(NewerSchemaError);
		writeFileSync(join(dir, "profile.json"), JSON.stringify({ schemaVersion: 99 }));
		expect(() => new FileProfileRepository(dir).load()).toThrow(NewerSchemaError);
		const settingsDir = tempDir();
		const settingsPath = join(settingsDir, "settings.json");
		const settings = new SettingsRepository(settingsDir);
		expect(settings.load()).toEqual(DEFAULT_SETTINGS);
		settings.save({ locale: "pt-BR", autoEquip: true, battleSpeed: 2 });
		expect(settings.load()).toEqual({ locale: "pt-BR", autoEquip: true, battleSpeed: 2 });
		expect(JSON.parse(readFileSync(settingsPath, "utf-8"))).toEqual({
			schemaVersion: 2,
			locale: "pt-BR",
			autoEquip: true,
			battleSpeed: 2,
		});
		settings.save(DEFAULT_SETTINGS);
		expect(settings.load()).toEqual(DEFAULT_SETTINGS);
		writeFileSync(settingsPath, JSON.stringify({ schemaVersion: 1, locale: "fr" }));
		expect(settings.load()).toEqual(DEFAULT_SETTINGS);
		writeFileSync(settingsPath, JSON.stringify({ schemaVersion: 1, locale: "en" }));
		expect(settings.load()).toEqual({ locale: "en", autoEquip: false, battleSpeed: 1 });
		writeFileSync(settingsPath, JSON.stringify({ schemaVersion: 2, autoEquip: true, battleSpeed: 7 }));
		expect(settings.load()).toEqual({ locale: null, autoEquip: true, battleSpeed: 1 });
		expect(makeRunId(new Date(Date.UTC(2026, 0, 2, 3, 4, 5)), 42)).toBe("20260102T030405Z-42");
		expect(() => parseTimestamp("yesterday")).toThrow();
		expect(new SystemClock().now()).toBeInstanceOf(Date);
		expect(new FileHistoryRepository(tempDir()).list()).toEqual([]);
	});

	test("hall of fame keeps the top 10 ordered", () => {
		const service = new ProfileService(DATA, new FileProfileRepository(tempDir()).load());
		for (let i = 0; i < 12; i++) {
			service.recordFinishedRun({
				runId: `run${i}`,
				name: "A",
				vocation: "mage",
				difficulty: "normal",
				round: i % 5,
				level: i,
				endedAt: `2026-01-${String(i + 1).padStart(2, "0")}T00:00:00Z`,
				won: false,
			});
		}
		const hall = service.profile.hallOfFame;
		expect(hall).toHaveLength(10);
		expect(hall.slice(0, 3).map((e) => e.round)).toEqual([4, 4, 3]);
		expect(hall[0]?.level ?? 0).toBeGreaterThan(hall[1]?.level ?? 0);
		service.recordFinishedRun({
			runId: "winner",
			name: "W",
			vocation: "mage",
			difficulty: "easy",
			round: 1,
			level: 1,
			endedAt: "2026-02-01T00:00:00Z",
			won: true,
		});
		expect(service.profile.hallOfFame[0]?.runId).toBe("winner");
		const repository = new FileProfileRepository(tempDir());
		repository.save(service.profile);
		expect(repository.load().toJson()).toEqual(service.profile.toJson());
		expect(service.revealed("rat")).toBe(false);
	});

	test("state clone is a deep copy and paths resolve", () => {
		const [engine] = GameEngine.newRun(DATA, new RunConfig("A", "warrior", "normal"), 1);
		const clone = engine.state.clone();
		clone.player.gold = 999;
		expect(engine.state.player.gold).not.toBe(999);
		expect(RunState.fromJson(clone.toJson()).player.gold).toBe(999);
		expect(resolveDataDir("custom")).toBe("custom");
		const previous = process.env.RPG_DATA_DIR;
		process.env.RPG_DATA_DIR = "from-env";
		expect(resolveDataDir()).toBe("from-env");
		delete process.env.RPG_DATA_DIR;
		expect(resolveDataDir().endsWith(".cli-turn-based-rpg")).toBe(true);
		if (previous !== undefined) process.env.RPG_DATA_DIR = previous;
	});
});
