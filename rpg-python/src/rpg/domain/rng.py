"""mulberry32 PRNG shared by the three implementations (docs/cross-language-parity.md)."""

from collections.abc import Sequence

_MASK = 0xFFFFFFFF


def _imul(a: int, b: int) -> int:
	return (a * b) & _MASK


class Rng:
	"""Deterministic 32-bit generator. The engine must never use any other source of randomness."""

	__slots__ = ("_state",)

	def __init__(self, seed: int) -> None:
		self._state = seed & _MASK

	@property
	def state(self) -> int:
		return self._state

	def next_u32(self) -> int:
		self._state = (self._state + 0x6D2B79F5) & _MASK
		t = self._state
		t = _imul(t ^ (t >> 15), t | 1)
		t = (t ^ ((t + _imul(t ^ (t >> 7), t | 61)) & _MASK)) & _MASK
		return (t ^ (t >> 14)) & _MASK

	def roll(self, minimum: int, maximum: int) -> int:
		if minimum > maximum:
			raise ValueError(f"invalid range [{minimum}, {maximum}]")
		return minimum + self.next_u32() % (maximum - minimum + 1)

	def chance(self, percent: int) -> bool:
		# Certain outcomes don't consume a number, so adding a 0% effect never shifts the sequence.
		if percent <= 0:
			return False
		if percent >= 100:
			return True
		return self.roll(1, 100) <= percent

	def weighted(self, weights: Sequence[int]) -> int:
		total = sum(weights)
		if total <= 0:
			raise ValueError("weights must have a positive sum")
		roll = self.roll(1, total)
		cumulative = 0
		for index, weight in enumerate(weights):
			cumulative += weight
			if cumulative >= roll:
				return index
		raise AssertionError("unreachable")

	def pick[T](self, items: Sequence[T]) -> T:
		if not items:
			raise ValueError("cannot pick from an empty sequence")
		return items[self.roll(0, len(items) - 1)]
