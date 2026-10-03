defmodule Rpg.Unit.MerchantAndLootTest do
  use ExUnit.Case, async: true

  import Rpg.Test.Helpers, only: [new_engine: 0, new_engine: 1, update_player: 2, update_state: 2]

  alias Rpg.Application.Commands.{BuyPotion, BuyStockItem, Equip, NextFight, SellItem, Unequip}
  alias Rpg.Application.{GameEngine, Loot, Merchant}
  alias Rpg.Domain.Character
  alias Rpg.Domain.Definitions.{AffixDef, Balance, GameData}
  alias Rpg.Domain.Entities.{AffixRoll, ItemInstance}
  alias Rpg.Domain.Rng
  alias Rpg.Test.Helpers

  defp loot_data do
    affixes = [
      %AffixDef{id: "of_power", stat: "attack", min: 1, max: 3, per_tier: 2, slots: ["weapon"]},
      %AffixDef{id: "of_the_bear", stat: "maxHp", min: 5, max: 10, per_tier: 5, slots: ["weapon", "helmet"]},
      %AffixDef{id: "of_speed", stat: "dodge", min: 1, max: 2, per_tier: 0, slots: ["weapon", "ring"]},
      %AffixDef{id: "of_speed_2", stat: "dodge", min: 1, max: 2, per_tier: 0, slots: ["weapon"]}
    ]

    GameData.replace(Helpers.with_test_items(Helpers.data()), affixes: affixes)
  end

  defp step(engine, command), do: GameEngine.step(engine, command)
  defp item(uid, id, rarity \\ "common"), do: %ItemInstance{uid: uid, item_id: id, rarity: rarity, tier: 0}

  test "buy potion rules" do
    engine = new_engine()
    gold = engine.state.player.gold
    {engine, events} = step(engine, %BuyPotion{potion_id: "health_potion", quantity: 2})
    assert events == [%{"type" => "potion_bought", "potionId" => "health_potion", "quantity" => 2, "gold" => 100}]
    assert engine.state.player.gold == gold - 100

    for {command, code} <- [
          {%BuyPotion{potion_id: "health_potion", quantity: 1}, "not_enough_gold"},
          {%BuyPotion{potion_id: "health_potion", quantity: 0}, "invalid_quantity"},
          {%BuyPotion{potion_id: "great_health_potion", quantity: 1}, "potion_locked"},
          {%BuyPotion{potion_id: "elixir", quantity: 1}, "unknown_potion"}
        ] do
      assert {_, [%{"type" => "error", "code" => ^code}]} = step(engine, command)
    end

    assert engine.state.stats.potions_bought["health_potion"] == 2
    assert engine.state.stats.gold_spent == 100
  end

  test "available potions unlock by round" do
    data = Helpers.data()
    engine = new_engine()
    assert Merchant.available_potions(engine.state, data) == ["health_potion", "mana_potion"]
    engine = update_state(engine, &%{&1 | round: 80})
    assert length(Merchant.available_potions(engine.state, data)) == length(data.potions)
  end

  test "equip, swap, sell and unequip" do
    data = Helpers.with_test_items(Helpers.data())
    axe = item(50, "test_axe")
    rod = item(51, "test_rod", "rare")
    engine = new_engine(data: data) |> update_player(&%{&1 | bag: &1.bag ++ [axe, rod]})
    starter_uid = engine.state.player.equipment["weapon"].uid

    assert {_, [%{"type" => "error", "code" => "cannot_equip"}]} = step(engine, %Equip{uid: 51})
    {engine, events} = step(engine, %Equip{uid: 50})

    assert events == [
             %{"type" => "item_unequipped", "uid" => starter_uid, "itemId" => "sword", "slot" => "weapon"},
             %{"type" => "item_equipped", "uid" => 50, "itemId" => "test_axe", "slot" => "weapon"}
           ]

    assert Character.build_sheet(engine.state.player, data).melee_min == 8 + 20

    gold = engine.state.player.gold
    {engine, events} = step(engine, %SellItem{uid: 51})

    assert events == [
             %{"type" => "item_sold", "uid" => 51, "itemId" => "test_rod", "gold" => Character.item_value(rod, data)}
           ]

    assert engine.state.player.gold == gold + 250
    assert {_, [%{"code" => "invalid_item"}]} = step(engine, %SellItem{uid: 51})
    {engine, events} = step(engine, %Unequip{slot: "weapon"})
    assert events == [%{"type" => "item_unequipped", "uid" => 50, "itemId" => "test_axe", "slot" => "weapon"}]
    assert {_, [%{"code" => "invalid_item"}]} = step(engine, %Unequip{slot: "weapon"})
    assert {_, [%{"code" => "invalid_item"}]} = step(engine, %Equip{uid: 999})
  end

  test "unequip with a full bag, and hp clamp" do
    data = Helpers.with_test_items(Helpers.data())
    capacity = data.balance.bag_capacity

    engine =
      new_engine(data: data)
      |> update_player(fn player ->
        player = %{player | equipment: Map.put(player.equipment, "helmet", item(70, "test_helmet"))}
        rings = Enum.map(0..(capacity - 1), &item(100 + &1, "test_ring"))
        %{player | hp: Character.build_sheet(player, data).max_hp, bag: player.bag ++ rings}
      end)

    assert {_, [%{"code" => "bag_full"}]} = step(engine, %Unequip{slot: "helmet"})
    engine = update_player(engine, &%{&1 | bag: Enum.drop(&1.bag, -1)})
    {engine, _} = step(engine, %Unequip{slot: "helmet"})
    assert engine.state.player.hp == Character.build_sheet(engine.state.player, data).max_hp
  end

  test "merchant stock purchase" do
    data = loot_data()
    engine = new_engine(data: data)
    assert length(engine.state.merchant_stock) == data.balance.merchant_stock_size
    [stock_item | _] = engine.state.merchant_stock
    price = Merchant.stock_price(stock_item, data)
    engine = update_player(engine, &%{&1 | gold: price})
    {engine, events} = step(engine, %BuyStockItem{index: 0})

    assert events == [
             %{"type" => "item_bought", "uid" => stock_item.uid, "itemId" => stock_item.item_id, "gold" => price}
           ]

    assert stock_item in engine.state.player.bag
    assert {_, [%{"code" => "not_enough_gold"}]} = step(engine, %BuyStockItem{index: 0})
    assert {_, [%{"code" => "invalid_item"}]} = step(engine, %BuyStockItem{index: 9})
    engine = update_player(engine, &%{&1 | bag: &1.bag ++ Enum.map(0..29, fn i -> item(200 + i, "test_ring") end)})
    assert {_, [%{"code" => "bag_full"}]} = step(engine, %BuyStockItem{index: 0})
    {engine, _} = step(engine, %NextFight{})
    assert engine.state.merchant_stock == []
  end

  test "generate item is deterministic and affixes are unique" do
    data = loot_data()
    vocation = GameData.vocation(data, "warrior")
    normal = Balance.difficulty(data.balance, "normal")
    opts = fn tier, uid -> [vocation: vocation, tier: tier, table: "boss", difficulty: normal, uid: uid] end
    first = Enum.map(0..19, &elem(Loot.generate_item(data, Rng.new(5), opts.(0, &1)), 0))
    second = Enum.map(0..19, &elem(Loot.generate_item(data, Rng.new(5), opts.(0, &1)), 0))
    assert first == second

    Enum.reduce(0..199, Rng.new(11), fn uid, rng ->
      {item, rng} = Loot.generate_item(data, rng, opts.(1, uid))
      assert item != nil
      assert item.rarity != "common"
      stats = Enum.map(item.affixes, & &1.stat)
      assert length(stats) == length(Enum.uniq(stats))
      assert Loot.can_use(GameData.item(data, item.item_id), vocation)
      rng
    end)
  end

  test "generate item without candidates consumes nothing" do
    data = Helpers.data()
    normal = Balance.difficulty(data.balance, "normal")
    empty = GameData.replace(data, items: [])
    rng = Rng.new(3)
    opts = [vocation: GameData.vocation(data, "mage"), tier: 9, table: "monster", difficulty: normal, uid: 1]
    assert Loot.generate_item(empty, rng, opts) == {nil, rng}
  end

  test "hard difficulty boosts non-common weights" do
    data = Helpers.data()
    [normal_common | normal] = Loot.rarity_weights(data, "monster", Balance.difficulty(data.balance, "normal"))
    [hard_common | hard] = Loot.rarity_weights(data, "monster", Balance.difficulty(data.balance, "hard"))
    assert hard_common == normal_common
    assert Enum.zip(hard, normal) |> Enum.all?(fn {h, n} -> h >= n end)
  end

  test "item stats apply rarity and affixes" do
    data = Helpers.with_test_items(Helpers.data())

    helmet = %ItemInstance{
      uid: 1,
      item_id: "test_helmet",
      rarity: "legendary",
      tier: 0,
      affixes: [%AffixRoll{stat: "maxHp", value: 7}]
    }

    assert Character.item_stats(helmet, data) == %{"armor" => 15, "maxHp" => 82}
    assert Character.item_value(helmet, data) == 1000
  end

  test "shields follow the vocation shield types" do
    data = Helpers.data()

    for vocation <- data.vocations, item <- data.items, item.slot == "shield" do
      assert Loot.can_use(item, vocation) == item.type in vocation.shield_types
    end
  end
end
