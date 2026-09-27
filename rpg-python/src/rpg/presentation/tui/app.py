"""Textual TUI — arrives in 0.3.0 (PLAN.md M2). Until then the game runs headless through the simulator."""

import sys

from rpg.presentation.cli import CliOptions


def run_tui(options: CliOptions) -> int:
	del options
	print("The terminal UI arrives in version 0.3.0. Try: uv run rpg --simulate 20", file=sys.stderr)
	return 1
