defmodule Rpg.Domain.Formulas do
  @moduledoc "Pure integer formulas from docs/game-design.md. No randomness, no state."

  alias Rpg.Domain.Definitions.{Balance, DifficultyDef, SpellLevelDef}

  defmodule RoundInfo do
    @moduledoc false
    @enforce_keys [:round, :tier, :cycle, :position, :is_boss]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            round: integer(),
            tier: integer(),
            cycle: integer(),
            position: integer(),
            is_boss: boolean()
          }
  end

  defmodule Scaling do
    @moduledoc false
    @enforce_keys [:hp_pct_product, :damage_pct_product, :reward_xp_pct_product, :reward_gold_pct_product]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            hp_pct_product: integer(),
            damage_pct_product: integer(),
            reward_xp_pct_product: integer(),
            reward_gold_pct_product: integer()
          }
  end

  @doc "floor(value * percent / 100) for non-negative operands — the only rounding rule of the engine."
  @spec pct(integer(), integer()) :: integer()
  def pct(value, percent) when value < 0 or percent < 0, do: raise(ArgumentError, "pct operands must be non-negative")
  def pct(value, percent), do: div(value * percent, 100)

  @spec clamp(integer(), integer(), integer()) :: integer()
  def clamp(value, minimum, maximum), do: max(minimum, min(maximum, value))

  @doc "Total experience needed to reach `level` (Tibia formula)."
  @spec xp_for_level(integer()) :: integer()
  def xp_for_level(level) when level <= 1, do: 0
  def xp_for_level(level), do: div(50 * (level ** 3 - 6 * level ** 2 + 17 * level - 12), 3)

  @doc "Total mana spent (since level 1) needed to advance from `magic_level` to the next one."
  @spec mana_for_magic_level(integer(), Balance.t()) :: integer()
  def mana_for_magic_level(magic_level, %Balance{} = balance) do
    base = balance.magic_level_base

    {_step, total} =
      Enum.reduce(1..(magic_level - 1)//1, {base, base}, fn _, {step, total} ->
        step = pct(step, balance.magic_level_growth_pct)
        {step, total + step}
      end)

    total
  end

  @spec spell_level_for_uses(integer(), [SpellLevelDef.t()]) :: SpellLevelDef.t()
  def spell_level_for_uses(uses, [first | _] = levels) do
    Enum.reduce(levels, first, fn level, current -> if uses >= level.uses, do: level, else: current end)
  end

  @spec armor_mitigation(integer(), integer()) :: integer()
  def armor_mitigation(damage, armor), do: div(damage * 100, 100 + armor)

  @spec round_info(integer(), Balance.t(), integer()) :: RoundInfo.t()
  def round_info(round_number, %Balance{rounds_per_tier: per_tier}, tier_count) do
    index = round_number - 1
    position = rem(index, per_tier)

    %RoundInfo{
      round: round_number,
      tier: rem(div(index, per_tier), tier_count),
      cycle: div(index, per_tier * tier_count),
      position: position,
      is_boss: position == per_tier - 1
    }
  end

  @spec scaling(RoundInfo.t(), Balance.t(), DifficultyDef.t()) :: Scaling.t()
  def scaling(%RoundInfo{} = info, %Balance{} = balance, %DifficultyDef{} = difficulty) do
    cycle_pct = 100 + info.cycle * balance.cycle_stat_pct
    reward_pct = 100 + info.cycle * balance.cycle_reward_pct
    position_pct = if info.is_boss, do: 100, else: 100 + info.position * balance.position_pct

    %Scaling{
      hp_pct_product: difficulty.hp_pct * cycle_pct * position_pct,
      damage_pct_product: difficulty.damage_pct * cycle_pct * position_pct,
      reward_xp_pct_product: difficulty.xp_pct * reward_pct,
      reward_gold_pct_product: difficulty.gold_pct * reward_pct
    }
  end

  @doc "Applies three chained percentages in one floor: value * a * b * c / 1_000_000."
  @spec scale_stat(integer(), integer()) :: integer()
  def scale_stat(value, pct_product), do: div(value * pct_product, 1_000_000)

  @doc "Applies two chained percentages in one floor: value * a * b / 10_000."
  @spec scale_reward(integer(), integer()) :: integer()
  def scale_reward(value, pct_product), do: div(value * pct_product, 10_000)
end
