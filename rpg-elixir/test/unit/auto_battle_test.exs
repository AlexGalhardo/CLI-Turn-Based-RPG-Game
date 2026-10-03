defmodule Rpg.Unit.AutoBattleTest do
  @moduledoc "Auto-battle policy decisions (docs/game-design.md §13)."
  use ExUnit.Case, async: true

  import Rpg.Test.Helpers, only: [update_monster: 2, update_player: 2, update_state: 2]

  alias Rpg.Application.Commands.{Attack, Cast, Defend, NextFight, UsePotion}
  alias Rpg.Application.{AutoBattle, GameEngine, RunConfig}
  alias Rpg.Domain.Character
  alias Rpg.Domain.Definitions.{AutoBattleDef, GameData}
  alias Rpg.Test.Helpers

  @offense_turn 1

  defp battle(vocation \\ "warrior", round_number \\ 0) do
    config = %RunConfig{name: "Auto", vocation_id: vocation, difficulty_id: "normal"}
    {engine, _} = GameEngine.new_run(Helpers.data(), config, 11)
    {engine, _} = engine |> update_state(&%{&1 | round: round_number}) |> GameEngine.step(%NextFight{})
    engine
  end

  defp choose(engine, mode, turn) do
    state = %{engine.state | turn: turn}
    AutoBattle.choose(AutoBattle.policy(Helpers.data(), mode), state)
  end

  defp support_turn(mode), do: AutoBattleDef.mode(Helpers.data().balance.auto_battle, mode).support_every - 1

  defp max_hp(engine), do: Character.build_sheet(engine.state.player, Helpers.data()).max_hp

  test "offensive actions per mode" do
    data = Helpers.data()
    engine = battle() |> update_player(&%{&1 | mp: 10_000})
    assert choose(engine, "melee", @offense_turn + 1) == %Attack{}
    assert choose(engine, "spells", @offense_turn) == %Cast{spell_id: "annihilation"}
    assert choose(engine, "balanced", 2) == %Cast{spell_id: "annihilation"}
    engine = update_player(engine, &%{&1 | mp: GameData.spell(data, "brutal_strike").mana})
    assert choose(engine, "spells", @offense_turn) == %Cast{spell_id: "brutal_strike"}
    engine = update_player(engine, &%{&1 | mp: 0})
    assert choose(engine, "spells", @offense_turn) == %Attack{}
  end

  test "support turn cadence per mode" do
    assert Enum.map(AutoBattle.modes(), &support_turn/1) == [4, 4, 1]
  end

  for mode <- ["melee", "spells", "balanced"] do
    @mode mode
    test "the support turn heals below half HP (#{mode})" do
      engine = battle()
      hp = div(max_hp(engine) * 40, 100)
      engine = update_player(engine, &%{&1 | hp: hp, mp: 10_000})
      assert choose(engine, @mode, support_turn(@mode)) == %Cast{spell_id: "wound_cleansing"}
      engine = update_player(engine, &%{&1 | mp: 0})
      assert choose(engine, @mode, support_turn(@mode)) == %UsePotion{potion_id: "health_potion"}
      engine = update_player(engine, &%{&1 | potions: %{}})
      assert choose(engine, @mode, support_turn(@mode)) == %Attack{}
    end
  end

  test "the heal waits for the support turn above the emergency line" do
    engine = battle()
    engine = update_player(engine, &%{&1 | hp: div(max_hp(engine) * 40, 100)})
    assert choose(engine, "melee", @offense_turn) == %Attack{}
  end

  test "emergency heal on any turn" do
    engine = battle()
    engine = update_player(engine, &%{&1 | hp: div(max_hp(engine) * 20, 100), mp: 10_000})
    assert choose(engine, "melee", @offense_turn) == %Cast{spell_id: "wound_cleansing"}
  end

  test "the best potion is the strongest owned" do
    engine =
      battle()
      |> update_player(&%{&1 | hp: 1, mp: 0, potions: Map.put(&1.potions, "strong_health_potion", 1)})

    assert choose(engine, "melee", @offense_turn) == %UsePotion{potion_id: "strong_health_potion"}
  end

  test "the support turn drinks mana when low" do
    engine = battle("mage") |> update_player(&%{&1 | mp: 0})
    turn = support_turn("spells")
    assert choose(engine, "spells", turn) == %UsePotion{potion_id: "mana_potion"}
    engine = update_player(engine, &%{&1 | potions: %{}})
    assert choose(engine, "spells", turn) == %Attack{}
  end

  test "the support turn defends against a telegraphed charge" do
    engine = battle("warrior", 9)
    assert engine.state.monster.is_boss
    every = Helpers.data().balance.boss_telegraph_every
    engine = update_monster(engine, &%{&1 | boss_actions: every})
    turn = support_turn("melee")
    assert choose(engine, "melee", turn) == %Defend{}
    assert choose(engine, "melee", turn + 1) == %Attack{}
    engine = update_monster(engine, &%{&1 | boss_actions: 0})
    assert choose(engine, "melee", turn) == %Attack{}
  end

  for mode <- ["melee", "spells", "balanced"] do
    @mode mode
    test "the policy finishes fights with valid commands (#{mode})" do
      policy = AutoBattle.policy(Helpers.data(), @mode)

      engine =
        Enum.reduce_while(1..500, battle("archer"), fn _, engine ->
          if engine.state.phase != :battle do
            {:halt, engine}
          else
            {engine, events} = GameEngine.step(engine, AutoBattle.choose(policy, engine.state))
            assert Enum.all?(events, &(&1["type"] != "error"))
            {:cont, engine}
          end
        end)

      assert engine.state.phase != :battle
    end
  end

  test "the policy is deterministic" do
    engine = battle("mage")
    rng_state = GameEngine.rng_state(engine)
    first = for mode <- AutoBattle.modes(), turn <- 0..5, do: choose(engine, mode, turn)
    second = for mode <- AutoBattle.modes(), turn <- 0..5, do: choose(engine, mode, turn)
    assert first == second
    assert GameEngine.rng_state(engine) == rng_state
  end
end
