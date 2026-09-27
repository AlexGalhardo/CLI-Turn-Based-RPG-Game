import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { GameEngine } from "../src/application/engine";
import { RunConfig } from "../src/application/run-state";
import type { GameData, ItemDef } from "../src/domain/definitions";
import type { JsonObject } from "../src/domain/json-types";
import { loadGameData } from "../src/infrastructure/data-loader";

export const SHARED_DIR = join(import.meta.dir, "..", "..", "shared");
export const GOLDEN_DIR = join(SHARED_DIR, "golden");
export const DATA: GameData = loadGameData();

export function newEngine(vocation = "warrior", difficulty = "normal", seed = 42, data: GameData = DATA): GameEngine {
	return GameEngine.newRun(data, new RunConfig("Tester", vocation, difficulty), seed)[0];
}

export function tempDir(): string {
	return mkdtempSync(join(tmpdir(), "rpg-ts-"));
}

/** Same summary as rpg-python's `tools/golden.final_state`. */
export function finalState(engine: GameEngine): JsonObject {
	const state = engine.state;
	const player = state.player;
	return {
		phase: state.phase,
		round: state.round,
		turn: state.turn,
		level: player.level,
		xp: player.xp,
		magicLevel: player.magicLevel,
		hp: player.hp,
		mp: player.mp,
		gold: player.gold,
		rngState: engine.rngState,
		nextItemUid: state.nextItemUid,
		stats: state.stats.toJson(),
	};
}

/** Adds deterministic test items (mirrors rpg-python tests/conftest.with_test_items). */
export function withTestItems(data: GameData): GameData {
	const extra: ItemDef[] = [
		{
			id: "test_helmet",
			name: "Test Helmet",
			slot: "helmet",
			type: "helmet",
			tier: 0,
			element: null,
			stats: [
				["armor", 10],
				["maxHp", 50],
			],
			value: 100,
		},
		{
			id: "test_ring",
			name: "Test Ring",
			slot: "ring",
			type: "ring",
			tier: 0,
			element: null,
			stats: [
				["critChance", 80],
				["dodge", 90],
			],
			value: 100,
		},
		{
			id: "test_axe",
			name: "Test Axe",
			slot: "weapon",
			type: "axe",
			tier: 0,
			element: null,
			stats: [["attack", 20]],
			value: 100,
		},
		{
			id: "test_rod",
			name: "Test Rod",
			slot: "weapon",
			type: "rod",
			tier: 0,
			element: null,
			stats: [["attack", 1]],
			value: 100,
		},
	];
	return data.with({ items: [...data.items, ...extra] });
}
