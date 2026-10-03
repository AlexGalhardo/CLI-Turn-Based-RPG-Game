defmodule Rpg.Application.Spawner do
  @moduledoc "Monster spawning for a round: tier, cycle, position, enemy class and difficulty scaling (docs/game-design.md §3)."

  alias Rpg.Domain.Definitions.{Balance, DifficultyDef, GameData}
  alias Rpg.Domain.Entities.MonsterInstance
  alias Rpg.Domain.Formulas.RoundInfo
  alias Rpg.Domain.{Formulas, Rng}

  @spec spawn_monster(GameData.t(), Rng.t(), integer(), DifficultyDef.t()) ::
          {MonsterInstance.t(), RoundInfo.t(), Rng.t()}
  def spawn_monster(%GameData{} = data, %Rng{} = rng, round_number, %DifficultyDef{} = difficulty) do
    balance = data.balance
    info = Formulas.round_info(round_number, balance, GameData.tier_count(data))

    {creature, enemy_class, rng} =
      if info.is_boss do
        {GameData.boss_of_tier(data, info.tier), "boss", rng}
      else
        {creature, rng} = Rng.pick(rng, GameData.monsters_in_tier(data, info.tier))
        {elite, rng} = Rng.chance(rng, balance.elite_chance_pct)
        {creature, if(elite, do: "elite", else: "normal"), rng}
      end

    row = Balance.enemy_class(balance, enemy_class)
    factors = Formulas.scaling(info, balance, difficulty)
    stat = fn value, product -> max(1, Formulas.pct(Formulas.scale_stat(value, product), row.stat_pct)) end
    reward = fn value, product -> Formulas.pct(Formulas.scale_reward(value, product), row.reward_pct) end
    hp = stat.(creature.hp, factors.hp_pct_product)

    attacks =
      Enum.map(creature.attacks, fn attack ->
        %{
          attack
          | min: stat.(attack.min, factors.damage_pct_product),
            max: stat.(attack.max, factors.damage_pct_product)
        }
      end)

    monster = %MonsterInstance{
      creature_id: creature.id,
      is_boss: creature.is_boss,
      enemy_class: enemy_class,
      hp: hp,
      max_hp: hp,
      xp: reward.(creature.xp, factors.reward_xp_pct_product),
      gold_min: reward.(creature.gold_min, factors.reward_gold_pct_product),
      gold_max: reward.(creature.gold_max, factors.reward_gold_pct_product),
      attacks: attacks
    }

    {monster, info, rng}
  end
end
