/**
 * Balance gate (docs/game-design.md §12): plays thousands of seeded bot runs per vocation and difficulty with an
 * implementation's simulator and checks the win rates in shared/data/balance-targets.json.
 *
 *   bun run balance:check [--impl python|rust|golang|typescript] [--runs N] [--jobs N]
 *
 * Every implementation replays the golden files identically, so any of them gives the same numbers; the compiled ones
 * are faster. Exit code 1 when a difficulty is outside target ± tolerance.
 */
import { readFileSync } from "node:fs";
import { cpus } from "node:os";
import { join } from "node:path";
import { parseArgs } from "node:util";

const ROOT = join(import.meta.dir, "..");

interface Targets {
	readonly runsPerVocation: number;
	readonly baseSeed: number;
	readonly tolerancePct: number;
	readonly vocationTolerancePct: number;
	readonly winRatePct: Readonly<Record<string, number>>;
}

export interface Row {
	readonly vocation: string;
	readonly difficulty: string;
	readonly runs: number;
	readonly wins: number;
}

const COMMANDS: Readonly<Record<string, { readonly cwd: string; readonly argv: readonly string[] }>> = {
	python: { cwd: "rpg-python", argv: ["uv", "run", "--frozen", "rpg"] },
	rust: { cwd: "rpg-rust", argv: ["cargo", "run", "--quiet", "--release", "--locked", "--"] },
	golang: { cwd: "rpg-golang", argv: ["go", "run", "./cmd/rpg"] },
	typescript: { cwd: "rpg-typescript", argv: ["bun", "run", "src/main.tsx"] },
};

/** Rows of the simulator report: `vocation difficulty runs wins win% ...`. */
export function parseReport(report: string): Row[] {
	const rows: Row[] = [];
	for (const line of report.split(/\r?\n/)) {
		const match = line.match(/^(\w+)\s+(\w+)\s+(\d+)\s+(\d+)\s+\d+%/);
		if (match?.[1] && match[2] && match[3] && match[4]) {
			rows.push({ vocation: match[1], difficulty: match[2], runs: Number(match[3]), wins: Number(match[4]) });
		}
	}
	return rows;
}

/** Mean of the per-vocation win rates of each difficulty, in percent with one decimal. */
export function winRates(rows: readonly Row[]): Map<string, number> {
	const byDifficulty = new Map<string, Map<string, { runs: number; wins: number }>>();
	for (const row of rows) {
		const vocations = byDifficulty.get(row.difficulty) ?? new Map<string, { runs: number; wins: number }>();
		const total = vocations.get(row.vocation) ?? { runs: 0, wins: 0 };
		vocations.set(row.vocation, { runs: total.runs + row.runs, wins: total.wins + row.wins });
		byDifficulty.set(row.difficulty, vocations);
	}
	const rates = new Map<string, number>();
	for (const [difficulty, vocations] of byDifficulty) {
		const perVocation = [...vocations.values()].map((v) => (v.wins * 100) / v.runs);
		const mean = perVocation.reduce((a, b) => a + b, 0) / perVocation.length;
		rates.set(difficulty, Math.round(mean * 10) / 10);
	}
	return rates;
}

async function runChunk(
	impl: string,
	difficulty: string,
	vocation: string,
	runs: number,
	seed: number,
): Promise<string> {
	const command = COMMANDS[impl];
	if (!command) throw new Error(`unknown implementation ${impl}`);
	const args = [...command.argv, "--simulate", `${runs}`, "--seed", `${seed}`];
	const child = Bun.spawn([...args, "--vocation", vocation, "--difficulty", difficulty], {
		cwd: join(ROOT, command.cwd),
		stdout: "pipe",
		stderr: "inherit",
	});
	const output = await new Response(child.stdout).text();
	if ((await child.exited) !== 0) throw new Error(`${args.join(" ")} failed`);
	return output;
}

async function main(): Promise<number> {
	const { values } = parseArgs({
		options: { impl: { type: "string", default: "python" }, runs: { type: "string" }, jobs: { type: "string" } },
	});
	const targets = JSON.parse(readFileSync(join(ROOT, "shared/data/balance-targets.json"), "utf-8")) as Targets;
	const vocations = (
		JSON.parse(readFileSync(join(ROOT, "shared/data/vocations.json"), "utf-8")) as { vocations: { id: string }[] }
	).vocations.map((v) => v.id);
	const impl = values.impl ?? "python";
	const runs = Number(values.runs ?? targets.runsPerVocation);
	const jobs = Number(values.jobs ?? cpus().length);
	const chunk = Math.max(1, Math.ceil(runs / jobs));

	const tasks: (() => Promise<string>)[] = [];
	for (const difficulty of Object.keys(targets.winRatePct)) {
		for (const vocation of vocations) {
			for (let offset = 0; offset < runs; offset += chunk) {
				const size = Math.min(chunk, runs - offset);
				tasks.push(() => runChunk(impl, difficulty, vocation, size, targets.baseSeed + offset));
			}
		}
	}
	const started = Date.now();
	const outputs: string[] = [];
	let next = 0;
	await Promise.all(
		Array.from({ length: Math.min(jobs, tasks.length) }, async () => {
			while (next < tasks.length) {
				const task = tasks[next++];
				if (task) outputs.push(await task());
			}
		}),
	);

	const rows = outputs.flatMap(parseReport);
	const rates = winRates(rows);
	console.log(`balance gate: ${impl}, ${runs} runs per vocation and difficulty, ${(Date.now() - started) / 1000} s`);
	let failed = false;
	for (const [difficulty, target] of Object.entries(targets.winRatePct)) {
		const rate = rates.get(difficulty) ?? 0;
		const ok = Math.abs(rate - target) <= targets.tolerancePct;
		failed ||= !ok;
		const perVocation = vocations.map((vocation) => {
			const mine = rows.filter((r) => r.difficulty === difficulty && r.vocation === vocation);
			const total = mine.reduce((a, r) => ({ runs: a.runs + r.runs, wins: a.wins + r.wins }), {
				runs: 0,
				wins: 0,
			});
			const vocationRate = Math.round((total.wins * 1000) / Math.max(1, total.runs)) / 10;
			// Each vocation must also be close to the target, so no class is the obvious pick.
			const vocationOk = Math.abs(vocationRate - target) <= targets.vocationTolerancePct;
			failed ||= !vocationOk;
			return `${vocationOk ? "" : "!"}${vocation} ${vocationRate}%`;
		});
		console.log(
			`${ok ? "ok  " : "FAIL"} ${difficulty.padEnd(7)} win rate ${rate}% (target ${target}% ± ${targets.tolerancePct}) — ${perVocation.join(", ")}`,
		);
	}
	if (failed) console.log(`a vocation marked ! is outside target ± ${targets.vocationTolerancePct}`);
	return failed ? 1 : 0;
}

if (import.meta.main) {
	process.exitCode = await main();
}
