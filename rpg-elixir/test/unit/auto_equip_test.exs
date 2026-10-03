defmodule Rpg.Unit.AutoEquipTest do
  @moduledoc "Auto-equip with auto-sell (docs/game-design.md §8.1)."
  use ExUnit.Case, async: true

  import Rpg.Test.Helpers,
    only: [calm: 1, update_monster: 2, update_player: 2, update_state: 2, with_enemy_class: 3, with_test_items: 1]

  alias Rpg.Application.Commands.{Attack, BuyStockItem, NextFight}
  alias Rpg.Application.{AutoEquip, GameEngine, RunConfig}
  alias Rpg.Domain.Character
  alias Rpg.Domain.Definitions.GameData
  alias Rpg.Domain.Entities.ItemInstance
  alias Rpg.Test.Helpers

  defp engine(data, auto \\ true) do
    config = %RunConfig{name: "Auto", vocation_id: "warrior", difficulty_id: "normal", auto_equip: auto}
    {engine, _} = GameEngine.new_run(data, config, 42)
    engine
  end

  defp item(uid, item_id, rarity \\ "common", tier \\ 0),
    do: %ItemInstance{uid: uid, item_id: item_id, rarity: rarity, tier: tier}

  defp add_to_bag(state, items), do: put_in(state.player.bag, state.player.bag ++ items)

  test "a better item is equipped and the old one sold" do
    data = with_test_items(Helpers.data())
    engine = engine(data)
    starter = engine.state.player.equipment["weapon"]
    axe = item(50, "test_axe")
    state = add_to_bag(engine.state, [axe])
    {events, state} = AutoEquip.auto_equip(state, data)

    assert events == [
             %{
               "type" => "item_auto_equipped",
               "uid" => 50,
               "itemId" => "test_axe",
               "slot" => "weapon",
               "score" => Character.item_score(axe, data)
             },
             %{
               "type" => "item_auto_sold",
               "uid" => starter.uid,
               "itemId" => "sword",
               "gold" => Character.item_value(starter, data)
             }
           ]

    assert state.player.equipment["weapon"] == axe
    assert state.player.gold == engine.state.player.gold + Character.item_value(starter, data)
    assert state.player.bag == []
  end

  test "empty slots are filled without selling" do
    data = with_test_items(Helpers.data())
    state = add_to_bag(engine(data).state, [item(60, "test_helmet")])
    {events, state} = AutoEquip.auto_equip(state, data)
    assert Enum.map(events, & &1["type"]) == ["item_auto_equipped"]
    assert state.player.equipment["helmet"].uid == 60
  end

  test "ties go to the lowest uid and worse items stay" do
    data = with_test_items(Helpers.data())

    state =
      add_to_bag(engine(data).state, [item(72, "test_helmet"), item(71, "test_helmet"), item(73, "test_rod")])

    {_events, state} = AutoEquip.auto_equip(state, data)
    assert state.player.equipment["helmet"].uid == 71
    assert Enum.map(state.player.bag, & &1.uid) == [72, 73]
    assert {[], ^state} = AutoEquip.auto_equip(state, data)
  end

  test "items above the player level are skipped" do
    data = with_test_items(Helpers.data())
    state = add_to_bag(engine(data).state, [item(80, "test_axe", "mythic", 9)])
    assert {[], _} = AutoEquip.auto_equip(state, data)
    state = put_in(state.player.level, 1 + 9 * data.balance.item_level_per_tier)
    {[first | _], _state} = AutoEquip.auto_equip(state, data)
    assert first["uid"] == 80
  end

  test "victory triggers auto-equip only when enabled" do
    game_data = Helpers.data() |> with_test_items() |> calm() |> with_enemy_class("normal", drop_chance_pct: 100)
    weak_sword = %{GameData.item(game_data, "sword") | stats: %{}}

    game_data =
      GameData.replace(game_data, items: Enum.map(game_data.items, &if(&1.id == "sword", do: weak_sword, else: &1)))

    for enabled <- [true, false] do
      {engine, _} = game_data |> engine(enabled) |> GameEngine.step(%NextFight{})
      {engine, events} = engine |> update_monster(&%{&1 | hp: 1}) |> GameEngine.step(%Attack{})
      assert engine.state.phase == :merchant
      assert "item_auto_equipped" in Enum.map(events, & &1["type"]) == enabled
      assert engine.state.stats.items_auto_equipped == if(enabled, do: 1, else: 0)
    end
  end

  test "buying a stock item triggers auto-equip" do
    data = with_test_items(Helpers.data())
    axe = item(90, "test_axe")

    setup = fn engine ->
      engine |> update_state(&%{&1 | merchant_stock: [axe]}) |> update_player(&%{&1 | gold: 10_000})
    end

    {engine, events} = data |> engine() |> setup.() |> GameEngine.step(%BuyStockItem{index: 0})
    assert Enum.map(events, & &1["type"]) == ["item_bought", "item_auto_equipped", "item_auto_sold"]
    assert engine.state.player.equipment["weapon"] == axe
    {_manual, events} = data |> engine(false) |> setup.() |> GameEngine.step(%BuyStockItem{index: 0})
    assert Enum.map(events, & &1["type"]) == ["item_bought"]
  end
end
