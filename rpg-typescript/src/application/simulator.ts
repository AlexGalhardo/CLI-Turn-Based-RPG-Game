/** Headless balance simulator: the bot plays many runs and we aggregate how far it gets. */
import type { GameData } from "../domain/definitions";
import { GreedyBot } from "./bot";
import { GameEngine } from "./engine";
import { RunConfig } from "./run-state";
import { Counter } from "./statistics";

const MAX_STEPS_PER_RUN = 200_000;

export interface RunResult {
	readonly round: number;
	readonly level: number;
	readonly deathCause: string;
}

export interface SimulationSummary {
	readonly vocation: string;
	readonly difficulty: string;
	readonly runs: number;
	readonly minRound: number;
	readonly p10Round: number;
	readonly medianRound: number;
	readonly p90Round: number;
	readonly maxRound: number;
	readonly meanLevel: number;
	readonly topKillers: ReadonlyArray<readonly [string, number]>;
}

export function playOne(data: GameData, config: RunConfig, seed: number): RunResult {
	const [engine] = GameEngine.newRun(data, config, seed);
	const bot = new GreedyBot(data);
	for (let step = 0; step < MAX_STEPS_PER_RUN; step++) {
		if (engine.state.phase === "game_over") {
			const state = engine.state;
			return { round: state.round, level: state.player.level, deathCause: state.deathCause ?? "" };
		}
		engine.step(bot.choose(engine.state));
	}
	throw new Error(`run did not finish (seed ${seed})`);
}

function percentile(sorted: readonly number[], percent: number): number {
	const index = Math.min(sorted.length - 1, Math.floor((sorted.length * percent) / 100));
	return sorted[index] ?? 0;
}

export function simulate(
	data: GameData,
	vocation: string,
	difficulty: string,
	runs: number,
	baseSeed = 1,
): SimulationSummary {
	if (runs <= 0) throw new RangeError("runs must be positive");
	const results = Array.from({ length: runs }, (_, i) =>
		playOne(data, new RunConfig("Bot", vocation, difficulty), baseSeed + i),
	);
	const rounds = results.map((r) => r.round).sort((a, b) => a - b);
	const killers = new Counter();
	for (const result of results) killers.add(result.deathCause);
	const topKillers = killers
		.entries()
		.sort((a, b) => b[1] - a[1] || (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0))
		.slice(0, 3);
	return {
		vocation,
		difficulty,
		runs,
		minRound: rounds[0] ?? 0,
		p10Round: percentile(rounds, 10),
		medianRound: percentile(rounds, 50),
		p90Round: percentile(rounds, 90),
		maxRound: rounds[rounds.length - 1] ?? 0,
		meanLevel: Math.floor(results.reduce((sum, r) => sum + r.level, 0) / runs),
		topKillers,
	};
}
