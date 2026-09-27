"""Command-line flags, identical in the three implementations (docs/tui.md)."""

import argparse
from dataclasses import dataclass

from rpg import __version__
from rpg.infrastructure.i18n import SUPPORTED_LOCALES


@dataclass(frozen=True, slots=True)
class CliOptions:
	seed: int | None
	lang: str | None
	no_anim: bool
	data_dir: str | None
	simulate: int | None
	vocation: str | None
	difficulty: str | None


def _non_negative(value: str) -> int:
	number = int(value)
	if number < 0:
		raise argparse.ArgumentTypeError("must be >= 0")
	return number


def _positive(value: str) -> int:
	number = int(value)
	if number <= 0:
		raise argparse.ArgumentTypeError("must be > 0")
	return number


def build_parser() -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser(prog="rpg", description="Endless turn-based RPG for the terminal.")
	parser.add_argument("--version", action="version", version=f"%(prog)s {__version__} (python)")
	parser.add_argument("--seed", type=_non_negative, help="deterministic run")
	parser.add_argument("--lang", choices=SUPPORTED_LOCALES, help="override the saved language")
	parser.add_argument("--no-anim", action="store_true", help="disable animations")
	parser.add_argument("--data-dir", help="saves/profile location")
	parser.add_argument("--simulate", type=_positive, metavar="N", help="run N headless bot games and print a report")
	parser.add_argument("--vocation", help="(simulator) restrict to one vocation")
	parser.add_argument("--difficulty", help="(simulator) restrict to one difficulty")
	return parser


def parse_args(argv: list[str] | None = None) -> CliOptions:
	namespace = build_parser().parse_args(argv)
	return CliOptions(
		seed=namespace.seed,
		lang=namespace.lang,
		no_anim=namespace.no_anim,
		data_dir=namespace.data_dir,
		simulate=namespace.simulate,
		vocation=namespace.vocation,
		difficulty=namespace.difficulty,
	)
