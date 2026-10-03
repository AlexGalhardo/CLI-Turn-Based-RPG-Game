/** Victory phase through the session (save, resume, history, Hall of Fame) and schema 1 → 2 migrations. */
import { describe, expect, test } from "bun:test";
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { Attack, ContinueRun, EndRun, NextFight } from "../../src/application/commands";
import { GameSession, type SessionContext } from "../../src/application/game-session";
import { RunConfig } from "../../src/application/run-state";
import { type JsonObject, jsonObj } from "../../src/domain/json-types";
import {
	FileHistoryRepository,
	FileProfileRepository,
	FileSaveRepository,
} from "../../src/infrastructure/repositories";
import { DATA, tempDir } from "../helpers";

const MAX_SWINGS = 200;

class FakeClock {
	current = Date.UTC(2026, 8, 27, 12, 0, 0);
	now(): Date {
		this.current += 10_000;
		return new Date(this.current);
	}
}

function context(dir: string): SessionContext {
	return {
		repositories: {
			saves: new FileSaveRepository(dir),
			history: new FileHistoryRepository(dir),
			profile: new FileProfileRepository(dir),
		},
		clock: new FakeClock(),
		gameVersion: "1",
	};
}

const readJsonObject = (path: string): JsonObject => jsonObj(JSON.parse(readFileSync(path, "utf-8")));

function winFinalFight(dir: string): [GameSession, SessionContext] {
	const ctx = context(dir);
	const [session] = GameSession.start(DATA, new RunConfig("Vic", "warrior", "easy", true), 8, ctx);
	session.state.round = DATA.balance.finalRound - 1;
	session.step(NextFight());
	const monster = session.state.monster;
	if (monster === null) throw new Error("no monster");
	expect(monster.creatureId).toBe("ferumbras");
	expect(monster.enemyClass).toBe("boss");
	for (let i = 0; i < MAX_SWINGS && session.state.phase === "battle"; i++) {
		session.state.player.hp = 1_000_000;
		monster.hp = 1;
		session.step(Attack());
	}
	expect(session.state.phase).toBe("victory");
	return [session, ctx];
}

/** Turns a current save into what version 1 wrote: no M8 fields and the old `epic` rarity. */
function v1(document: JsonObject): JsonObject {
	const old = structuredClone(document);
	old.schemaVersion = 1;
	const run = jsonObj(old.run);
	delete jsonObj(run.config).autoEquip;
	delete run.won;
	const player = jsonObj(run.player);
	jsonObj(jsonObj(player.equipment).weapon).rarity = "epic";
	const stats = jsonObj(run.stats);
	for (const key of ["itemsAutoEquipped", "elitesKilled", "potionsDropped"]) delete stats[key];
	stats.itemsDropped = { epic: 2, legendary: 1 };
	stats.droppedItems = [{ itemId: "sword", rarity: "epic", round: 3 }];
	return old;
}

describe("victory through the session", () => {
	test("a victory is saved, resumed and ended as won", () => {
		const dir = tempDir();
		const [, ctx] = winFinalFight(dir);
		const save = readJsonObject(join(dir, "save.json"));
		expect(jsonObj(save.run).phase).toBe("victory");
		expect(jsonObj(save.run).won).toBe(true);

		const resumed = GameSession.resume(DATA, ctx);
		if (resumed === null) throw new Error("no save");
		expect(resumed.state.phase).toBe("victory");
		expect(resumed.step(EndRun()).events).toEqual([{ type: "run_ended", won: true }]);
		expect(ctx.repositories.profile.load().achievements.has("conqueror")).toBe(true);
		expect(existsSync(join(dir, "save.json"))).toBe(false);
		const record = ctx.repositories.history.list()[0];
		expect(record?.won).toBe(true);
		expect(record?.deathCause).toBe("");
		expect(ctx.repositories.profile.load().hallOfFame[0]?.won).toBe(true);
	});

	test("continuing after a victory keeps the run won", () => {
		const [session] = winFinalFight(tempDir());
		expect(session.step(ContinueRun()).events).toEqual([
			{ type: "merchant_entered", round: DATA.balance.finalRound },
		]);
		expect(session.state.phase).toBe("merchant");
		expect(session.state.won).toBe(true);
		session.step(NextFight());
		expect(session.state.round).toBe(DATA.balance.finalRound + 1);
	});
});

describe("schema 1 → 2 migrations", () => {
	test("a v1 save is migrated", () => {
		const dir = tempDir();
		GameSession.start(DATA, new RunConfig("Old", "warrior", "normal"), 4, context(dir));
		const path = join(dir, "save.json");
		writeFileSync(path, JSON.stringify(v1(readJsonObject(path))));
		const loaded = new FileSaveRepository(dir).load();
		if (loaded === null) throw new Error("no save");
		const run = loaded.run;
		expect(run.config.autoEquip).toBe(false);
		expect(run.won).toBe(false);
		expect(run.player.equipment.get("weapon")?.rarity).toBe("legendary");
		expect(run.stats.itemsDropped.toJson()).toEqual({ legendary: 3 });
		expect(run.stats.droppedItems[0]?.rarity).toBe("legendary");
		expect(run.stats.elitesKilled).toBe(0);
	});

	test("a v1 monster gets its class from isBoss", () => {
		const dir = tempDir();
		const [session] = GameSession.start(DATA, new RunConfig("Old", "mage", "normal"), 4, context(dir));
		session.state.round = 9;
		session.step(NextFight());
		const path = join(dir, "save.json");
		const document = readJsonObject(path);
		jsonObj(document.run).monster = session.state.toJson().monster ?? null;
		const old = v1(document);
		delete jsonObj(jsonObj(old.run).monster).enemyClass;
		writeFileSync(path, JSON.stringify(old));
		expect(new FileSaveRepository(dir).load()?.run.monster?.enemyClass).toBe("boss");
		jsonObj(jsonObj(old.run).monster).isBoss = false;
		writeFileSync(path, JSON.stringify(old));
		expect(new FileSaveRepository(dir).load()?.run.monster?.enemyClass).toBe("normal");
	});

	test("v1 history and profile are migrated", () => {
		const root = tempDir();
		const wonDir = join(root, "won");
		const [session] = winFinalFight(wonDir);
		session.step(EndRun());
		const historyFile = readdirSync(join(wonDir, "history")).find((file) => file.endsWith(".json")) ?? "";
		const record = readJsonObject(join(wonDir, "history", historyFile));
		record.schemaVersion = 1;
		delete record.won;
		const stats = jsonObj(record.stats);
		for (const key of ["itemsAutoEquipped", "elitesKilled", "potionsDropped"]) delete stats[key];
		const oldDir = join(root, "old");
		mkdirSync(join(oldDir, "history"), { recursive: true });
		writeFileSync(join(oldDir, "history", historyFile), JSON.stringify(record));
		expect(new FileHistoryRepository(oldDir).list()[0]?.won).toBe(false);

		const profile = readJsonObject(join(wonDir, "profile.json"));
		profile.schemaVersion = 1;
		const hall = profile.hallOfFame;
		if (Array.isArray(hall)) {
			for (const entry of hall) delete jsonObj(entry).won;
		}
		writeFileSync(join(oldDir, "profile.json"), JSON.stringify(profile));
		expect(new FileProfileRepository(oldDir).load().hallOfFame[0]?.won).toBe(false);
	});
});
