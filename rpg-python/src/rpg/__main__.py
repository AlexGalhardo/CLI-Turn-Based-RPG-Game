"""Entry point: `uv run rpg [flags]`."""

import sys

from rpg.application.simulator import simulate
from rpg.infrastructure.data_loader import load_game_data
from rpg.infrastructure.paths import find_shared_dir
from rpg.presentation.cli import CliOptions, parse_args
from rpg.presentation.simulator_report import render_report


def run_simulator(options: CliOptions) -> int:
	data = load_game_data(find_shared_dir())
	vocations = [options.vocation] if options.vocation else [v.id for v in data.vocations]
	difficulties = [options.difficulty] if options.difficulty else [d.id for d in data.balance.difficulties]
	runs = options.simulate or 1
	try:
		summaries = [simulate(data, v, d, runs, options.seed or 1) for v in vocations for d in difficulties]
	except ValueError as exc:
		print(f"error: {exc}", file=sys.stderr)
		return 2
	print(render_report(summaries, data))
	return 0


def main(argv: list[str] | None = None) -> int:
	options = parse_args(argv)
	if options.simulate is not None:
		return run_simulator(options)
	from rpg.presentation.tui.app import run_tui  # noqa: PLC0415 — Textual is only imported when the TUI runs

	return run_tui(options)


if __name__ == "__main__":
	sys.exit(main())
