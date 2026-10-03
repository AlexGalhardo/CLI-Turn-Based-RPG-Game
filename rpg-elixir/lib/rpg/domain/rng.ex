defmodule Rpg.Domain.Rng do
  @moduledoc """
  mulberry32 PRNG shared by every implementation (docs/cross-language-parity.md).

  The generator is an immutable value: every function returns `{result, next_rng}`, so the caller threads the state
  explicitly. The engine must never use any other source of randomness.
  """

  import Bitwise

  @mask 0xFFFFFFFF

  @enforce_keys [:state]
  defstruct [:state]

  @type t :: %__MODULE__{state: non_neg_integer()}

  @spec new(integer()) :: t()
  def new(seed), do: %__MODULE__{state: band(seed, @mask)}

  @spec state(t()) :: non_neg_integer()
  def state(%__MODULE__{state: state}), do: state

  defp imul(a, b), do: band(a * b, @mask)

  @spec next_u32(t()) :: {non_neg_integer(), t()}
  def next_u32(%__MODULE__{state: state}) do
    state = band(state + 0x6D2B79F5, @mask)
    t = imul(bxor(state, state >>> 15), bor(state, 1))
    t = band(bxor(t, band(t + imul(bxor(t, t >>> 7), bor(t, 61)), @mask)), @mask)
    {band(bxor(t, t >>> 14), @mask), %__MODULE__{state: state}}
  end

  @spec roll(t(), integer(), integer()) :: {integer(), t()}
  def roll(_rng, minimum, maximum) when minimum > maximum do
    raise ArgumentError, "invalid range [#{minimum}, #{maximum}]"
  end

  def roll(rng, minimum, maximum) do
    {value, rng} = next_u32(rng)
    {minimum + rem(value, maximum - minimum + 1), rng}
  end

  @doc "Certain outcomes don't consume a number, so adding a 0% effect never shifts the sequence."
  @spec chance(t(), integer()) :: {boolean(), t()}
  def chance(rng, percent) when percent <= 0, do: {false, rng}
  def chance(rng, percent) when percent >= 100, do: {true, rng}

  def chance(rng, percent) do
    {value, rng} = roll(rng, 1, 100)
    {value <= percent, rng}
  end

  @spec weighted(t(), [non_neg_integer()]) :: {non_neg_integer(), t()}
  def weighted(rng, weights) do
    total = Enum.sum(weights)
    if total <= 0, do: raise(ArgumentError, "weights must have a positive sum")
    {target, rng} = roll(rng, 1, total)

    index =
      weights
      |> Enum.scan(&(&1 + &2))
      |> Enum.find_index(&(&1 >= target))

    {index, rng}
  end

  @spec pick(t(), [term()]) :: {term(), t()}
  def pick(_rng, []), do: raise(ArgumentError, "cannot pick from an empty sequence")

  def pick(rng, items) do
    {index, rng} = roll(rng, 0, length(items) - 1)
    {Enum.at(items, index), rng}
  end
end
