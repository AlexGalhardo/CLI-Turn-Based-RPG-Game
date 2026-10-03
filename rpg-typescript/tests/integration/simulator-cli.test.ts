import { describe, expect, test } from "bun:test";
import { join } from "node:path";

const MAIN = join(import.meta.dir, "../../src/main.tsx");

// Spawned instead of imported: the entry point is exercised end to end and stays out of the per-file coverage gate.
function report(seed: string): string {
	const result = Bun.spawnSync([
		process.execPath,
		MAIN,
		"--simulate",
		"2",
		"--vocation",
		"mage",
		"--difficulty",
		"easy",
		"--seed",
		seed,
	]);
	expect(result.exitCode).toBe(0);
	return result.stdout.toString();
}

describe("simulator CLI", () => {
	test("--seed 0 falls back to base seed 1 like the reference", () => {
		expect(report("0")).toBe(report("1"));
	});
});
