defmodule Rpg.Application.Spawner do
  @moduledoc "Monster spawning for a round: tier, cycle, position and difficulty scaling (docs/game-design.md §3)."

  alias Rpg.Domain.Definitions.{DifficultyDef, GameData}
  alias Rpg.Domain.Entities.MonsterInstance
  alias Rpg.Domain.Formulas.RoundInfo
  alias Rpg.Domain.{Formulas, Rng}

  @spec spawn_monster(GameData.t(), Rng.t(), integer(), DifficultyDef.t()) ::
          {MonsterInstance.t(), RoundInfo.t(), Rng.t()}
  def spawn_monster(%GameData{} = data, %Rng{} = rng, round_number, %DifficultyDef{} = difficulty) do
    info = Formulas.round_info(round_number, data.balance, GameData.tier_count(data))

    {creature, rng} =
      if info.is_boss,
        do: {GameData.boss_of_tier(data, info.tier), rng},
        else: Rng.pick(rng, GameData.monsters_in_tier(data, info.tier))

    factors = Formulas.scaling(info, data.balance, difficulty)
    hp = max(1, Formulas.scale_stat(creature.hp, factors.hp_pct_product))

    attacks =
      Enum.map(creature.attacks, fn attack ->
        %{
          attack
          | min: max(1, Formulas.scale_stat(attack.min, factors.damage_pct_product)),
            max: max(1, Formulas.scale_stat(attack.max, factors.damage_pct_product))
        }
      end)

    monster = %MonsterInstance{
      creature_id: creature.id,
      is_boss: creature.is_boss,
      hp: hp,
      max_hp: hp,
      xp: Formulas.scale_reward(creature.xp, factors.reward_xp_pct_product),
      gold_min: Formulas.scale_reward(creature.gold_min, factors.reward_gold_pct_product),
      gold_max: Formulas.scale_reward(creature.gold_max, factors.reward_gold_pct_product),
      attacks: attacks
    }

    {monster, info, rng}
  end
end
