"""Checks that `rpg-asm --simulate` prints the same report, byte for byte, as the Python reference (docs/asm.md).

The reference simulator only needs the standard library, so it is run straight from its sources with
`PYTHONPATH=<rpg-python/src>`. Seed 0 is always included: the reference uses `seed or 1` (port-feature skill).

Usage: python tools/simulator_test.py --bin build/rpg-asm --reference ../rpg-python/src [--runs 20]
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

CASES: tuple[tuple[str, ...], ...] = (
	("--seed", "42"),
	("--seed", "0"),
	("--seed", "7", "--vocation", "mage", "--difficulty", "hard"),
)


def output_of(command: list[str], env: dict[str, str] | None = None) -> bytes:
	result = subprocess.run(command, capture_output=True, check=False, env=env)
	if result.returncode != 0:
		raise RuntimeError(f"{' '.join(command)} failed: {result.stderr.decode(errors='replace')}")
	return result.stdout.replace(b"\r\n", b"\n")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--bin", type=Path, required=True, help="path of the rpg-asm binary")
	parser.add_argument("--reference", type=Path, required=True, help="path of rpg-python/src")
	parser.add_argument("--runs", type=int, default=20)
	args = parser.parse_args()

	env = {
		**os.environ,
		"PYTHONPATH": str(args.reference.resolve()),
		"PYTHONIOENCODING": "utf-8",
	}
	failures = 0
	for case in CASES:
		flags = ["--simulate", str(args.runs), *case]
		expected = output_of([sys.executable, "-m", "rpg", *flags], env)
		actual = output_of([str(args.bin.resolve()), *flags])
		if expected == actual:
			print(f"ok   simulator {' '.join(flags)} ({len(expected)} bytes)")
		else:
			failures += 1
			print(f"FAIL simulator {' '.join(flags)}")
			print(
				"--- reference\n"
				+ expected.decode(errors="replace")
				+ "--- rpg-asm\n"
				+ actual.decode(errors="replace")
			)
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
