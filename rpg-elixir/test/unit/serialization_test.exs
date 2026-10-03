defmodule Rpg.Unit.SerializationTest do
  use ExUnit.Case, async: true

  alias Rpg.Application.Commands

  alias Rpg.Application.Commands.{
    Attack,
    BuyPotion,
    BuyStockItem,
    Cast,
    Defend,
    Equip,
    NextFight,
    SellItem,
    Unequip,
    UsePotion
  }

  alias Rpg.Application.{GameEngine, RunState}
  alias Rpg.Domain.Entities.{ActiveStatus, AffixRoll, ItemInstance}
  alias Rpg.Domain.JsonTypes
  alias Rpg.Test.Helpers

  @all_commands [
    %Attack{},
    %Cast{spell_id: "brutal_strike"},
    %UsePotion{potion_id: "health_potion"},
    %Defend{},
    %NextFight{},
    %BuyPotion{potion_id: "mana_potion", quantity: 3},
    %SellItem{uid: 4},
    %Equip{uid: 5},
    %Unequip{slot: "ring"},
    %BuyStockItem{index: 1}
  ]

  for command <- @all_commands do
    test "command round trip #{inspect(command)}" do
      command = unquote(Macro.escape(command))
      json = command |> Commands.to_map() |> JSON.encode!() |> JSON.decode!()
      assert Commands.from_map(json) == command
    end
  end

  test "unknown command type" do
    assert_raise ArgumentError, ~r/unknown command/, fn -> Commands.from_map(%{"type" => "dance"}) end
  end

  test "run state round trip through JSON" do
    engine = Helpers.new_engine()
    {engine, _} = GameEngine.step(engine, %NextFight{})

    engine =
      Enum.reduce(1..3, engine, fn _, engine ->
        {engine, _} = GameEngine.step(engine, %Attack{})
        engine
      end)

    engine =
      engine
      |> Helpers.update_player(fn player ->
        item = %ItemInstance{
          uid: 90,
          item_id: "sword",
          rarity: "epic",
          tier: 2,
          affixes: [%AffixRoll{stat: "dodge", value: 3}]
        }

        %{
          player
          | bag: player.bag ++ [item],
            statuses: player.statuses ++ [%ActiveStatus{status_id: "burn", turns: 2, per_turn: 4}]
        }
      end)
      |> Helpers.update_state(fn state ->
        if state.monster do
          put_in(
            state.monster.statuses,
            state.monster.statuses ++ [%ActiveStatus{status_id: "stun", turns: 1, per_turn: 0}]
          )
        else
          state
        end
      end)

    state = engine.state
    raw = state |> RunState.to_map() |> JSON.encode!() |> JSON.decode!()
    restored = RunState.from_map(raw)
    assert restored == state
    assert RunState.to_map(restored) == RunState.to_map(state)
  end

  for {reader, value} <- [{:obj, []}, {:list, %{}}, {:int, true}, {:int, "1"}, {:str, 1}, {:bool, 0}] do
    test "json reader #{reader} rejects #{inspect(value)}" do
      assert_raise JsonTypes.Error, fn -> apply(JsonTypes, unquote(reader), [unquote(Macro.escape(value))]) end
    end
  end

  test "json field reports the missing key" do
    assert_raise JsonTypes.Error, ~r/startHp/, fn -> JsonTypes.field(%{}, "startHp") end
  end
end
