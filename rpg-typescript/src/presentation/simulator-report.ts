import { type SimulationSummary, winRatePct } from "../application/simulator";
import type { GameData } from "../domain/definitions";

const HEADER = [
	"vocation",
	"difficulty",
	"runs",
	"wins",
	"win %",
	"min",
	"p10",
	"median",
	"p90",
	"max",
	"avg lvl",
	"top killers",
];

export function renderReport(summaries: readonly SimulationSummary[], data: GameData): string {
	const rows: string[][] = [HEADER];
	for (const s of summaries) {
		const killers = s.topKillers.map(([id, count]) => `${data.creature(id).name} (${count})`).join(", ");
		rows.push([
			s.vocation,
			s.difficulty,
			String(s.runs),
			String(s.wins),
			`${winRatePct(s)}%`,
			String(s.minRound),
			String(s.p10Round),
			String(s.medianRound),
			String(s.p90Round),
			String(s.maxRound),
			String(s.meanLevel),
			killers,
		]);
	}
	const widths = HEADER.map((_, i) => Math.max(...rows.map((row) => (row[i] ?? "").length)));
	const lines = rows.map((row) =>
		row
			.map((cell, i) => cell.padEnd(widths[i] ?? 0))
			.join("  ")
			.trimEnd(),
	);
	lines.splice(1, 0, widths.map((w) => "-".repeat(w)).join("  "));
	return lines.join("\n");
}
