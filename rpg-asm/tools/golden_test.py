"""Golden-file parity tests of the Assembly port (docs/asm.md, docs/cross-language-parity.md §4).

The binary has no JSON parser, so this tool (standard library only) does the conversion on both sides:

- every `shared/golden/<scenario>.json` becomes a plain-text script for `rpg-asm --replay` (seed, config, one command
  per line) and the canonical lines the binary must print: one line per event with every field in JSON order, then
  the flattened `finalState` (`state.*`) and `finalRun` (`run.*`) objects;
- `prng.json` is checked with `rpg-asm --prng`;
- for the `bot-full-run-*` scenarios a second script replaces the recorded commands with `@bot`: the port's own
  bot must then choose exactly the recorded commands (printed as `> command` lines) and produce the same events.

Nothing is dropped from the comparison: event lines are compared in order, state lines as a set (JSON objects are
unordered), and a list always carries its length (`path.#=n`).

Usage: python tools/golden_test.py --bin build/rpg-asm --shared ../shared
"""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

MAX_SHOWN_DIFFERENCES = 12


def scalar(value: object) -> str:
	if isinstance(value, bool):
		return "true" if value else "false"
	if value is None:
		return "null"
	return str(value)


def flatten(prefix: str, value: object, out: list[str]) -> None:
	"""`{"a": {"b": [1]}}` → `a.b.#=1`, `a.b.0=1`: the form printed by dump_state in presentation/replay.asm."""
	if isinstance(value, dict):
		for key, item in value.items():
			flatten(f"{prefix}.{key}", item, out)
	elif isinstance(value, list):
		out.append(f"{prefix}.#={len(value)}")
		for index, item in enumerate(value):
			flatten(f"{prefix}.{index}", item, out)
	else:
		out.append(f"{prefix}={scalar(value)}")


def event_lines(index: int, events: list[dict[str, Any]]) -> list[str]:
	lines = []
	for event in events:
		fields = "".join(f" {key}={scalar(value)}" for key, value in event.items() if key != "type")
		lines.append(f"[{index}] {event['type']}{fields}")
	return lines


def command_line(command: dict[str, Any]) -> str:
	return " ".join(
		[
			command["type"],
			*(scalar(value) for key, value in command.items() if key != "type"),
		]
	)


def header(golden: dict[str, Any]) -> list[str]:
	config = golden["config"]
	return [
		f"seed {golden['seed']}",
		f"name {config['name']}",
		f"vocation {config['vocation']}",
		f"difficulty {config['difficulty']}",
		f"autoEquip {scalar(config['autoEquip'])}",
		"begin",
	]


def expected_state(golden: dict[str, Any]) -> list[str]:
	lines: list[str] = []
	flatten("state", golden["finalState"], lines)
	flatten("run", golden["finalRun"], lines)
	return lines


def split_output(output: str) -> tuple[list[str], list[str]]:
	"""Separates the ordered part (events and bot commands) from the final-state lines."""
	ordered: list[str] = []
	state: list[str] = []
	for line in output.splitlines():
		(ordered if line.startswith(("[", "> ")) else state).append(line)
	return ordered, state


def differences(expected: list[str], actual: list[str]) -> list[str]:
	result = []
	for index in range(max(len(expected), len(actual))):
		want = expected[index] if index < len(expected) else "<nothing>"
		got = actual[index] if index < len(actual) else "<nothing>"
		if want != got:
			result.append(f"    line {index + 1}:\n      expected: {want}\n      actual:   {got}")
			if len(result) >= MAX_SHOWN_DIFFERENCES:
				result.append("    ...")
				break
	return result


class Runner:
	def __init__(self, binary: Path, workdir: Path) -> None:
		self.binary = binary
		self.workdir = workdir
		self.failures = 0
		self.checks = 0

	def run(self, *args: str) -> str:
		result = subprocess.run(
			[str(self.binary), *args],
			capture_output=True,
			text=True,
			encoding="utf-8",
			check=False,
		)
		if result.returncode != 0:
			return f"<exit code {result.returncode}> {result.stderr.strip()}"
		return result.stdout

	def report(self, name: str, problems: list[str]) -> None:
		self.checks += 1
		if problems:
			self.failures += 1
			print(f"FAIL {name}")
			print("\n".join(problems))
		else:
			print(f"ok   {name}")

	def prng(self, path: Path) -> None:
		for vector in json.loads(path.read_text(encoding="utf-8"))["vectors"]:
			expected = [str(output) for output in vector["outputs"]]
			actual = self.run("--prng", str(vector["seed"]), str(len(expected))).splitlines()
			self.report(f"prng seed {vector['seed']}", differences(expected, actual))

	def replay(
		self,
		name: str,
		script: list[str],
		expected_ordered: list[str],
		expected_state_lines: list[str],
	) -> None:
		script_path = self.workdir / f"{name}.script"
		script_path.write_text("\n".join(script) + "\n", encoding="utf-8", newline="\n")
		ordered, state = split_output(self.run("--replay", str(script_path)))
		problems = differences(expected_ordered, ordered)
		if not problems:
			problems = differences(sorted(expected_state_lines), sorted(state))
		lines = len(expected_ordered) + len(expected_state_lines)
		self.report(f"{name} ({lines} lines)", problems)

	def scenario(self, path: Path) -> None:
		golden = json.loads(path.read_text(encoding="utf-8"))
		commands = golden["commands"]
		events = golden["events"]
		state = expected_state(golden)

		ordered = [line for index, batch in enumerate(events) for line in event_lines(index, batch)]
		self.replay(
			f"replay {path.stem}",
			header(golden) + [command_line(c) for c in commands],
			ordered,
			state,
		)

		if path.stem.startswith("bot-full-run-"):
			# Same run, but the port's bot chooses: its commands must be the recorded ones.
			ordered = event_lines(0, events[0])
			for index, command in enumerate(commands, start=1):
				ordered.append(f"> {command_line(command)}")
				ordered += event_lines(index, events[index])
			self.replay(f"bot    {path.stem}", [*header(golden), "@bot"], ordered, state)


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--bin", type=Path, required=True, help="path of the rpg-asm binary")
	parser.add_argument("--shared", type=Path, required=True, help="path of the shared/ folder")
	args = parser.parse_args()

	golden_dir = args.shared / "golden"
	scenarios = sorted(path for path in golden_dir.glob("*.json") if path.name != "prng.json")
	if not scenarios:
		print(f"no golden scenario found in {golden_dir}", file=sys.stderr)
		return 2
	with tempfile.TemporaryDirectory(prefix="rpg-asm-golden-") as workdir:
		runner = Runner(args.bin.resolve(), Path(workdir))
		runner.prng(golden_dir / "prng.json")
		for path in scenarios:
			runner.scenario(path)
	print(f"{runner.checks - runner.failures}/{runner.checks} checks passed")
	return 1 if runner.failures else 0


if __name__ == "__main__":
	sys.exit(main())
