defmodule Rpg.Application.Loot do
  @moduledoc "Item factory: base item + rarity + affixes (docs/game-design.md §8)."

  alias Rpg.Domain.Definitions.{DifficultyDef, GameData, ItemDef, VocationDef}
  alias Rpg.Domain.Entities.{AffixRoll, ItemInstance}
  alias Rpg.Domain.{Formulas, Rng}

  @spec can_use(ItemDef.t(), VocationDef.t()) :: boolean()
  def can_use(%ItemDef{slot: "weapon", type: type}, %VocationDef{} = vocation), do: type in vocation.weapon_types
  def can_use(%ItemDef{slot: "shield", type: type}, %VocationDef{} = vocation), do: type in vocation.shield_types
  def can_use(%ItemDef{}, %VocationDef{}), do: true

  @spec rarity_weights(GameData.t(), String.t(), DifficultyDef.t()) :: [non_neg_integer()]
  def rarity_weights(%GameData{} = data, table, %DifficultyDef{} = difficulty) do
    weights = Map.fetch!(data.balance.rarity_weights, table)

    Enum.map(data.balance.rarities, fn rarity ->
      weight = Map.get(weights, rarity.id, 0)
      if rarity.id == "common", do: weight, else: Formulas.pct(weight, difficulty.non_common_weight_pct)
    end)
  end

  @doc """
  Generates one item. Options: `:vocation`, `:tier`, `:table`, `:difficulty`, `:uid`.
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
      weights = rarity_weights(data, Keyword.fetch!(opts, :table), Keyword.fetch!(opts, :difficulty))
      {rarity_index, rng} = Rng.weighted(rng, weights)
      rarity = Enum.at(data.balance.rarities, rarity_index)
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
