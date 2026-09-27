import json
from pathlib import Path

import pytest

from rpg.domain.json_types import json_int, json_list, json_obj
from rpg.domain.rng import Rng


def test_matches_reference_vectors(shared_dir: Path) -> None:
	document = json_obj(json.loads((shared_dir / "golden" / "prng.json").read_text(encoding="utf-8")))
	for raw in json_list(document["vectors"]):
		vector = json_obj(raw)
		rng = Rng(json_int(vector["seed"]))
		expected = [json_int(v) for v in json_list(vector["outputs"])]
		assert [rng.next_u32() for _ in expected] == expected


def test_seed_is_reduced_modulo_2_32() -> None:
	assert Rng(2**32 + 42).next_u32() == Rng(42).next_u32()


def test_state_round_trip_continues_sequence() -> None:
	rng = Rng(7)
	rng.next_u32()
	clone = Rng(rng.state)
	assert [clone.next_u32() for _ in range(5)] == [rng.next_u32() for _ in range(5)]


def test_roll_is_inclusive_and_bounded() -> None:
	rng = Rng(1)
	values = {rng.roll(3, 5) for _ in range(500)}
	assert values == {3, 4, 5}


def test_roll_rejects_inverted_range() -> None:
	with pytest.raises(ValueError, match="invalid range"):
		Rng(1).roll(5, 4)


@pytest.mark.parametrize(("percent", "expected"), [(0, False), (-5, False), (100, True), (150, True)])
def test_certain_chances_do_not_consume(percent: int, expected: bool) -> None:
	rng = Rng(9)
	state = rng.state
	assert rng.chance(percent) is expected
	assert rng.state == state


def test_chance_consumes_one_number() -> None:
	rng = Rng(9)
	reference = Rng(9)
	result = rng.chance(50)
	assert result is (reference.roll(1, 100) <= 50)
	assert rng.state == reference.state


def test_weighted_respects_zero_weights() -> None:
	rng = Rng(3)
	assert {rng.weighted([0, 5, 0]) for _ in range(50)} == {1}


def test_weighted_rejects_empty_total() -> None:
	with pytest.raises(ValueError, match="positive"):
		Rng(3).weighted([0, 0])


def test_pick() -> None:
	rng = Rng(5)
	assert rng.pick(["a"]) == "a"
	with pytest.raises(ValueError, match="empty"):
		rng.pick([])
