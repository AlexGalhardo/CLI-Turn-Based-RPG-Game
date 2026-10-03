defmodule Rpg.Application.Loot do
  @moduledoc "Item factory: base item + rarity + affixes (docs/game-design.md §8)."

  alias Rpg.Domain.Definitions.{GameData, ItemDef, RarityDef, VocationDef}
  alias Rpg.Domain.Entities.{AffixRoll, ItemInstance}
  alias Rpg.Domain.Rng

  @spec can_use(ItemDef.t(), VocationDef.t()) :: boolean()
  def can_use(%ItemDef{slot: "weapon", type: type}, %VocationDef{} = vocation), do: type in vocation.weapon_types
  def can_use(%ItemDef{slot: "shield", type: type}, %VocationDef{} = vocation), do: type in vocation.shield_types
  def can_use(%ItemDef{}, %VocationDef{}), do: true

  @doc "Weighted roll in the order of `balance.rarities`; zero weights are skipped and a single option is not rolled."
  @spec roll_rarity(GameData.t(), Rng.t(), %{String.t() => integer()}) :: {RarityDef.t(), Rng.t()}
  def roll_rarity(%GameData{} = data, %Rng{} = rng, weights) do
    options =
      data.balance.rarities
      |> Enum.map(&{&1, Map.get(weights, &1.id, 0)})
      |> Enum.filter(fn {_rarity, weight} -> weight > 0 end)

    case options do
      [] ->
        raise ArgumentError, "rarity table without a positive weight"

      [{rarity, _weight}] ->
        {rarity, rng}

      _ ->
        {index, rng} = Rng.weighted(rng, Enum.map(options, &elem(&1, 1)))
        {elem(Enum.at(options, index), 0), rng}
    end
  end

  @doc """
  Generates one item. Options: `:vocation`, `:tier`, `:weights` (rarity id => weight), `:uid`.
  Returns `{nil, rng}` (consuming no randomness) when no item fits the vocation and tier.
  """
  @spec generate_item(GameData.t(), Rng.t(), keyword()) :: {ItemInstance.t() | nil, Rng.t()}
  def generate_item(%GameData{} = data, %Rng{} = rng, opts) do
    vocation = Keyword.fetch!(opts, :vocation)
    tier = Keyword.fetch!(opts, :tier)
    lowest_tier = max(0, tier - 1)

    candidates =
      data.items
      |> Enum.filter(&(&1.tier >= lowest_tier and &1.tier <= tier and can_use(&1, vocation)))
      |> Enum.sort_by(& &1.id)

    if candidates == [] do
      {nil, rng}
    else
      {base, rng} = Rng.pick(rng, candidates)
      {rarity, rng} = roll_rarity(data, rng, Keyword.fetch!(opts, :weights))
      {affix_count, rng} = Rng.roll(rng, rarity.affix_min, rarity.affix_max)
      {rolls, rng} = roll_affixes(data, rng, base, tier, affix_count, [])

      item = %ItemInstance{
        uid: Keyword.fetch!(opts, :uid),
        item_id: base.id,
        rarity: rarity.id,
        tier: tier,
        affixes: rolls
      }

      {item, rng}
    end
  end

  defp roll_affixes(_data, rng, _base, _tier, 0, rolls), do: {Enum.reverse(rolls), rng}

  defp roll_affixes(data, rng, base, tier, remaining, rolls) do
    used_stats = Enum.map(rolls, & &1.stat)

    pool =
      data.affixes
      |> Enum.filter(&(base.slot in &1.slots and &1.stat not in used_stats))
      |> Enum.sort_by(& &1.id)

    if pool == [] do
      {Enum.reverse(rolls), rng}
    else
      {affix, rng} = Rng.pick(rng, pool)
      {value, rng} = Rng.roll(rng, affix.min, affix.max)
      roll = %AffixRoll{stat: affix.stat, value: value + tier * affix.per_tier}
      roll_affixes(data, rng, base, tier, remaining - 1, [roll | rolls])
    end
  end
end
