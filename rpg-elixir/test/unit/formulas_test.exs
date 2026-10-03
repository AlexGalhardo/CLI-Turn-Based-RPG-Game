defmodule Rpg.Unit.FormulasTest do
  use ExUnit.Case, async: true

  alias Rpg.Domain.Definitions.{Balance, GameData}
  alias Rpg.Domain.Formulas
  alias Rpg.Test.Helpers

  test "pct floors" do
    assert Formulas.pct(99, 50) == 49
    assert Formulas.pct(0, 150) == 0
    assert Formulas.pct(7, 100) == 7
  end

  test "pct rejects negative operands" do
    assert_raise ArgumentError, ~r/non-negative/, fn -> Formulas.pct(-1, 10) end
  end

  test "clamp" do
    assert Formulas.clamp(5, 0, 3) == 3
    assert Formulas.clamp(-1, 0, 3) == 0
    assert Formulas.clamp(2, 0, 3) == 2
  end

  for {level, xp} <- [{1, 0}, {2, 100}, {3, 200}, {4, 400}, {5, 800}, {10, 9300}, {20, 98_800}, {100, 15_694_800}] do
    test "xp for level #{level} matches the Tibia table" do
      assert Formulas.xp_for_level(unquote(level)) == unquote(xp)
    end
  end

  test "mana for magic level is cumulative" do
    balance = Helpers.data().balance
    base = balance.magic_level_base
    assert Formulas.mana_for_magic_level(1, balance) == base
    assert Formulas.mana_for_magic_level(2, balance) == base + div(base * balance.magic_level_growth_pct, 100)
    assert Formulas.mana_for_magic_level(5, balance) > Formulas.mana_for_magic_level(4, balance)
  end

  test "spell level for uses" do
    levels = Helpers.data().balance.spell_levels

    for {uses, level} <- [{0, 1}, {19, 1}, {20, 2}, {49, 2}, {50, 3}, {5000, 3}] do
      assert Formulas.spell_level_for_uses(uses, levels).level == level
    end
  end

  test "armor mitigation" do
    assert Formulas.armor_mitigation(100, 0) == 100
    assert Formulas.armor_mitigation(100, 100) == 50
    assert Formulas.armor_mitigation(10, 3) == 9
  end

  for {round, expected} <- [
        {1, {0, 0, 0, false}},
        {9, {0, 0, 8, false}},
        {10, {0, 0, 9, true}},
        {11, {1, 0, 0, false}},
        {100, {9, 0, 9, true}},
        {101, {0, 1, 0, false}},
        {250, {4, 2, 9, true}}
      ] do
    test "round info for round #{round}" do
      data = Helpers.data()
      info = Formulas.round_info(unquote(round), data.balance, GameData.tier_count(data))
      assert {info.tier, info.cycle, info.position, info.is_boss} == unquote(Macro.escape(expected))
    end
  end

  test "scaling combines difficulty, cycle and position" do
    data = Helpers.data()
    balance = data.balance
    hard = Balance.difficulty(balance, "hard")
    factors = Formulas.scaling(Formulas.round_info(103, balance, GameData.tier_count(data)), balance, hard)
    cycle_pct = 100 + balance.cycle_stat_pct
    position_pct = 100 + 2 * balance.position_pct
    assert factors.hp_pct_product == hard.hp_pct * cycle_pct * position_pct

    assert Formulas.scale_stat(1000, factors.hp_pct_product) ==
             div(1000 * hard.hp_pct * cycle_pct * position_pct, 1_000_000)

    assert Formulas.scale_reward(100, factors.reward_xp_pct_product) ==
             div(100 * hard.xp_pct * (100 + balance.cycle_reward_pct), 10_000)
  end

  test "bosses ignore position scaling" do
    data = Helpers.data()
    normal = Balance.difficulty(data.balance, "normal")
    info = Formulas.round_info(10, data.balance, GameData.tier_count(data))
    assert Formulas.scaling(info, data.balance, normal).hp_pct_product == normal.hp_pct * 100 * 100
  end
end
