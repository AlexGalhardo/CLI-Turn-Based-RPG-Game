/** Replays every shared/golden scenario recorded by the Python reference: the TypeScript port must match exactly. */
import { describe, expect, test } from "bun:test";
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { GreedyBot } from "../../src/application/bot";
import { commandFromJson, commandToJson } from "../../src/application/commands";
import { GameEngine } from "../../src/application/engine";
import { RunConfig } from "../../src/application/run-state";
import { jsonInt, jsonList, jsonObj } from "../../src/domain/json-types";
import { Rng } from "../../src/domain/rng";
import { loadGameData } from "../../src/infrastructure/data-loader";
import { finalState, GOLDEN_DIR } from "../helpers";

const data = loadGameData();
const scenarioFiles = readdirSync(GOLDEN_DIR)
	.filter((file) => file.endsWith(".json") && file !== "prng.json")
	.sort();

describe("PRNG reference vectors", () => {
	const document = jsonObj(JSON.parse(readFileSync(join(GOLDEN_DIR, "prng.json"), "utf-8")));
	for (const raw of jsonList(document.vectors)) {
		const vector = jsonObj(raw);
		test(`seed ${vector.seed}`, () => {
			const rng = new Rng(jsonInt(vector.seed));
			const expected = jsonList(vector.outputs).map(jsonInt);
			expect(expected.map(() => rng.nextU32())).toEqual(expected);
		});
	}
});

describe("golden scenarios", () => {
	test("there are golden files to replay", () => {
		expect(scenarioFiles.length).toBeGreaterThanOrEqual(11);
	});

	for (const file of scenarioFiles) {
		test(file.replace(".json", ""), () => {
			const golden = jsonObj(JSON.parse(readFileSync(join(GOLDEN_DIR, file), "utf-8")));
			const [engine, first] = GameEngine.newRun(
				data,
				RunConfig.fromJson(golden.config ?? null),
				jsonInt(golden.seed),
			);
			const expectedEvents = jsonList(golden.events);
			expect(first).toEqual(expectedEvents[0] as never);
			jsonList(golden.commands).forEach((rawCommand, index) => {
				const events = engine.step(commandFromJson(rawCommand));
				if (JSON.stringify(events) !== JSON.stringify(expectedEvents[index + 1])) {
					expect({ command: index + 1, events }).toEqual({
						command: index + 1,
						events: expectedEvents[index + 1] as never,
					});
				}
			});
			expect(finalState(engine)).toEqual(golden.finalState as never);
		});
	}
});

describe("bot parity", () => {
	for (const file of scenarioFiles.filter((name) => name.startsWith("bot-full-run-"))) {
		test(`${file.replace(".json", "")} — the TypeScript bot chooses the recorded commands`, () => {
			const golden = jsonObj(JSON.parse(readFileSync(join(GOLDEN_DIR, file), "utf-8")));
			const [engine] = GameEngine.newRun(data, RunConfig.fromJson(golden.config ?? null), jsonInt(golden.seed));
			const bot = new GreedyBot(data);
			const commands = [];
			while (engine.state.phase !== "game_over") {
				const command = bot.choose(engine.state);
				commands.push(command);
				engine.step(command);
			}
			expect(commands.map(commandToJson)).toEqual(jsonList(golden.commands) as never);
		});
	}
});
