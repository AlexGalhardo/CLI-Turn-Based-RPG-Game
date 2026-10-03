from collections.abc import Iterable

from rpg.application.simulator import SimulationSummary
from rpg.domain.definitions import GameData

HEADER = (
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
)


def render_report(summaries: Iterable[SimulationSummary], data: GameData) -> str:
	rows = [HEADER]
	for s in summaries:
		killers = ", ".join(f"{data.creature(cid).name} ({n})" for cid, n in s.top_killers)
		rows.append(
			(
				s.vocation,
				s.difficulty,
				str(s.runs),
				str(s.wins),
				f"{s.win_rate_pct}%",
				str(s.min_round),
				str(s.p10_round),
				str(s.median_round),
				str(s.p90_round),
				str(s.max_round),
				str(s.mean_level),
				killers,
			)
		)
	widths = [max(len(row[i]) for row in rows) for i in range(len(HEADER))]
	lines = ["  ".join(cell.ljust(widths[i]) for i, cell in enumerate(row)).rstrip() for row in rows]
	lines.insert(1, "  ".join("-" * w for w in widths))
	return "\n".join(lines)
