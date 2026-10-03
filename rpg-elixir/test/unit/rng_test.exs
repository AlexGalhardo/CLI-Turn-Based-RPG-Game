defmodule Rpg.Unit.RngTest do
  use ExUnit.Case, async: true

  alias Rpg.Domain.Rng
  alias Rpg.Test.Helpers

  defp take(rng, count) do
    Enum.map_reduce(1..count, rng, fn _, rng -> Rng.next_u32(rng) end) |> elem(0)
  end

  test "matches the reference vectors" do
    document = Helpers.read_json(Path.join(Helpers.golden_dir(), "prng.json"))

    for vector <- document["vectors"] do
      assert take(Rng.new(vector["seed"]), length(vector["outputs"])) == vector["outputs"]
    end
  end

  test "seed is reduced modulo 2^32" do
    assert elem(Rng.next_u32(Rng.new(2 ** 32 + 42)), 0) == elem(Rng.next_u32(Rng.new(42)), 0)
  end

  test "state round trip continues the sequence" do
    {_value, rng} = Rng.next_u32(Rng.new(7))
    clone = Rng.new(Rng.state(rng))
    assert take(clone, 5) == take(rng, 5)
  end

  test "roll is inclusive and bounded" do
    {values, _rng} = Enum.map_reduce(1..500, Rng.new(1), fn _, rng -> Rng.roll(rng, 3, 5) end)
    assert MapSet.new(values) == MapSet.new([3, 4, 5])
  end

  test "roll rejects an inverted range" do
    assert_raise ArgumentError, ~r/invalid range/, fn -> Rng.roll(Rng.new(1), 5, 4) end
  end

  for {percent, expected} <- [{0, false}, {-5, false}, {100, true}, {150, true}] do
    test "certain chance #{percent} does not consume" do
      rng = Rng.new(9)
      assert Rng.chance(rng, unquote(percent)) == {unquote(expected), rng}
    end
  end

  test "chance consumes one number" do
    {result, rng} = Rng.chance(Rng.new(9), 50)
    {roll, reference} = Rng.roll(Rng.new(9), 1, 100)
    assert result == roll <= 50
    assert rng == reference
  end

  test "weighted respects zero weights" do
    {values, _rng} = Enum.map_reduce(1..50, Rng.new(3), fn _, rng -> Rng.weighted(rng, [0, 5, 0]) end)
    assert Enum.uniq(values) == [1]
  end

  test "weighted rejects an empty total" do
    assert_raise ArgumentError, ~r/positive/, fn -> Rng.weighted(Rng.new(3), [0, 0]) end
  end

  test "pick" do
    assert {"a", _rng} = Rng.pick(Rng.new(5), ["a"])
    assert_raise ArgumentError, ~r/empty/, fn -> Rng.pick(Rng.new(5), []) end
  end
end
