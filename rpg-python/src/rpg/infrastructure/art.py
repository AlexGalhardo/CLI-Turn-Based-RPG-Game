"""Parses shared/art files: `@animation` headers, frames separated by `%%` (docs/data-format.md)."""

from dataclasses import dataclass
from pathlib import Path

from rpg.domain.definitions import MonsterDef

type Frame = tuple[str, ...]
type Animations = dict[str, tuple[Frame, ...]]


def parse_art(text: str) -> Animations:
	animations: dict[str, list[list[str]]] = {}
	current: list[list[str]] | None = None
	for line in text.rstrip("\n").split("\n"):
		if line.startswith("@"):
			current = [[]]
			animations[line[1:].strip()] = current
		elif line == "%%":
			if current is None:
				raise ValueError("frame separator before any @animation")
			current.append([])
		elif current is None:
			raise ValueError("art content before the first @animation")
		else:
			current[-1].append(line)
	return {name: tuple(tuple(frame) for frame in frames) for name, frames in animations.items()}


@dataclass
class ArtLibrary:
	shared_dir: Path

	def __post_init__(self) -> None:
		self._cache: dict[Path, Animations] = {}

	def for_creature(self, creature: MonsterDef) -> Animations:
		folder, name = ("bosses", creature.id) if creature.is_boss else ("families", creature.family)
		return self.load_file(folder, name)

	def load_file(self, folder: str, name: str) -> Animations:
		return self._load(self.shared_dir / "art" / folder / f"{name}.txt")

	def _load(self, path: Path) -> Animations:
		if path not in self._cache:
			self._cache[path] = parse_art(path.read_text(encoding="utf-8"))
		return self._cache[path]


def frame_for(animations: Animations, animation: str, tick: int) -> Frame:
	"""Frame of `animation` at animation tick `tick`, falling back to idle; empty when there is no art."""
	frames = animations.get(animation) or animations.get("idle") or ()
	if not frames:
		return ()
	return frames[tick % len(frames)]
